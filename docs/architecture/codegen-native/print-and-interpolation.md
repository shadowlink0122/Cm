# print系ビルトインと文字列補間

`println("x = {x}")` のような補間付き出力は、型検査時にプレースホルダを実AST部分式へ脱糖し（`LiteralExpr.interp_parts`）、MIR loweringが型検査済みの部分式を「位置プレースホルダ化されたフォーマット文字列＋値ローカル列」として消費し、LLVMコード生成が引数の型ごとに `cm_print_*`／`cm_format_replace_*` などのランタイム関数呼び出しへディスパッチする三段構成で実装されている。書式化の実体はC実装のランタイム（`runtime/print.c`／`runtime/format.c`）に一本化されており、nativeはAOTリンク、jitはホストプロセス内シンボル解決で同一の実装を共有するため、両者の出力は常に一致する。

## 概要

パイプラインは「型検査: 補間プレースホルダを実AST部分式へ脱糖・推論」→「HIR: `println` の別名解決と部分式のHIR化」→「MIR lowering: フォーマット文字列の位置プレースホルダ変換と `cm_println_format` 等への呼び出し構築」→「LLVMコア: 引数型ディスパッチでランタイム関数呼び出しを生成」→「ランタイム: プレースホルダ置換と出力」の順に流れる。

- 型検査の `desugar_interpolation_parts`（`src/internal/types/checking/utils/interp.cpp`）が文字列リテラルのプレースホルダ内容を一度だけ実ASTへパースし、`LiteralExpr.interp_parts`（内容文字列＋式）として保持する。部分式はその場のスコープで通常の式として推論され、スコープ・move後使用・未定義変数・型の診断は本物の検査器が行う。
- HIRでは `println`/`print` はimportエイリアスまたはフォールバックで `__println__`/`__print__` に正規化され、`interp_parts` は `HirLiteral.interp_parts` として型検査済みHIR式に下ろされる（`src/internal/hir/lowering/expr.cpp` の `lower_literal`）。
- print系の実運用経路はMIR loweringの `try_lower_println`（`src/internal/mir/lowering/expr_println.cpp`）で、`extract_named_placeholders`（`src/internal/mir/lowering/expr_interp.cpp:24`）が名前付きプレースホルダを位置プレースホルダへ変換し、`lower_interp_arg_values` が脱糖済み部分式を通常の `lower_expression` で値ローカル列へ降下して `cm_println_format(format, argc, args...)` 呼び出しを組み立てる。
- print以外の文脈（`string s = "x = {x}";` 等）の補間リテラルは `src/internal/mir/lowering/expr/basic.cpp` が同じ `lower_interp_arg_values` を経て `cm_format_string` 呼び出しへ降下する。
- LLVMコアは `convertCallTerminator`（`src/internal/codegen/llvm/core/terminator/call.cpp:70-100`）で `cm_println_format`/`cm_format_string`/`__print__`/`__println__` を検出し、`print_codegen.cpp` のヘルパへ振り分ける。

## データ構造とアルゴリズム

### 型検査時のプレースホルダ脱糖（desugar_interpolation_parts）

型検査の `infer_literal` は文字列リテラルに対して一度だけ `desugar_interpolation_parts` を実行し、`extract_placeholder_exprs` が走査した各プレースホルダ内容を合成ラッパー（`int __interp_part__() { return (content); }`）経由で実ASTへパースして `LiteralExpr.interp_parts` に保持する（`src/internal/types/checking/utils/interp.cpp`）。パースした部分式ツリーにはリテラル位置のSpanを刻印して診断位置を保ち、部分式はその場で `infer_type` に掛けられるため、`{undefined}` はUndefined variable、move後の `{s}` はused after moveの標準診断になる。パース不能な内容は登録されず従来どおりリテラル文字として扱われる。

HIR loweringの `lower_literal` は各部分式を `lower_expr` でHIR化して `HirLiteral.interp_parts` に引き継ぎ、`clone_hir_expr` もこれを保持する（shared所有）。かつてMIR段にあったプレースホルダ再パースのミニパイプラインと影の型チェッカーはこの脱糖に置き換えられ削除済みである。

### MIR lowering: 名前付き→位置プレースホルダ変換と式の埋め込み

```cm
int x = 255;
println("x = {x}, hex = {x:x}");  // → cm_println_format("x = {}, hex = {:x}", 2, x, x)
```

`extract_named_placeholders` はフォーマット文字列を走査し、`{name}` を `{}` に、`{name:spec}` を `{:spec}` に置換しながら変数名リストを抽出する（`src/internal/mir/lowering/expr_interp.cpp:24-281`）。走査はネストした波括弧（構造体リテラル `Key{id: 1}`）・引用符内・`[]`/`()` 内・enumパスの `::` を式の一部として扱う深度カウント方式で、素朴な `find('}')` では壊れるケースを終端判定から除外している。

抽出された各プレースホルダ内容は `lower_interp_arg_values`（`src/internal/mir/lowering/expr_interp.cpp`）が値ローカルへ解決する。`HirLiteral.interp_parts` から内容文字列ごとの出現キューを作り（`{x} {x}` の重複対応）、各 `var_name` に一致する型検査済み部分式を通常の `lower_expression` で降下する。部分式が無い内容（スキャナ差分の防衛経路。全スイート掃引で到達0件）は識別子直接参照のみの `resolve_interp_placeholder` に落ち、解決不能ならエラー型のダミー値になる。メンバ・添字・メソッド・演算子・キャスト等の解決規則はすべて通常の式lowering（型検査済みHIR）のものであり、補間固有の解決規則は存在しない。

補間なし単一引数の `println(v)` はフォーマットを経由せず、引数型から `cm_println_string`/`cm_println_int`/`cm_println_uint`/`cm_println_long`/`cm_println_ulong`/`cm_println_double`/`cm_println_bool`/`cm_println_char` を直接選択する（`expr_println.cpp:132-260`）。

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

型チェック層は `with Display` を `toString(): string`、`with Debug` を `debug(): string` のメソッドシグネチャとして登録し（`src/internal/types/checking/auto_impl.cpp:65-66, 292-299`）、MIR側は生成した関数名を `impl_info[struct]["Display"]` に記録する。補間内の `{obj.toString()}` はマングル名 `Struct__toString` の呼び出しとして降下され、`toString`→`Display`、`debug`→`Debug` の解決は通常のメソッド呼び出しlowering（型検査済みHIR式）が行う。ユーザーが `impl` ブロックで自前の `toString` を定義した場合も同じマングル名で解決されるため、呼び出し側の形は変わらない。

### 書式指定子のランタイム委譲（境界）

`{:x}` などの指定子はMIR・codegenを素通りしてフォーマット文字列内に残り、解釈はランタイムの `cm_format_replace_*` が行う。

```c
// src/internal/codegen/llvm/native/runtime/format.c:2425-2432 — 指定子の解釈はランタイム側
if (strcmp(specifier, ":x") == 0) {
    formatted_value = cm_format_int_hex(value);
} else if (strcmp(specifier, ":X") == 0) {
    formatted_value = cm_format_int_HEX(value);
```

置換の骨格は `cm_format_replace`（`runtime/format.c:2358`）で、最初の `{...}` を値文字列で置き換えた新規文字列を返す（1呼び出し1プレースホルダ消費）。基数変換・精度・ゼロ詰め・最短round-trip表現といった書式化アルゴリズム自体の設計は本書の範囲外で、[numeric-and-casts.md](numeric-and-casts.md) が正典である。

### eprintln と native/jit の共通性

`eprint`/`eprintln` はビルトインではなくCm標準ライブラリの通常関数で、`string` を受けて `write(STDERR_FD, ...)` するだけである（`libs/std/io/console/output.cm:40-51`）。したがって `eprintln("x = {x}")` の補間は引数側の文字列リテラル経路（`cm_format_string`）で呼び出し前に解決済みの文字列が渡る。

print・format系ランタイムの実体は `runtime/print.c`/`runtime/format.c` の1組しかなく、nativeでは生成オブジェクトとAOTリンクされ（[linking-and-runtime.md](linking-and-runtime.md)）、jitでは `DynamicLibrarySearchGenerator::GetForCurrentProcess` がcmバイナリ自身に埋め込まれた同じ実装をホストプロセスから解決する（`src/internal/codegen/llvm/jit/jit_engine.cpp:83-95`）。生成されるLLVM IRはnative/jitで同一であり、差はオブジェクト出力かインプロセス実行かのみである。

## 実装箇所

| ファイル | 役割 |
|---|---|
| `src/internal/types/checking/utils/interp.cpp` | `desugar_interpolation_parts`（型検査時のプレースホルダ脱糖・Span刻印・部分式推論の起点） |
| `src/internal/hir/lowering/expr.cpp` / `decl.cpp` | `println`/`print` の別名解決と `interp_parts` のHIR化（`lower_literal`） |
| `src/internal/mir/lowering/expr_println.cpp` | `__println__` の降下本体（フォーマット判定、ランタイム関数選択、Call終端の構築） |
| `src/internal/mir/lowering/expr_interp.cpp` | `extract_named_placeholders`、`lower_interp_arg_values`（脱糖済み部分式の消費）、`resolve_interp_placeholder`（識別子直接参照の防衛経路） |
| `src/internal/mir/lowering/expr/basic.cpp` | 非print文脈の補間リテラル → `cm_format_string` 呼び出し |
| `src/internal/mir/lowering/auto_impl/debug_display_css.cpp` | `Struct__toString`/`Struct__debug` のMIR合成（ネスト再帰・`impl_info` 登録） |
| `src/internal/codegen/llvm/core/terminator/call.cpp` | `cm_println_format`/`cm_format_string`/`__print__` 系のcodegenディスパッチ |
| `src/internal/codegen/llvm/core/print_codegen.cpp` | 型ディスパッチ（`generateFormatReplace`/`generateValueToString`/`generatePrint*Call`） |
| `src/internal/codegen/llvm/core/runtime/builtins.cpp` | `cm_format_*`/`cm_*_to_string` 宣言シグネチャの一元生成 |
| `src/internal/codegen/llvm/native/runtime/print.c` | `cm_print_*`/`cm_println_*` の出力実体 |
| `src/internal/codegen/llvm/native/runtime/format.c` | `cm_format_*`/`cm_format_replace_*`/エスケープ解除の実体 |
| `libs/std/io/console/output.cm` | `print`/`println`/`eprint`/`eprintln` のstdlib実装（fd直書き） |

## 落とし穴とケア

- 型の呼び分けはHIR型が第一情報である: LLVM上では `bool`/`char` は同じ `i8`、符号の有無も区別されないため、HIR型を失うと `true` が `1`、`'A'` が `65` と表示される類の劣化が起きる。MIRローカルの型がHIR式の型より新しい情報を持つ場合（matchアームのペイロード変数等）はローカル型を優先する不変条件を守ること（`expr_println.cpp:224-231`）。
- 浮動小数リテラルを整数系ランタイムへ流さない: `println(1.0)` が `cm_println_int`（i32引数）に落ちるとLLVMモジュール検証エラーになるため、ランタイム関数選択で `Float`/`Double` を必ず先に判定する（`expr_println.cpp:150-156`）。
- `{{`/`}}` エスケープの解除は「1回だけ」実行される経路を維持する: フォーマット経路では `cm_format_unescape_braces` が入口で解除し、`cm_println_string` 単体でも `cm_unescape_braces` が走るため、経路の組み替え時に二重解除（`{{{{` が `{` になる）や未解除を作りやすい。
- プレースホルダ終端の判定は深度カウントが前提: 構造体リテラル・文字列リテラル・ビットスライス `x[3:0]` を含む補間では、素朴な `find('}')`/`find(':')` に戻すと指定子誤認や終端誤りが再発する（`expr_interp.cpp:52-160`）。
- 補間の解決規則をMIR段へ再導入しない: プレースホルダの意味はすべて型検査時の脱糖（実AST部分式）で決まり、MIR段はそれを消費するだけという不変条件を守る。かつてMIR段のテキストパターン照合・再パースが部分一致で誤った場所を構築するバグクラス（B7/N1/V1〜V4/W5）の再発源だった。防衛経路（`resolve_interp_placeholder`）へ到達した場合はデバッグログが出る。
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
