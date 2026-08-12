# 配列・スライス・構造体リテラルのLLVM IR対訳

Cmの集約リテラルは要素型と入れ物の種類でloweringが分かれ、固定長配列`int[N]`はスタック上の`alloca [N x iM]`への要素ごとの`store`列、スライス`T[]`は`cm_slice_new`＋`cm_slice_push_*`のランタイム呼び出し列、構造体リテラルはフィールドごとのGEP＋`store`列になる。
O3ではSROAと定数伝播が強力に効くため、添字やフィールドが全て定数で決まる集約はメモリ表現ごと消えて結果の即値だけが残り、実行時入力で添字が決まる場合にのみ`alloca`と`store`列が生き残る。
本文書の抜粋は`CM_DUMP_IR=2 ./cm compile -O3`の最適化後IRで、消える場合はその事実自体を対訳として記す。

### 固定長配列リテラル（定数添字のみ: O3で消滅）

```cm
int main() {
    int[3] arr = [10, 20, 30];
    println(arr[0] + arr[1] + arr[2]);
    return 0;
}
```

```llvm
tail call void @cm_println_int(i32 60)
ret i32 0
```

要素も添字も定数のため、配列の`alloca`・`store`列はSROAで分解された後に定数畳み込みされ、合計値`i32 60`だけがIRに残る。
固定長配列リテラルが「実行時に構築される」わけではないことを示す代表例であり、O0では`alloca [3 x i32]`と3本の`store`が現れる。
配列とスライスのコード生成全体は[../../codegen-native/slice-and-array-codegen.md](../../codegen-native/slice-and-array-codegen.md)を参照。

### 固定長配列リテラル（実行時添字: store列が残る）

```cm
int main(int argc, string[] argv) {
    int[4] table = [10, 20, 30, 40];
    println(table[argc]);
    return 0;
}
```

```llvm
%local_4 = alloca [4 x i32], align 4
store i32 10, ptr %local_4, align 4
%elem_ptr3 = getelementptr inbounds [4 x i32], ptr %local_4, i64 0, i64 1
store i32 20, ptr %elem_ptr3, align 4
%elem_ptr6 = getelementptr inbounds [4 x i32], ptr %local_4, i64 0, i64 2
store i32 30, ptr %elem_ptr6, align 4
%elem_ptr9 = getelementptr inbounds [4 x i32], ptr %local_4, i64 0, i64 3
store i32 40, ptr %elem_ptr9, align 4
%idx_ext11 = sext i32 %arg0 to i64
%elem_ptr12 = getelementptr inbounds [4 x i32], ptr %local_4, i64 0, i64 %idx_ext11
%field_load = load i32, ptr %elem_ptr12, align 4
```

添字が`argc`という実行時値のため配列本体が必要になり、`alloca [4 x i32]`に対して要素ごとの`getelementptr`＋`store`でリテラルが構築される。
Cmは配列リテラルをLLVMの`ConstantArray`グローバル＋memcpyではなく個別store列として発行し、定数化はLLVM側の最適化に委ねる方針である。
添字アクセスの境界検査は[../../slices/bounds-checking.md](../../slices/bounds-checking.md)を参照。

### スライスリテラル

```cm
int[] xs = [1, 2, 3];
println(xs.len());
println(xs[1]);
```

```llvm
%2 = tail call ptr @cm_slice_new(i64 4, i64 3)
tail call void @cm_slice_push_i32(ptr %2, i32 1)
tail call void @cm_slice_push_i32(ptr %2, i32 2)
tail call void @cm_slice_push_i32(ptr %2, i32 3)
%4 = tail call i64 @cm_slice_len(ptr %2)
%5 = tail call i32 @cm_slice_get_i32(ptr %2, i64 1)
```

動的配列`int[]`のリテラルは`cm_slice_new(elem_size=4, cap=3)`でヒープ上に`CmSlice`ヘッダとバッファを確保し、要素を`cm_slice_push_i32`で1つずつ追加する完全なランタイム構築になる。
長さ取得や添字アクセスも`cm_slice_len`・`cm_slice_get_i32`のランタイム呼び出しのままで、要素が定数でもO3では畳み込まれない（外部関数呼び出しのため）。
`CmSlice`ヘッダ構造と幅サフィックス付き関数群は[../../slices/runtime-representation.md](../../slices/runtime-representation.md)を参照。

### 空リテラル `[]`

```cm
int[] empty = [];
println(empty.len());
```

```llvm
%3 = tail call ptr @cm_slice_new(i64 4, i64 0)
%6 = tail call i64 @cm_slice_len(ptr %3)
```

空リテラルは要素pushを伴わない`cm_slice_new(elem_size, 0)`単発になり、要素型（ここでは`int`のelem_size=4）は代入先の型注釈から決まる。
固定長配列に`[]`は使えないため、空リテラルは常にスライス構築としてloweringされる。

### 構造体リテラル（O3ではスカラへ分解）

```cm
struct Point { int x; int y; }

Point p = Point { x: 3, y: 4 };
println(p.x * p.y);
Point q = Point { x: argc, y: 10 };
println(q.x + q.y);
```

```llvm
tail call void @cm_println_int(i32 12)
%add = add i32 %arg0, 10
tail call void @cm_println_int(i32 %add)
```

構造体リテラルはO0ではフィールドごとの`getelementptr %Point, ptr %local, i32 0, i32 N`＋`store`列と構造体全体の`load`/`store`コピーで構築される。
O3ではSROAが構造体をフィールド単位のスカラへ分解するため、定数フィールドのみの`p`は`i32 12`へ畳み込まれ、実行時値を含む`q`もメモリを介さない`add`1命令になる。
構造体の値コピー戦略は[../../memory/aggregate-copy.md](../../memory/aggregate-copy.md)を参照。

### コンストラクタ呼び出しによる初期化

```cm
impl Point {
    self(int a, int b) {
        self.x = a;
        self.y = b;
    }
}

Point p(argc, 20);
println(p.x + p.y);
```

```llvm
define void @Point__ctor_2(ptr nocapture writeonly %arg0, i32 %arg1, i32 %arg2) local_unnamed_addr #0 {
entry:
  store i32 %arg1, ptr %arg0, align 4
  %field_ptr2 = getelementptr %Point, ptr %arg0, i64 0, i32 1
  store i32 %arg2, ptr %field_ptr2, align 4
  ret void
}

; main側は呼び出しがインライン化されて畳み込まれる
%add = add i32 %arg0, 20
tail call void @cm_println_int(i32 %add)
```

`Point p(argc, 20)`は構築先ポインタを第1引数に取る自由関数`Point__ctor_2`の呼び出しへloweringされ、コンストラクタ本体はフィールドへの`store`列になる。
O3では呼び出し側にインライン化された後にSROAで構造体が消え、リテラル初期化と同じ最終形（`add i32 %arg0, 20`）へ収束する。
メソッド・コンストラクタのlowering規約は[../../lowering/method-chains.md](../../lowering/method-chains.md)を参照。

### ネストした集約リテラル

```cm
int[2][2] grid = [[1, 2], [3, 4]];
println(grid[argc][0]);
```

```llvm
%local_4 = alloca [2 x [2 x i32]], align 4
store i32 1, ptr %local_4, align 4
%elem_ptr16 = getelementptr inbounds [2 x [2 x i32]], ptr %local_4, i64 0, i64 1
store i32 3, ptr %elem_ptr16, align 4
%elem_ptr22 = getelementptr inbounds [2 x [2 x i32]], ptr %local_4, i64 0, i64 %idx_ext18, i64 0
%field_load = load i32, ptr %elem_ptr22, align 4
```

ネストした配列リテラルは多次元の`alloca [2 x [2 x i32]]`へのstore列となり、O3のデッドストア除去が読まれる可能性のある要素（各行の先頭1と3）だけを残して他のstoreを削除する。
構造体入れ子（`Line { start: Point {...}, end: Point {...} }`）も同様に、O3では読まれるフィールドの計算だけがスカラとして残る。
`Point[2]`のような構造体配列のリテラルでも、使用フィールド（例: `.y`）へのstoreのみが生き残る同じパターンになる。

### O3による集約リテラルの消滅規則まとめ

固定長配列と構造体のリテラルはメモリ確保を伴わないスタック構築のため、全アクセスが定数で解決できればIRから完全に消える。
スライスリテラルはランタイム関数呼び出しで構築されるため、未使用でも`cm_slice_new`呼び出し自体は副作用扱いで残る。
この差は「消えて欲しい一時集約はスライスでなく固定長配列・構造体で書く」という性能上の指針にもなる。
最適化パスの適用順は[../../codegen-native/optimization-levels.md](../../codegen-native/optimization-levels.md)を参照。

## 関連資料

- [../../codegen-native/slice-and-array-codegen.md](../../codegen-native/slice-and-array-codegen.md) — 配列・スライスのコード生成詳細
- [../../slices/runtime-representation.md](../../slices/runtime-representation.md) — `CmSlice`ヘッダと要素型ディスパッチ
- [../../slices/bounds-checking.md](../../slices/bounds-checking.md) — 添字境界検査のlowering
- [../../memory/aggregate-copy.md](../../memory/aggregate-copy.md) — 集約コピーとポインタ渡しの戦略
- [numeric-literals.md](numeric-literals.md) — 数値・bool・charリテラルの対訳
- [string-literals.md](string-literals.md) — 文字列リテラルの対訳
