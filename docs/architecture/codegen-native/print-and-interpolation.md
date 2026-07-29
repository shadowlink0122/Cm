# print系ビルトインと文字列補間

`println("x = {x}")` のような補間付き出力は、HIR層の補間分解モデルに従ってMIR loweringで「位置プレースホルダ化されたフォーマット文字列＋値ローカル列」へ脱糖され、LLVMコード生成が引数の型ごとに `cm_print_*`／`cm_format_replace_*` などのランタイム関数呼び出しへディスパッチする三段構成で実装されている。書式化の実体はC実装のランタイム（`runtime_print.c`／`runtime_format.c`）に一本化されており、nativeはAOTリンク、jitはホストプロセス内シンボル解決で同一の実装を共有するため、両者の出力は常に一致する。

## 概要

パイプラインは「HIR: `println` を `__println__` ビルトインへ別名解決」→「MIR lowering: 補間文字列の分解と `cm_println_format` 等への呼び出し構築」→「LLVMコア: 引数型ディスパッチでランタイム関数呼び出しを生成」→「ランタイム: プレースホルダ置換と出力」の順に流れる。

- HIRでは `println`/`print` はimportエイリアスまたはフォールバックで `__println__`/`__print__` に正規化される（`src/internal/hir/lowering/expr.cpp:845` ほか）。
- 補間分解の基準モデルは `StringInterpolationProcessor`（`src/internal/hir/string_interpolation.cpp`）が定義し、リテラル片と補間片への分割、`{{`/`}}` エスケープ、`{expr:spec}` の指定子分離、`toString`/`format*` 呼び出しへの脱糖と文字列連結による再構成を提供する。
- print系の実運用経路はMIR loweringの `try_lower_println`（`src/internal/mir/lowering/expr_println.cpp:42`）で、同じ分解規則を `extract_named_placeholders`（`src/internal/mir/lowering/expr_interp.cpp:24`）として実装し、名前付きプレースホルダを位置プレースホルダへ変換して `cm_println_format(format, argc, args...)` 呼び出しを組み立てる。
- print以外の文脈（`string s = "x = {x}";` 等）の補間リテラルは `src/internal/mir/lowering/expr/basic.cpp:52-263` が同じ分解を経て `cm_format_string` 呼び出しへ降下する。
- LLVMコアは `convertCallTerminator`（`src/internal/codegen/llvm/core/terminator/call.cpp:70-100`）で `cm_println_format`/`cm_format_string`/`__print__`/`__println__` を検出し、`print_codegen.cpp` のヘルパへ振り分ける。

## データ構造とアルゴリズム

### HIRの補間分解モデル（StringInterpolationProcessor）

`StringInterpolationProcessor` は補間文字列を `StringPart{LITERAL | INTERPOLATION, content, format_spec}` の列へ分割する（`splitInterpolatedString`、`src/internal/hir/string_interpolation.cpp:76`）。

```cpp
// src/internal/hir/string_interpolation.cpp:53-60 — {name:spec} の指定子分離
size_t colon = content.find(':');
if (colon != std::string::npos) {
    var.name = content.substr(0, colon);
    var.format_spec = content.substr(colon + 1);
}
```

`createInterpolatedStringExpr`（`:144`）は各片をHIR式へ再構成し、補間片は指定子なしなら `toString` 呼び出し、`x`/`X`/`b`/`o`/`.n` 指定子なら `formatHex`/`formatHexUpper`/`formatBinary`/`formatOctal`/`formatDecimal` 呼び出しへ脱糖して（`applyFormat`、`:198`）、`HirBinaryOp::Add` の連鎖で文字列連結式にする。

### MIR lowering: 名前付き→位置プレースホルダ変換と式の埋め込み

```cm
int x = 255;
println("x = {x}, hex = {x:x}");  // → cm_println_format("x = {}, hex = {:x}", 2, x, x)
```

`extract_named_placeholders` はフォーマット文字列を走査し、`{name}` を `{}` に、`{name:spec}` を `{:spec}` に置換しながら変数名リストを抽出する（`src/internal/mir/lowering/expr_interp.cpp:24-281`）。走査はネストした波括弧（構造体リテラル `Key{id: 1}`）・引用符内・`[]`/`()` 内・enumパスの `::` を式の一部として扱う深度カウント方式で、素朴な `find('}')` では壊れるケースを終端判定から除外している。

抽出された各プレースホルダ内容は `try_lower_println` 内で値ローカルへ解決される。単純な識別子はスコープ解決、`&var`（アドレス）、`*ptr`（デリファレンス）、`obj.field`／`ptr->field` のチェーン、`arr[i]`／スライス添字（`slice_scalar_info` による `cm_slice_get_*` の幅選択）、`Enum::Variant` 定数、`x as int` キャストなどの専用経路があり、演算子を含む一般式やメソッド呼び出しは本物のフロントエンドで降下する。

```cpp
// src/internal/mir/lowering/expr_interp.cpp:288 — 補間式をダミー関数として本物のパイプラインでHIR化
std::string src = "int __interp_expr__() { return (" + content + "); }";
```

この `lower_interp_expression`（`:285`）はLexer→Parser→HirLoweringのミニパイプラインを起動し、enum定義・構造体フィールド・変数型を親コンテキストからシードした上で、return式を現在の関数コンテキストの通常の式loweringに掛ける。

補間なし単一引数の `println(v)` はフォーマットを経由せず、引数型から `cm_println_string`/`cm_println_int`/`cm_println_uint`/`cm_println_long`/`cm_println_ulong`/`cm_println_double`/`cm_println_bool`/`cm_println_char` を直接選択する（`expr_println.cpp:2104-2178`）。

### LLVMコアの型ディスパッチ（print_codegen.cpp）

`generatePrintFormatCall`（`src/internal/codegen/llvm/core/print_codegen.cpp:326`）は、まず `cm_format_unescape_braces` でエスケープを処理した後、値引数ごとに `generateFormatReplace`（`:111`）を畳み込み、最後に `cm_println_string`/`cm_print_string` で出力する。`generateFormatStringCall`（`:417`）は出力の代わりに結果文字列を宛先ローカルへ格納する（非print文脈の補間）。

`generateFormatReplace` の呼び分けは「HIR型を第一情報、LLVM型を第二情報」とする二段判定である。

| 値の型 | 呼び出すランタイム関数 |
|---|---|
| `bool` / `char`（HIR型で判定） | `cm_format_bool`/`cm_format_char` で文字列化後 `cm_format_replace` |
| 32bit以下整数 | `cm_format_replace_int`／符号なしは `cm_format_replace_uint` |
| 64bit整数 | `cm_format_replace_long`／`cm_format_replace_ulong` |
| 浮動小数（floatはdoubleへ拡張） | `cm_format_replace_double` |
| ポインタ（HIR型がPointer） | `cm_format_replace_ptr`（16進表示） |
| 文字列（LLVMポインタ） | `cm_format_replace_string` |
| タグ付きユニオン | 実行時タグで分岐し各バリアント型へ再帰（後述） |

タグ付きユニオンは集約 `{i32, [N x i8]}` またはそのポインタで届くため、タグをロードして各バリアントの比較ブロックを連ね、一致したバリアント型で `generateFormatReplace` を再帰呼び出しし、結果をφノードで合流させる（`:140-215`）。不正なタグは `"<?>"` で、構造体バリアントの詳細表示は `"<struct>"` でプレースホルダを消費する。

`generateValueToString`（`:20`）はフォーマット置換を伴わない単一値の文字列化で、同じ型判定により `cm_format_bool`/`cm_format_char`/`cm_format_int`/`cm_format_uint`/`cm_format_long`/`cm_format_ulong`/`cm_format_double` を呼び分ける（引数連結出力やWasmの `cm_format_string_1..4` 経路で使用）。これらの宣言シグネチャは `src/internal/codegen/llvm/core/runtime/builtins.cpp:88` 以降で一元的に生成される。

### 集約型の文字列化: Display/Debug自動実装への委譲

構造体の文字列化はcodegenではなくMIR段階で解決される。`with Display`/`with Debug` を持つ構造体には `AutoImplGenerator` がMIR関数 `Struct__toString`（`"(v1, v2)"` 形式）と `Struct__debug`（`"Name { f1: v1, ... }"` 形式）を合成する（`src/internal/mir/lowering/auto_impl/debug_display_css.cpp:202` ほか）。フィールドごとに型に応じた `cm_format_int` 等で文字列化して `cm_string_concat` で連結し、フィールドが構造体なら `FieldType__toString`/`FieldType__debug` を呼ぶことで再帰的なネスト表示が成立する。

```cpp
// src/internal/mir/lowering/auto_impl/debug_display_css.cpp:258 — ネスト構造体は自動実装へ再帰
convert_func = field.type->name + "__toString";
```

型チェック層は `with Display` を `toString(): string`、`with Debug` を `debug(): string` のメソッドシグネチャとして登録し（`src/internal/types/checking/auto_impl.cpp:65-66, 292-299`）、MIR側は生成した関数名を `impl_info[struct]["Display"]` に記録する。補間内の `{obj.toString()}` はマングル名 `Struct__toString` の呼び出しとして降下され、`toString`→`Display`、`debug`→`Debug` のインターフェース名が呼び出しメタデータに付与される（`expr_println.cpp:1680-1690`）。ユーザーが `impl` ブロックで自前の `toString` を定義した場合も同じマングル名で解決されるため、呼び出し側の形は変わらない。

### 書式指定子のランタイム委譲（境界）

`{:x}` などの指定子はMIR・codegenを素通りしてフォーマット文字列内に残り、解釈はランタイムの `cm_format_replace_*` が行う。

```c
// src/internal/codegen/llvm/native/runtime_format.c:2425-2432 — 指定子の解釈はランタイム側
if (strcmp(specifier, ":x") == 0) {
    formatted_value = cm_format_int_hex(value);
} else if (strcmp(specifier, ":X") == 0) {
    formatted_value = cm_format_int_HEX(value);
```

置換の骨格は `cm_format_replace`（`runtime_format.c:2358`）で、最初の `{...}` を値文字列で置き換えた新規文字列を返す（1呼び出し1プレースホルダ消費）。基数変換・精度・ゼロ詰め・最短round-trip表現といった書式化アルゴリズム自体の設計は本書の範囲外で、[numeric-and-casts.md](numeric-and-casts.md) が正典である。

### eprintln と native/jit の共通性

`eprint`/`eprintln` はビルトインではなくCm標準ライブラリの通常関数で、`string` を受けて `write(STDERR_FD, ...)` するだけである（`libs/std/io/console/output.cm:40-51`）。したがって `eprintln("x = {x}")` の補間は引数側の文字列リテラル経路（`cm_format_string`）で呼び出し前に解決済みの文字列が渡る。

print・format系ランタイムの実体は `runtime_print.c`/`runtime_format.c` の1組しかなく、nativeでは生成オブジェクトとAOTリンクされ（[linking-and-runtime.md](linking-and-runtime.md)）、jitでは `DynamicLibrarySearchGenerator::GetForCurrentProcess` がcmバイナリ自身に埋め込まれた同じ実装をホストプロセスから解決する（`src/internal/codegen/llvm/jit/jit_engine.cpp:83-95`）。生成されるLLVM IRはnative/jitで同一であり、差はオブジェクト出力かインプロセス実行かのみである。

## 実装箇所

| ファイル | 役割 |
|---|---|
| `src/internal/hir/string_interpolation.cpp` | 補間分解モデル（分割・エスケープ・指定子分離・toString/format*脱糖・連結再構成） |
| `src/internal/hir/lowering/expr.cpp` / `decl.cpp` | `println`/`print` → `__println__`/`__print__` の別名解決 |
| `src/internal/mir/lowering/expr_println.cpp` | `__println__` の降下本体（プレースホルダ内容の値解決、ランタイム関数選択、Call終端の構築） |
| `src/internal/mir/lowering/expr_interp.cpp` | `extract_named_placeholders`、補間式のミニパイプライン降下、`resolve_interp_placeholder` |
| `src/internal/mir/lowering/expr/basic.cpp` | 非print文脈の補間リテラル → `cm_format_string` 呼び出し |
| `src/internal/mir/lowering/auto_impl/debug_display_css.cpp` | `Struct__toString`/`Struct__debug` のMIR合成（ネスト再帰・`impl_info` 登録） |
| `src/internal/codegen/llvm/core/terminator/call.cpp` | `cm_println_format`/`cm_format_string`/`__print__` 系のcodegenディスパッチ |
| `src/internal/codegen/llvm/core/print_codegen.cpp` | 型ディスパッチ（`generateFormatReplace`/`generateValueToString`/`generatePrint*Call`） |
| `src/internal/codegen/llvm/core/runtime/builtins.cpp` | `cm_format_*`/`cm_*_to_string` 宣言シグネチャの一元生成 |
| `src/internal/codegen/llvm/native/runtime_print.c` | `cm_print_*`/`cm_println_*` の出力実体 |
| `src/internal/codegen/llvm/native/runtime_format.c` | `cm_format_*`/`cm_format_replace_*`/エスケープ解除の実体 |
| `libs/std/io/console/output.cm` | `print`/`println`/`eprint`/`eprintln` のstdlib実装（fd直書き） |

## 落とし穴とケア

- 型の呼び分けはHIR型が第一情報である: LLVM上では `bool`/`char` は同じ `i8`、符号の有無も区別されないため、HIR型を失うと `true` が `1`、`'A'` が `65` と表示される類の劣化が起きる。MIRローカルの型がHIR式の型より新しい情報を持つ場合（matchアームのペイロード変数等）はローカル型を優先する不変条件を守ること（`expr_println.cpp:2096-2103`）。
- 浮動小数リテラルを整数系ランタイムへ流さない: `println(1.0)` が `cm_println_int`（i32引数）に落ちるとLLVMモジュール検証エラーになるため、ランタイム関数選択で `Float`/`Double` を必ず先に判定する（`expr_println.cpp:2072-2076`）。
- `{{`/`}}` エスケープの解除は「1回だけ」実行される経路を維持する: フォーマット経路では `cm_format_unescape_braces` が入口で解除し、`cm_println_string` 単体でも `cm_unescape_braces` が走るため、経路の組み替え時に二重解除（`{{{{` が `{` になる）や未解除を作りやすい。
- プレースホルダ終端の判定は深度カウントが前提: 構造体リテラル・文字列リテラル・ビットスライス `x[3:0]` を含む補間では、素朴な `find('}')`/`find(':')` に戻すと指定子誤認や終端誤りが再発する（`expr_interp.cpp:52-160`）。
- 補間内容の式降下が失敗したときは従来のパターン処理へフォールバックし、失敗を握りつぶして未初期化テンポラリを渡さない: ゴミ値表示のバグクラスを防ぐ不変条件である（`expr_println.cpp:141-149`）。
- タグ付きユニオンのフォーマットは全バリアント分岐＋デフォルト分岐（`"<?>"`）を必ず持つ: 分岐漏れはプレースホルダが未消費のまま残り、以降の置換が1つずつずれる。
- プリミティブ型の `impl` メソッド内で `self` を補間する場合、selfはポインタで届くため値をロードしてHIR型を実型に差し替えてから型ディスパッチに掛ける（`print_codegen.cpp:348-401`）。
- 回帰テストは `tests/common/formatting/`（指定子・整形）、`tests/common/const_interpolation/`、`tests/common/strings/`・`tests/common/string/` にあり、`make test-llvm` がnative経路、`make test-interpreter` 等が他バックエンドとの出力一致を検証する。

## 関連資料

- [numeric-and-casts.md](numeric-and-casts.md) — `cm_format_*` の書式化アルゴリズム（最短round-trip・基数変換・整列）の正典
- [linking-and-runtime.md](linking-and-runtime.md) — ランタイムCソースのビルドとAOTリンクの仕組み
- [mir-to-llvm.md](mir-to-llvm.md) — Call終端の変換とオペランド変換の全体像
- [../codegen-jit/lljit-engine.md](../codegen-jit/lljit-engine.md) — jitのホストプロセスシンボル解決
- [../strings/representation.md](../strings/representation.md) — 文字列表現と `cm_string_concat` の前提
- [../lowering/enums-and-match.md](../lowering/enums-and-match.md) — タグ付きユニオンの表現とタグ規約
