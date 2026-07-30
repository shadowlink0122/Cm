# 数値・bool・charリテラルのLLVM IR対訳

Cmの数値リテラルは型サフィックスを持たず、宣言や引数などの文脈から型が決まった上でLLVM IRの即値定数（`i32 42`・`double 3.14`など）としてそのまま命令へ埋め込まれる。
O3では定数どうしの演算・比較は完全に畳み込まれるため、値を観測するには`println`への引き渡しや`must { ... }`によるアンカーが必要であり、定数条件の`must`自体もtrueへ畳み込まれてIRから消える。
本文書の抜粋は`CM_DUMP_IR=2 ./cm compile -O3`で取得した最適化後IRであり、対比が必要な箇所のみ`-O0`（`CM_DUMP_IR=1`）のIRを併記する。

### 10進整数リテラル

```cm
int main() {
    int dec = 42;
    println(dec);
    return 0;
}
```

```llvm
tail call void @cm_println_int(i32 42)
ret i32 0
```

`int`は`i32`へ写像され、リテラル42は変数を経由せず`i32 42`の即値として使用箇所へ直接埋め込まれる。
O0では`store i32 42, ptr %local_1`と`load`の組で現れるが、O3のmem2reg/SROAでalloca経由の受け渡しは消滅する。
型の写像規則は[../../codegen-native/numeric-and-casts.md](../../codegen-native/numeric-and-casts.md)を参照。

### 基数プレフィックス付きリテラル（0x・0b・0o）

```cm
int hex = 0xFF;
int bin = 0b1010;
int oct = 0o755;
println(hex);
println(bin);
println(oct);
```

```llvm
tail call void @cm_println_int(i32 255)
tail call void @cm_println_int(i32 10)
tail call void @cm_println_int(i32 493)
```

16進（`0x`）・2進（`0b`）・8進（`0o`）の各プレフィックスはレクサの段階で整数値へ変換されるため、IRには基数の痕跡が残らず10進の即値だけが現れる。
`0b`リテラルは`?`（don't careビット）を含むと`match`パターン専用のマスク付きリテラルになるが、通常の値文脈では単なる整数定数である。
字句解析の詳細は`src/internal/syntax/lexer/lexer.cpp`の`scan_number`を参照。

### 型サフィックスの不在と文脈による型決定

```cm
long big = 1234567890123;
uint u = 4294967295;
println(big);
println(u);
```

```llvm
tail call void @cm_println_long(i64 1234567890123)
tail call void @cm_println_uint(i32 -1)
```

Cmには`42L`や`10u`のような型サフィックスが存在せず、リテラルは代入先や仮引数の型（この例では`long`＝`i64`、`uint`＝`i32`）をそのまま受け取って定数化される。
LLVMの整数定数は符号を持たないビットパターンなので、`uint`の4294967295は`i32 -1`と表示されるが、符号なし解釈は`cm_println_uint`のような使用側の演算・関数が担う。
型推論の仕組みは[../../types/inference.md](../../types/inference.md)を参照。

### 浮動小数リテラル

```cm
double d = 3.14;
double e = 2.5e3;
println(d);
println(e);
```

```llvm
tail call void @cm_println_double(double 3.140000e+00)
tail call void @cm_println_double(double 2.500000e+03)
```

小数点または指数部（`e`/`E`）を含むリテラルは`FloatLiteral`となり、`double`は`double`型の即値定数として埋め込まれる。
`float`文脈のリテラルは`float`定数になり、例えば`float`引数との乗算は`fmul float %arg0, 5.000000e-01`のように単精度のまま演算される。
浮動小数の変換・拡幅規則は[../../types/casts.md](../../types/casts.md)を参照。

### boolリテラル

```cm
bool flag = true;
println(flag);
```

```llvm
tail call void @cm_println_bool(i8 1)
```

`true`/`false`はメモリ表現としては`i8`の1/0であり、比較演算の結果（`i1`）を格納する際は`zext i1 %eq to i8`で拡張される。
O3では定数boolを条件に持つ分岐は畳み込まれ、到達しない側のコードごと消える。

### charリテラル

```cm
char a = 'A';
char nl = '\n';
println(a);
println(nl);
```

```llvm
tail call void @cm_println_char(i8 65)
tail call void @cm_println_char(i8 10)
```

`'A'`のようなcharリテラルは`i8`の文字コード即値（65）となり、`'\n'`等のエスケープはレクサで解決済みの値（10）が埋め込まれる。
文字列内のマルチバイト文字の扱いは[../../strings/utf8.md](../../strings/utf8.md)を参照。

### 負数リテラル

```cm
int neg = -42;
println(neg);
println(-neg);
```

```llvm
tail call void @cm_println_int(i32 -42)
tail call void @cm_println_int(i32 42)
```

`-42`は構文上は正のリテラル42への単項マイナスであり、O0では`%neg = sub i32 0, %load`という減算命令として現れる。
O3では定数畳み込みにより`i32 -42`の即値へ縮約され、二重否定`-neg`も`i32 42`まで畳み込まれる。

### 暗黙拡幅を伴う文脈

```cm
int n = 7;
long sum = n + 100;
println(sum);
```

O0のIR（抜粋）:

```llvm
%add = add i32 %load2, %load3
%sext = sext i32 %load4 to i64
store i64 %sext, ptr %local_3, align 8
```

`int`変数を含む式を`long`へ代入すると、演算は`i32`で行われた後に`sext i32 ... to i64`で符号拡張される。
一方`long big = 1234567890123;`のようにリテラルを直接`long`文脈へ置いた場合は最初から`i64`定数として生成され、実行時変換は発生しない。
O3ではこの例全体が定数伝播で`i64 107`に畳み込まれ、`sext`もIRから消える。
拡幅・縮小変換の網羅的な規則は[../../codegen-native/numeric-and-casts.md](../../codegen-native/numeric-and-casts.md)を参照。

### O3による定数式の消滅

```cm
must { 0xFF == 255; }
```

この`must`ブロックはO0では`icmp eq i32 255, 255`と失敗時分岐を生成するが、O3では条件が定数trueに畳み込まれてブロックごとIRから消える。
リテラルのみで構成された式・検証は最適化後のIRに一切残らないため、対訳を確認する際は実行時入力（`main`の引数など）や`println`で値を生かす必要がある。
最適化パイプラインの構成は[../../codegen-native/optimization-levels.md](../../codegen-native/optimization-levels.md)を参照。

## 関連資料

- [../../codegen-native/numeric-and-casts.md](../../codegen-native/numeric-and-casts.md) — 数値型の写像とキャスト命令の網羅
- [../../types/inference.md](../../types/inference.md) — リテラル型を決める型推論
- [../../types/casts.md](../../types/casts.md) — 明示キャストのlowering
- [../../codegen-native/optimization-levels.md](../../codegen-native/optimization-levels.md) — O0〜O3の各パス構成
- [string-literals.md](string-literals.md) — 文字列リテラルの対訳
- [aggregate-literals.md](aggregate-literals.md) — 配列・スライス・構造体リテラルの対訳
