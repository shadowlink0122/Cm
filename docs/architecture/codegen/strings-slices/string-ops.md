# 文字列操作のCm構文→LLVM IR対訳（O3）

Cmの`string`はLLVM IRでは素の`ptr`（`char*`）であり、連結・比較・補間・組み込みメソッドはすべてCランタイム関数（`cm_*`・`__builtin_string_*`）の呼び出しへloweringされる。
ランタイム関数はLLVMから見ると外部宣言のため、O3でもインライン化・定数畳み込み・デッドコード削除の対象にならず、呼び出し境界がそのままIRに残る。
本文書の抜粋は`CM_DUMP_IR=2 ./cm compile -O3`で取得した最適化後IR（arm64-apple-darwin）である。

### 文字列リテラル

```cm
string a = "hello";
```

```llvm
@strh = private unnamed_addr constant <{ i32, i32, i32, i32, [6 x i8] }> <{ i32 1129141041, i32 5, i32 1395741251, i32 0, [6 x i8] c"hello\00" }>, align 16
; 式の値はデータ先頭（ヘッダ16バイトの直後）へのGEP
ptr getelementptr inbounds (<{ i32, i32, i32, i32, [6 x i8] }>, ptr @strh, i64 0, i32 4, i64 0)
```

リテラルは`{magic, byte_len, magic2, reserved}`の16バイトヘッダ付きパック構造体グローバル定数として発行され、式の値としてはデータ先頭へのGEP（+16バイト）が使われる。
これによりランタイムはポインタの16バイト手前を見るだけでO(1)バイト長取得ができ、`char*`としてのFFI互換も保たれる。
ヘッダレイアウトの詳細は[文字列の内部表現](../../strings/representation.md)を参照。

### 連結 `a + b`（cm_string_concat）

```cm
string c = a + b;
println(c);
```

```llvm
%2 = tail call ptr @cm_string_concat(ptr nonnull getelementptr inbounds (<{ i32, i32, i32, i32, [6 x i8] }>, ptr @strh, i64 0, i32 4, i64 0), ptr nonnull getelementptr inbounds (<{ i32, i32, i32, i32, [6 x i8] }>, ptr @strh.1, i64 0, i32 4, i64 0))
tail call void @cm_println_string(ptr %2)
```

`+`演算子はポインタ同士のAdd変換時に`cm_string_concat`呼び出しへ置換され、ランタイム側で1回の確保と`memcpy`により新規文字列を生成する。
両辺が定数リテラルでもO3でコンパイル時連結はされず、呼び出しがそのまま残る（ランタイム境界）。

### 3項以上の連結（concat3/concat4への平坦化）

```cm
string s3 = a + b + c;
string s4 = a + b + c + d;
```

```llvm
%2 = tail call ptr @cm_string_concat3(ptr nonnull getelementptr inbounds (...), ptr nonnull getelementptr inbounds (...), ptr nonnull getelementptr inbounds (...))
%3 = tail call ptr @cm_string_concat4(ptr nonnull getelementptr inbounds (...), ptr nonnull getelementptr inbounds (...), ptr nonnull getelementptr inbounds (...), ptr nonnull getelementptr inbounds (...))
```

左結合の連結チェーンは中間文字列を作らず、3項は`cm_string_concat3`、4項は`cm_string_concat4`へ平坦化され、確保回数が1回に抑えられる。
中間結果の生成と解放が消えるため、`a + b + c`で`cm_string_concat`が2回呼ばれることはない。

### 再代入時の一時解放（cm_string_free）

```cm
string c = a + b;
must { c = c + "z"; }
```

```llvm
%2 = tail call ptr @cm_string_concat(ptr nonnull getelementptr inbounds (...), ptr nonnull getelementptr inbounds (...))
%3 = tail call ptr @cm_string_concat(ptr %2, ptr nonnull getelementptr inbounds (...))
tail call void @cm_string_free(ptr %2)
```

再代入で不要になった旧文字列にはMIRクリーンアップパスが`cm_string_free`を挿入する（新規所有バッファを返す関数のホワイトリストで判定）。
結果が最終的に未使用でも`cm_string_concat`呼び出し自体はO3で削除されない点に注意（LLVMは外部関数の純粋性を証明できない）。

### 比較 `==` / `<`（cm_strcmp）

```cm
if (a == b) { println("eq"); }
if (a < b) { println("lt"); }
```

```llvm
%cm_strcmp = tail call i32 @cm_strcmp(ptr nonnull getelementptr inbounds (...), ptr nonnull getelementptr inbounds (...))
%streq = icmp eq i32 %cm_strcmp, 0
br i1 %streq, label %bb1, label %bb3
...
%cm_strcmp1 = tail call i32 @cm_strcmp(ptr nonnull getelementptr inbounds (...), ptr nonnull getelementptr inbounds (...))
%strlt = icmp slt i32 %cm_strcmp1, 0
```

文字列比較は常に内容ベースの`cm_strcmp`呼び出し+戻り値の`icmp`に変換され、ポインタ同一性比較は一切使われない。
`==`は`icmp eq %ret, 0`、`<`は`icmp slt %ret, 0`と、比較演算子ごとに述語だけが変わる。

### 補間 `"x={x}"`（cm_format_*呼び出し列）

```cm
int x = 42;
string name = "cm";
println("x={x}, name={name}");
```

```llvm
%2 = tail call ptr @cm_format_unescape_braces(ptr nonnull getelementptr inbounds (...))  ; テンプレート "x={}, name={}"
%3 = tail call ptr @cm_format_replace_int(ptr %2, i32 42)
%4 = tail call ptr @cm_format_replace_string(ptr %3, ptr nonnull getelementptr inbounds (...))
tail call void @cm_println_string(ptr %4)
```

補間文字列はHIRで「位置プレースホルダ付きテンプレート + 埋め込み式のリスト」に分解され、`cm_format_unescape_braces`の後に埋め込み値の型ごとの`cm_format_replace_*`（`_int`/`_string`/`_double`等）が順に連鎖する。
各replace呼び出しは先頭の`{}`を1つ消費して新しい文字列を返すため、呼び出し順がプレースホルダ順に一致する。

### 書式指定付き補間 `{v:x}`

```cm
int v = 255;
println("hex={v:x}");  // hex=ff
```

```llvm
@strh = private unnamed_addr constant <{ i32, i32, i32, i32, [9 x i8] }> <{ i32 1129141041, i32 8, i32 1395741251, i32 0, [9 x i8] c"hex={:x}\00" }>, align 16
%2 = tail call ptr @cm_format_unescape_braces(ptr nonnull getelementptr inbounds (...))
%3 = tail call ptr @cm_format_replace_int(ptr %2, i32 255)
```

書式指定子はコンパイル時に解釈されず、テンプレート定数に`{:x}`の形でそのまま残り、`cm_format_replace_int`がランタイムで指定子を解析して16進文字列化する。
書式の意味論をランタイムに委譲する境界設計は[print/補間のコード生成](../../codegen-native/print-and-interpolation.md)を参照。

### メソッド呼び出し `s.len()` / `s.substring(...)`（__builtin_string_*）

```cm
string s = "こんにちは";
int n = s.len();                 // 5（コードポイント数）
string t = s.substring(1, 3);    // んに
```

```llvm
%2 = tail call i64 @__builtin_string_codepoint_len(ptr nonnull getelementptr inbounds (...))
%trunc = trunc i64 %2 to i32
%3 = tail call ptr @__builtin_string_substring(ptr nonnull getelementptr inbounds (...), i64 1, i64 3)
```

文字列メソッドはHIR loweringで`__builtin_string_*`関数群へ写像され、`len()`はUTF-8コードポイント数を返す`__builtin_string_codepoint_len`、`substring(a, b)`はコードポイント添字で切り出す`__builtin_string_substring`になる。
戻り値は`i64`のため、`int`変数への格納時に`trunc`が入る。

### println / eprintln

```cm
println(7);        // 組み込み: 型ごとにcm_println_*へディスパッチ
println(3.14);
println(true);
```

```llvm
tail call void @cm_println_int(i32 7)
tail call void @cm_println_double(double 3.140000e+00)
tail call void @cm_println_bool(i8 1)
```

組み込み`println`は引数型ごとに`cm_println_int`/`cm_println_double`/`cm_println_bool`/`cm_println_string`等へ静的ディスパッチされ、呼び出し境界はO3でも残る。
一方、`import std::io::console::output::{eprint, eprintln};`で使うライブラリ側の`eprintln`はCmソースで書かれているため、O3では以下のように`write(2)`直接呼び出しまで完全にインライン化される。

```cm
eprintln("error!");
```

```llvm
%2 = tail call i64 @write(i32 2, ptr nonnull getelementptr inbounds (...), i64 6)  ; "error!"
%3 = tail call i64 @write(i32 2, ptr nonnull getelementptr inbounds (...), i64 1)  ; "\n"
```

`strlen`呼び出しも定数長に畳み込まれており、「Cmで書かれたコードは最適化を素通しできるが、Cランタイム関数は境界として残る」という対比がここに端的に現れる。

## 関連資料

- [文字列の内部表現（SDSヘッダ・リテラル発行・連結/解放の実装）](../../strings/representation.md)
- [print/補間のコード生成（補間分解・型ディスパッチ・書式のランタイム委譲）](../../codegen-native/print-and-interpolation.md)
- [UTF-8とコードポイント添字](../../strings/utf8.md)
- [スライス操作のCm構文→LLVM IR対訳](slice-ops.md)
