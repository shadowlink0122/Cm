# スライス操作のCm構文→LLVM IR対訳（O3）

Cmの動的配列（スライス、`T[]`）はLLVM IRではランタイムのヘッダ構造体`CmSlice`への不透明ポインタ（`ptr`）であり、生成・push・添字・長さ取得などの全操作が`cm_slice_*`ランタイム関数の呼び出しへloweringされる。
ランタイム関数は外部宣言のためLLVMは副作用の有無を証明できず、O3でも呼び出しは融合・削除されない（結果が未使用の`cm_slice_new`すら残る）。
本文書の抜粋は`CM_DUMP_IR=2 ./cm compile -O3`で取得した最適化後IR（arm64-apple-darwin）で、lowering由来の未使用一時`cm_slice_new`呼び出しは抜粋から省略している。

### 空スライスと配列リテラル `int[] xs = [];`

```cm
int[] xs = [];
int[] ys = [1, 2, 3, 4, 5];
```

```llvm
%2 = tail call ptr @cm_slice_new(i64 4, i64 0)
%3 = tail call ptr @cm_slice_new(i64 4, i64 5)
tail call void @cm_slice_push_i32(ptr %3, i32 1)
tail call void @cm_slice_push_i32(ptr %3, i32 2)
...
```

生成は`cm_slice_new(elem_size, initial_cap)`で、第1引数は要素サイズ（`int`は4）、第2引数は初期容量（空リテラルは0、要素付きリテラルは要素数）になる。
リテラルの各要素は要素型の幅サフィックス付き`cm_slice_push_i32`で順に積まれ、O3でも一括初期化には畳み込まれない。
要素型→関数サフィックスの対応は[スライスと配列のコード生成](../../codegen-native/slice-and-array-codegen.md)を参照。

### `xs.push(1)`

```cm
xs.push(1);
xs.push(2);
```

```llvm
tail call void @cm_slice_push_i32(ptr %2, i32 1)
tail call void @cm_slice_push_i32(ptr %2, i32 2)
```

pushは容量拡張（2倍成長）を含めてすべてランタイム側の`cm_slice_push_i32`が担い、IRには引数を渡すだけの呼び出しが並ぶ。
ヘッダのアドレスは成長しても変わらないため、`CmSlice*`を保持するIR側のSSA値はそのまま使い続けられる。

### 添字の読み書き `xs[0]`

```cm
xs[0] = 10;      // 書き込み
int v = xs[1];   // 読み出し
```

```llvm
%3 = tail call ptr @cm_slice_get_element_ptr(ptr %2, i64 0)
store i32 10, ptr %3, align 4
%4 = tail call i32 @cm_slice_get_i32(ptr %2, i64 1)
```

書き込みは`cm_slice_get_element_ptr`で要素アドレスを取得してからIR側の`store`で書き、読み出しは値を直接返す`cm_slice_get_i32`になる。
境界検査はどちらもランタイム関数の内部で行われるため、O3でも添字アクセスが素の`load`/`store`へ最適化されることはない（詳細は[境界検査](../../slices/bounds-checking.md)）。

### `xs.len()`

```cm
int n = xs.len();
println(n);
```

```llvm
%5 = tail call i64 @cm_slice_len(ptr %2)
%trunc = trunc i64 %5 to i32
tail call void @cm_println_int(i32 %trunc)
```

長さはヘッダの`len`フィールドを返す`cm_slice_len`（戻り値`i64`）で取得し、`int`文脈では`trunc`が入る。
`println(xs.len())`のように`uint`文脈で直接使うと出力側は`cm_println_uint`にディスパッチされる。

### pop / delete

```cm
int last = xs.pop();
xs.delete(0);        // remove(i)/delete(i)/clear()が削除系メソッド
```

```llvm
%3 = tail call i32 @cm_slice_pop_i32(ptr %2)
tail call void @cm_slice_delete(ptr %2, i64 0)
```

`pop()`は要素型幅付きの`cm_slice_pop_i32`で末尾値を返し、`delete(i)`（別名`remove(i)`）は幅に依存しない`cm_slice_delete`で中間要素を詰める。
なお`insert`メソッドは提供されておらず、削除系は`remove`/`delete`/`clear`の3種である。

### サブスライス `xs[1:3]`

```cm
int[] xs = [10, 20, 30, 40];
int[] ys = xs[1:3];
println(ys[0]);      // 20
println(ys.len());   // 2
```

```llvm
%3 = tail call ptr @cm_slice_subslice(ptr %2, i64 1, i64 3)
%4 = tail call i32 @cm_slice_get_i32(ptr %3, i64 0)
tail call void @cm_println_int(i32 %4)
%5 = tail call i64 @cm_slice_len(ptr %3)
```

範囲構文`xs[a:b]`は半開区間`[a, b)`を切り出す`cm_slice_subslice`へloweringされ、結果は通常のスライスとして以降の`cm_slice_*`操作にそのまま渡せる。

### 二次元スライス `xs[i][j]`

```cm
int[][] rows = [];
int[] r0 = [1, 2];
rows.push(r0);
rows[0].push(3);         // 内側スライスへのミューテーション
int v = rows[0][1];      // 読み出し
```

```llvm
%2 = tail call ptr @cm_slice_new(i64 32, i64 0)          ; 外側: elem_size=sizeof(CmSlice)
%3 = tail call ptr @cm_slice_new(i64 4, i64 2)
tail call void @cm_slice_push_i32(ptr %3, i32 1)
tail call void @cm_slice_push_i32(ptr %3, i32 2)
tail call void @cm_slice_push_slice(ptr %2, ptr %3)
%4 = tail call ptr @cm_slice_get_subslice_ref(ptr %2, i64 0)
tail call void @cm_slice_push_i32(ptr %4, i32 3)
%5 = tail call ptr @cm_slice_get_subslice(ptr %2, i64 0)
%6 = tail call i32 @cm_slice_get_i32(ptr %5, i64 1)
```

多次元スライスは外側の要素サイズが`CmSlice`ヘッダそのもの（インライン格納）になり、内側スライスの追加は`cm_slice_push_slice`でヘッダ値をコピーする。
`rows[0].push(3)`のようなミューテーションのチェーンでは格納中ヘッダへの参照を返す`cm_slice_get_subslice_ref`が使われ、読み出しの`rows[0][1]`ではヘッダをコピーして返す`cm_slice_get_subslice`が使われる。
メソッドチェーンのレシーバ解決の仕組みは[チェーンレシーバ](../../slices/chain-receiver.md)を参照。

### 構造体メンバスライスの操作

```cm
struct Bag {
    int[] items;
};

Bag b = { items: [] };
b.items.push(7);
b.items[0] = 70;
println(b.items.len());
```

```llvm
%2 = tail call ptr @cm_slice_new(i64 4, i64 0)
tail call void @cm_slice_push_i32(ptr %2, i32 7)
%3 = tail call ptr @cm_slice_get_element_ptr(ptr %2, i64 0)
store i32 70, ptr %3, align 4
%5 = tail call i64 @cm_slice_len(ptr %2)
```

構造体はメンバとしてスライスハンドル（`CmSlice*`）を1ワードで保持するだけなので、ローカル構造体はO3のSROAで分解され、メンバスライスへの操作は変数スライスと同一の`cm_slice_*`列に落ちる（IRから`%Bag`型が消える）。
一方、スライスに格納された構造体の要素経由（`bags[0].items.push(v)`のような形）では、要素アドレス取得`cm_slice_get_element_ptr`とメンバの`load`を経てハンドルへ到達する形になり、二次元の場合の`cm_slice_get_subslice_ref`と同様に「格納場所への参照を得てから操作する」パターンとなる。
nativeバックエンドで動作する構造体メンバスライスの操作範囲には制限があり、可変長データ中心の用途ではjs/tsバックエンドの利用が案内されている（[スライスと配列のコード生成](../../codegen-native/slice-and-array-codegen.md)参照）。

## 関連資料

- [スライスと配列のコード生成（要素型ディスパッチ・多次元・固定長配列との使い分け）](../../codegen-native/slice-and-array-codegen.md)
- [スライスのランタイム表現（CmSliceヘッダ・成長戦略・関数対応表）](../../slices/runtime-representation.md)
- [境界検査](../../slices/bounds-checking.md)
- [チェーンレシーバ](../../slices/chain-receiver.md)
- [文字列操作のCm構文→LLVM IR対訳](string-ops.md)
