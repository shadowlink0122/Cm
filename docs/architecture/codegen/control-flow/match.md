# match文・match式のコード生成

Cmのmatchはデータ付きenum（タグ付きunion）に対するパターン分岐であり、HIR lowering段で「タグ読み出し＋整数比較＋ペイロード抽出」のif-elseチェーン（文形式）または三項演算子ネスト（式形式）へ完全に脱糖される。
enum値は`{ i32 tag, [N x i8] payload }`の2フィールド構造体なので、LLVM IRに現れるのはタグフィールドへのGEPとロード、比較分岐、ペイロードフィールドへのGEPという汎用命令だけである。
O3ではSimplifyCFGが比較チェーンをタグ値に対する`switch i32`へ再構成し、ガードは`select`へ、ペイロードコピーはSROAでスカラ化される。

### 文形式match: タグ分岐とペイロード抽出

```cm
enum Shape {
    Circle(int),
    Square(int),
    Empty,
}

#[noinline]
int area_like(Shape s) {
    int result = 0;
    match (s) {
        Shape::Circle(r) => { result = 3 * r * r; }
        Shape::Square(w) => { result = w * w; }
        Shape::Empty => { result = 0; }
    }
    return result;
}
```

最適化前IRでは、armごとに「field 0（タグ）をGEPで読んで期待値と`icmp eq`→不一致なら次のarmへ」という比較チェーンと、一致armでの「field 1（ペイロード）のGEPロード→束縛変数へ格納」が並ぶ。

```llvm
bb0:
  %field_ptr = getelementptr %__TaggedUnion_Shape, ptr %byval_copy_0, i32 0, i32 0
  %field_load = load i32, ptr %field_ptr, align 4   ; タグ読み出し
  %eq = icmp eq i32 %load1, %load2                  ; Circleのタグ0と比較
  ...
bb1:                                                ; Circle arm
  %field_ptr4 = getelementptr %__TaggedUnion_Shape, ptr %local_8, i32 0, i32 1
  %payload_load = load i32, ptr %field_ptr4, align 4 ; ペイロード抽出 → rに束縛
```

O3ではこの比較チェーンがタグ値に対する`switch i32`へ再構成され、各armの計算は`phi`で合流する。

```llvm
define i32 @area_like(ptr nocapture readonly %arg0) local_unnamed_addr #0 {
entry:
  %byval_copy_0.sroa.0.0.copyload = load i32, ptr %arg0, align 1
  ; …（SROAによるペイロードのバイト単位ロードは省略）…
  switch i32 %byval_copy_0.sroa.0.0.copyload, label %bb3 [
    i32 0, label %bb1                               ; Circle
    i32 1, label %bb4                               ; Square
  ]

bb1:                                                ; 3 * r * r
  %mul6 = mul i32 %mul, %local_8.sroa.2.7.insert.insert
  br label %bb3

bb3:
  %local_2.0 = phi i32 [ %mul6, %bb1 ], [ %mul22, %bb4 ], [ 0, %entry ]
  ret i32 %local_2.0
}
```

タグはオフセット0のi32ロード1回で取得され、ペイロード領域`[N x i8]`はSROAがバイト単位のロードとシフト合成へ分解する（構造体を値渡しした場合のbyvalコピー除去の副産物）。
バリアントElse相当のEmpty（タグ2）は`switch`のdefault経由で`phi`の定数0になる。
enumのメモリレイアウトとタグ登録の詳細は[../../lowering/enums-and-match.md](../../lowering/enums-and-match.md)を参照。

### ガードと束縛

```cm
#[noinline]
int describe(Shape s, int limit) {
    int code = 0;
    match (s) {
        Shape::Circle(r) if r > limit => { code = 100; }
        Shape::Circle(r) => { code = r; }
        _ => { code = -1; }
    }
    return code;
}
```

```llvm
define i32 @describe(ptr nocapture readonly %arg0, i32 %arg1) local_unnamed_addr #0 {
entry:
  %byval_copy_0.sroa.0.0.copyload = load i32, ptr %arg0, align 1
  %eq = icmp eq i32 %byval_copy_0.sroa.0.0.copyload, 0
  br i1 %eq, label %bb1, label %bb6

bb1:                                              ; Circleの2armがガードのselectに融合
  ; …（ペイロードrのバイト合成は省略）…
  %gt.not24 = icmp sgt i32 %local_9.sroa.2.7.insert.insert, %arg1
  %spec.select = select i1 %gt.not24, i32 100, i32 %local_9.sroa.2.7.insert.insert
  br label %bb6

bb6:
  %local_3.0 = phi i32 [ -1, %entry ], [ %spec.select, %bb1 ]
  ret i32 %local_3.0
}
```

ガード付きarmは脱糖段で「タグ一致 かつ ガード式」の連結条件になり、ガード式内の束縛変数`r`はペイロード抽出結果へ置換される。
この例ではタグ0の2つのarmが同じペイロードを参照するため、O3でガード判定が`select`（100か`r`）に畳まれ、分岐はタグ判定の1回だけになる。
ワイルドカード`_`は常に成立する最終armとして脱糖され、IR上はタグ不一致時の合流先（`phi`の-1）に対応する。

### 式形式match

```cm
#[noinline]
int tag_of(Shape s) {
    int t = match (s) {
        Shape::Circle(r) => 1,
        Shape::Square(w) => 2,
        Shape::Empty => 3,
    };
    return t;
}
```

```llvm
define i32 @tag_of(ptr nocapture readonly %arg0) local_unnamed_addr #0 {
entry:
  %byval_copy_0.sroa.0.0.copyload = load i32, ptr %arg0, align 1
  switch i32 %byval_copy_0.sroa.0.0.copyload, label %bb8 [
    i32 0, label %bb3
    i32 1, label %bb6
    i32 2, label %bb9
  ]

bb3:
  %local_6.0 = phi i32 [ %local_11.0, %bb6 ], [ 1, %entry ]
  ret i32 %local_6.0
```

式形式はHIR段で三項演算子のネスト（`タグ==0 ? 1 : (タグ==1 ? 2 : 3)`）へ脱糖されるため、最適化前IRは条件分岐の連鎖になる。
O3ではこれもタグの`switch i32`へ再構成され、armの値が`phi`の連鎖として合流する（さらに単純な場合はテーブル参照や算術式まで縮退する）。
網羅性検査は型検査段で完結しているため、lowering以降のIRには「どのarmにも一致しない」経路の考慮が不要である。

## 関連資料

- [../../lowering/enums-and-match.md](../../lowering/enums-and-match.md) — enumのタグ付きunion表現とmatch脱糖の実装詳細
- [../../pipeline/mir-design.md](../../pipeline/mir-design.md) — 脱糖後の分岐が乗るMIR終端命令
- [../../codegen-native/llvm-optimization.md](../../codegen-native/llvm-optimization.md) — 比較チェーンのswitch再構成とSROA
- [switch.md](switch.md) — 整数値に対するswitch文のコード生成
- [if-else.md](if-else.md) — ガード条件のselect化
