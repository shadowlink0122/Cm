# enum構文とLLVM IRの対訳

Cmのenumは、ペイロードを持たない単純enumが裸の `i32` に、データ付きvariantを持つenumが `{ i32 tag, [N x i8] payload }` のタグ付きunion構造体にそれぞれ変換される。
本書は単純enumの整数タグ表現、データ付きvariantの構築store列、matchのタグ分岐とペイロード抽出、enum値の受け渡しについて、`CM_DUMP_IR` で採取した実際のO3 IRを対訳として示す。

### 単純enum（整数タグ）

```cm
enum Color {
    Red,
    Green,
    Blue
}

int main() {
    Color c = Color::Green;
    int n = c as int;
    println("{n}");
    return 0;
}
```

O0では宣言順のタグ値がそのまま `i32` としてstoreされ、enum専用のラッパー型は現れない。

```llvm
; -O0
store i32 1, ptr %local_2, align 4
```

O3ではローカルなenum値は定数畳み込みで消え、使用箇所に即値が直接埋め込まれる。

```llvm
; -O3: mainからenum変数が消え、即値1が直接渡る
%3 = call ptr @cm_format_replace_int(ptr %2, i32 1)
```

単純enumはメモリ表現を持たない純粋な整数タグであり、`as int` は変換命令なしのno-opになる。
タグ値の割り当て規則は [enumとmatchのlowering](../../lowering/enums-and-match.md) を参照。

### 単純enumのmatch

```cm
string name(Color c) {
    match (c) {
        Color::Red => { return "red"; }
        Color::Green => { return "green"; }
        Color::Blue => { return "blue"; }
    }
    return "?";
}
```

```llvm
define nonnull ptr @name(i32 %arg0) local_unnamed_addr #0 {
entry:
  %0 = icmp ult i32 %arg0, 3
  br i1 %0, label %switch.lookup, label %common.ret

switch.lookup:
  %1 = sext i32 %arg0 to i64
  %switch.gep = getelementptr inbounds [3 x ptr], ptr @switch.table.name, i64 0, i64 %1
  %switch.load = load ptr, ptr %switch.gep, align 8
  br label %common.ret
```

matchはlowering段でタグの整数比較チェーンに脱糖され、LLVMの `switch` を経てO3では範囲チェック1回とグローバル配列 `@switch.table.name` からのルックアップテーブル参照に潰される。
armごとの分岐が消えてテーブル引き1回になるのは、全armが定数を返す形にO3のSimplifyCFGが適用された結果である。

### データ付きvariantの構築

```cm
struct Point { int x; int y; }

enum Message {
    Quit,
    Move(Point),
    Write(string)
}

Point p = Point { x: 3, y: 4 };
Message m = Message::Move(p);
```

データ付きenumは最大variantサイズのペイロード領域を持つ `%__TaggedUnion_Message = type { i32, [8 x i8] }` として表現される。
最適化前はタグstoreとペイロードのmemcpyの列になる。

```llvm
; -O3 最適化前（CM_DUMP_IR=1）
%field_ptr2 = getelementptr %__TaggedUnion_Message, ptr %local_6, i32 0, i32 0
store i32 1, ptr %field_ptr2, align 4
%field_ptr3 = getelementptr %__TaggedUnion_Message, ptr %local_6, i32 0, i32 1
call void @llvm.memcpy.p0.p0.i64(ptr %field_ptr3, ptr %local_2, i64 8, i1 false)
```

O3ではPoint一時変数とmemcpyがSROAで消え、タグと各フィールドの即値がallocaへ直接storeされる。

```llvm
; -O3 最適化後（CM_DUMP_IR=2）
store i32 1, ptr %local_6, align 8
%field_ptr3 = getelementptr inbounds %__TaggedUnion_Message, ptr %local_6, i64 0, i32 1
store i32 3, ptr %field_ptr3, align 4
%idx = getelementptr inbounds %__TaggedUnion_Message, ptr %local_6, i64 0, i32 1, i64 4
store i32 4, ptr %idx, align 8
```

構築の脱糖規則（タグ登録とペイロードレイアウト）は [enumとmatchのlowering](../../lowering/enums-and-match.md) が詳しい。

### matchでのタグ分岐とペイロード抽出

```cm
int handle(Message m) {
    match (m) {
        Message::Quit => { return 0; }
        Message::Move(p) => { return p.x + p.y; }
        Message::Write(s) => { return 1; }
    }
    return -1;
}
```

```llvm
define i32 @handle(ptr nocapture readonly %arg0) local_unnamed_addr #0 {
entry:
  %tag = load i32, ptr %arg0, align 1
  switch i32 %tag, label %bb10 [
    i32 0, label %common.ret
    i32 1, label %bb5
    i32 2, label %bb9
  ]

bb5:                                              ; Message::Move(p)
  %0 = getelementptr i8, ptr %arg0, i64 8
  %p.y = load i32, ptr %0, align 1
  %1 = getelementptr i8, ptr %arg0, i64 4
  %p.x = load i32, ptr %1, align 1
  %add = add i32 %p.x, %p.y
  br label %common.ret
```

先頭でタグを1回loadし、lowering段の比較チェーンがO3で単一の `switch` に再構成される。
`Move(p)` のペイロード束縛は、タグ直後のオフセット（この例ではバイト4と8）からの直接loadに潰され、Pointへの中間コピーは残らない。
比較チェーンからswitchへの再構成過程は [enumとmatchのlowering](../../lowering/enums-and-match.md) を参照。

### enum値の受け渡し

単純enumは裸の `i32` なので、引数も戻り値もそのまま整数レジスタで受け渡される（前節 `@name(i32 %arg0)`）。
データ付きenumの引数はポインタで渡され、最適化前は呼び出し先で防御コピーが作られる。

```llvm
; -O3 最適化前: 呼び出し先冒頭の値渡しコピー
define i32 @handle(ptr %arg0) {
entry:
  %byval_copy_0 = alloca %__TaggedUnion_Message, align 8
  call void @llvm.memcpy.p0.p0.i64(ptr %byval_copy_0, ptr %arg0, i64 12, i1 false)
```

O3では読み取りしかないことが証明されるとコピーが除去され、`ptr nocapture readonly %arg0` から直接loadする形になる（前節のIR）。
呼び出し側は構築済みallocaのポインタを `call i32 @handle(ptr nonnull %local_6)` とそのまま渡す。
集約型引数の受け渡し規約は [MIR→LLVM IR変換の構造](../../codegen-native/mir-to-llvm.md) を参照。

## 関連資料

- [enumとmatchのlowering](../../lowering/enums-and-match.md) — タグ付きunionのレイアウト決定とmatch脱糖・網羅性検査の設計
- [MIR→LLVM IR変換の構造](../../codegen-native/mir-to-llvm.md) — 型マッピングと集約型の受け渡し規約
- [LLVM最適化パイプライン](../../codegen-native/llvm-optimization.md) — SROA・SimplifyCFGなど本書のO3変形を担うパス群
- [Result/OptionのIR対訳](result-option.md) — 同じタグ付きunion機構を使う組み込みenumの対訳
- [ユニオン型のIR対訳](union-types.md) — 型そのものをタグにする匿名ユニオンとの比較
