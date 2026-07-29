# コンパイルパイプライン全体像

Cmコンパイラは、字句解析（Lexer）→ 構文解析（Parser）→ AST → 型検査 → HIR → MIR → MIR最適化 → LLVM IR → native実行ファイル / JIT実行、という単方向の段構成でソースを処理する。
本書は各段の責務・主要データ構造・入口関数と、ドライバ（`src/cmd/cm/`）からの呼び出し流れを、native/jitバックエンドの観点で記述する。

## 概要

Cmは「構文の形を保持するAST」「名前解決とフラット化を済ませたHIR」「基本ブロックCFGのMIR」という3層の中間表現を経由し、最後にLLVM IRへ変換して機械語を得る多段設計である。
段を分ける理由は、構文検査・型検査（AST）、ラムダ切り出しや単相化などの言語機能の脱糖（HIR→MIR）、バックエンド非依存の最適化（MIR）、機械語生成（LLVM）という関心を分離し、js/sv/wasmを含む複数バックエンドがMIRを共通入力として共有できるようにするためである。
native（`cm compile`）とjit（`cm run`）はMIR→LLVM IR変換器（`MIRToLLVM`）まで完全に共通で、その先が「オブジェクト生成+リンク」か「ORC JITでのインメモリ実行」かだけが異なる。

## データ構造とアルゴリズム

### ドライバ: main.cpp → build.cpp → backend/

CLIエントリは `int main(int argc, char* argv[])`（`src/cmd/cm/main.cpp:18`）で、`cli::parse_options`（`src/cmd/cm/main.cpp:36`）でオプションを解釈した後、`switch (opts.command)`（`src/cmd/cm/main.cpp:89`）で `Check`/`Fmt` 以外のコマンド（Run/Compile/Test）を `driver::run_build(opts, argv[0])`（`src/cmd/cm/main.cpp:100`）へ委譲する。
ビルドパイプライン本体は `int run_build(cli::Options& opts, const char* argv0)`（`src/cmd/cm/build.cpp:47`）で、段階ごとに狭い `try` で囲み、段階名付きで例外を報告する。
段間の共有状態は `struct BuildContext`（`src/cmd/cm/driver.hpp:44`）に集約され、ソースコード・前処理結果・各段の所要時間計測を保持してバックエンド関数へ渡される。
バックエンドの入口は `emit_jit_run` / `emit_sv` / `emit_js` / `emit_llvm` で、いずれも `(BuildContext&, mir::MirProgram&)` を受ける（`src/cmd/cm/driver.hpp:63-66`）。
バックエンド選択は `run_build` 末尾にあり、`Command::Run` なら `emit_jit_run`、`Command::Compile` ならターゲット文字列で sv/js を振り分けた残りすべて（native/wasm/baremetal/uefi）が `emit_llvm` に落ちる（`src/cmd/cm/build.cpp:660-671`）。

### 字句解析: syntax/lexer

`class Lexer`（`src/internal/syntax/lexer/lexer.hpp:15`）が入口で、メインエントリは `std::vector<Token> tokenize()`（`src/internal/syntax/lexer/lexer.hpp:25`）である。
トークンは `struct Token`（`src/internal/syntax/lexer/token.hpp:197`）で、`TokenKind`（`token.hpp:13`）、位置（`start`/`end`）、`std::variant<std::monostate, int64_t, double, std::string>` 型の値（`token.hpp:180`）を持つ。
ドライバからは `Lexer lexer(code, lexer_platform); auto tokens = lexer.tokenize();`（`src/cmd/cm/build.cpp:271-272`）と呼ばれる（`LexerPlatform::SV` はsvターゲット時のみで、native/jitでは `Default`）。

### 構文解析とAST: syntax/parser・syntax/ast

`class Parser`（`src/internal/syntax/parser/parser.hpp:23`）はトークン列を受け取る再帰下降パーサで、入口は `ast::Program parse()`（`parser.hpp:34`）、式は優先順位別の `parse_expr` → … → `parse_primary` の段で解析する（`parser.hpp:83-99`）。
ASTのルートは `struct Program { std::vector<DeclPtr> declarations; ... }`（`src/internal/syntax/ast/nodes.hpp:184`）で、ノードは基底 `struct Node`（`nodes.hpp:28`）の下に `Expr`（`nodes.hpp:75`、種別variantは `nodes.hpp:64-73`）、`Stmt`（`nodes.hpp:125`）、`Decl`（`nodes.hpp:166`）が `std::variant` ベースの種別を持って並ぶ。
ドライバは `Parser parser(std::move(tokens), lexer.is_sv()); program = parser.parse();`（`src/cmd/cm/build.cpp:280-281`）と呼び出し、エラーがあれば診断を整形して終了する（`build.cpp:283-298`）。
パース直後に `ast::TargetFilteringVisitor` がターゲット属性で宣言をフィルタする（`src/cmd/cm/build.cpp:319-320`）。

### 型検査: types/checking（AST上で実施）

型検査はHIRではなくAST上で行われ、`class TypeChecker`（`src/internal/types/checking/checker.hpp:18`）の `bool check(ast::Program&)`（実装は `src/internal/types/checking/decl.cpp:28`）が入口である。
ドライバからは `TypeChecker checker; ... checker.check(program);`（`src/cmd/cm/build.cpp:341-346`）と呼ばれ、推論結果は各 `ast::Expr::type` に書き込まれて後段のloweringが参照する。

### HIR: hir/・hir/lowering

HIRのルートは `struct HirProgram { std::vector<HirDeclPtr> declarations; ... }`（`src/internal/hir/nodes.hpp:580`）で、関数は `struct HirFunction`（`nodes.hpp:388`）、ほかに `HirStruct`（`nodes.hpp:424`）、`HirEnum`（`nodes.hpp:526`）などが並ぶ。
AST→HIR変換の入口は `class HirLowering`（`src/internal/hir/lowering/fwd.hpp:20`）の `HirProgram lower(ast::Program&)`（宣言 `fwd.hpp:23`、実装 `src/internal/hir/lowering/impl.cpp:14`）である。
この段でラムダの独立関数への切り出し（`src/internal/hir/lowering/expr.cpp:1292` の `lower_lambda`）やmatch式の脱糖など、構文糖の展開が行われる。
ドライバからは `hir::HirLowering hir_lowering; hir = hir_lowering.lower(program);`（`src/cmd/cm/build.cpp:431-432`）と呼ばれる。

### MIR: mir/・mir/lowering

MIRのルートは `struct MirProgram`（`src/internal/mir/nodes.hpp:639`）で、関数 `MirFunction`（`nodes.hpp:435`）はローカル変数表 `locals`・引数 `arg_locals`・戻り値ローカル `return_local`・基本ブロック列 `basic_blocks` を持つCFG表現である（詳細は [mir-design.md](mir-design.md)）。
HIR→MIR変換の入口は `class MirLowering`（`src/internal/mir/lowering/lowering.hpp:21`）の `MirProgram lower(const hir::HirProgram&)`（宣言 `lowering.hpp:43`、実装 `src/internal/mir/lowering/lowering.cpp:19`）で、内部はインポート処理・宣言登録・自動impl生成・関数lowering・単相化・クロージャ情報伝播などの多パス構成である（`lowering.cpp:23-68`）。
ドライバからは `mir::MirLowering mir_lowering; ... mir = mir_lowering.lower(hir);`（`src/cmd/cm/build.cpp:450-454`）と呼ばれる。
MIR最適化は `mir::opt::run_optimization_passes(mir, opts.optimization_level, ..., user_opts)`（`src/cmd/cm/build.cpp:494`）で、最適化レベルはCLIの `opts.optimization_level` がそのまま渡る。
`cm compile` 時のみ関数単位DCEとプログラム単位DCE（未到達関数除去）が追加実行される（`src/cmd/cm/build.cpp:528-535`、`build.cpp:541-544`）。
`--sanitize` 指定時は、バックエンド分岐の直前にMIRレベルで検査コードが計装される（`instrument_undefined_checks`: `build.cpp:650`、`instrument_bounds_checks`: `build.cpp:657`）。

### MIR→LLVM IR: codegen/llvm/core

MIR→LLVM IR変換器は `class MIRToLLVM`（`src/internal/codegen/llvm/core/mir_to_llvm.hpp:18`）で、入口は `void convert(const mir::MirProgram&)`（宣言 `mir_to_llvm.hpp:87`、実装 `src/internal/codegen/llvm/core/translate/program.cpp:25`）である。
変換は構造体型の2パス定義の後、関数ごとに `convertFunctionSignature`（実装 `src/internal/codegen/llvm/core/translate/signature.cpp:243`）→ `convertFunction`（`core/translate/function.cpp:25`）→ `convertBasicBlock`（`function.cpp:720`）→ `convertStatement`（`core/statement.cpp:24`）/ `convertTerminator`（`core/terminator.cpp:13`）と降りていく。
このcoreはnativeとjitで完全共有であり、差分コンパイル用にモジュール単位版 `convert(const mir::ModuleProgram&)`（`translate/program.cpp:498`）も持つ。

### native出力: codegen/llvm/native

`class LLVMCodeGen`（`src/internal/codegen/llvm/native/codegen.hpp:30`）が実行ファイル/オブジェクト生成を担い、ドライバ側の `emit_llvm`（`src/cmd/cm/backend/llvm.cpp:26`）がターゲット設定と `llvm_opts.optimizationLevel = opts.optimization_level`（`llvm.cpp:85`）を行って `codegen.compileWithModuleInfo(mir, {})`（`llvm.cpp:191`）を呼ぶ。
コンパイル本体 `LLVMCodeGen::compile`（`src/internal/codegen/llvm/native/codegen.cpp:35`）は、`initialize`（`codegen.cpp:57`、内部で `MIRToLLVM` を生成: `codegen.cpp:725`）→ `generateIR`（`codegen.cpp:60`）→ `verifyModule`（`codegen.cpp:64`）→ `optimize`（`codegen.cpp:89`、O0はスキップ: `codegen.cpp:766`）→ `emit`（`codegen.cpp:102`）の順で進む。
実行形式出力 `emitExecutable`（`codegen.cpp:1066`）はまず `.o` を生成し（`codegen.cpp:1069`）、ターゲット別のリンカ（nativeは `clang++`: `codegen.cpp:1099-1122`、wasmは `wasm-ld`、uefiは `lld-link`、baremetalは `arm-none-eabi-ld`: `codegen.cpp:1084-1090`）を起動する。
環境変数 `CM_MODULE_CODEGEN=1` のnative実行形式ではモジュール分割並列コンパイル経路（`compileModules`: `codegen.cpp:160`、`MirSplitter` 使用: `codegen.cpp:168`）が選択される。

### jit実行: codegen/llvm/jit

`cm run` は `emit_jit_run`（`src/cmd/cm/backend/run.cpp:27`）から `cm::codegen::jit::JITEngine`（`src/internal/codegen/llvm/jit/jit_engine.hpp:29`）を生成し（`run.cpp:136`）、`jit.execute(mir, "main", opts.optimization_level, sanitize_bounds)`（`run.cpp:143`）で実行する。
`JITEngine::execute`（宣言 `jit_engine.hpp:44`、実装 `jit_engine.cpp:182`）は、LLVM ORCの `LLJIT` を初期化（`initializeJIT`: `jit_engine.cpp:54`、ホストCPU検出: `jit_engine.cpp:57`）した後、共通の `MIRToLLVM converter(llvmCtx); converter.convert(program);`（`jit_engine.cpp:197-198`）でIRを得て、`optimizeModule`（`jit_engine.cpp:102`、O1〜O3を `PassBuilder` の `OptimizationLevel` にマップ: `jit_engine.cpp:163-175`）を通し、`jit_->addIRModule`（`jit_engine.cpp:240`）→ `jit_->lookup(entryPoint)`（`jit_engine.cpp:247`）で得た関数ポインタを直接呼ぶ（`jit_engine.cpp:267`）。
ランタイム関数のシンボルはホストプロセスから `DynamicLibrarySearchGenerator::GetForCurrentProcess` で動的解決される（`jit_engine.cpp:88`）。
`cm test`（Runベース）は `#[test]` 属性関数を収集し、関数ごとに独立した `JITEngine` を生成して実行する（`src/cmd/cm/backend/run.cpp:78-116`）。

### 呼び出し流れの要約

```
main.cpp:18 main → main.cpp:100 run_build (build.cpp:47)
  → 前処理 (ImportPreprocessor / ConditionalPreprocessor, build.cpp:178,239)
  → Lexer.tokenize()            build.cpp:271-272  → std::vector<Token>
  → Parser.parse()              build.cpp:281      → ast::Program
  → TargetFilteringVisitor      build.cpp:320
  → TypeChecker.check(program)  build.cpp:346      （AST上で型検査）
  → HirLowering.lower(program)  build.cpp:432      → hir::HirProgram
  → MirLowering.lower(hir)      build.cpp:454      → mir::MirProgram
  → run_optimization_passes     build.cpp:494      （O0/O1/O2）
  → DCE（Compile時のみ）        build.cpp:528,542
  → サニタイザMIR計装           build.cpp:650,657
  → バックエンド分岐            build.cpp:660-671
      Run     → emit_jit_run (run.cpp:27) → JITEngine.execute → ORC JIT実行
      Compile → emit_llvm (llvm.cpp:26)  → LLVMCodeGen.compile → .o + リンク
```

## 実装箇所

| 段 | 責務 | 主なファイル |
|---|---|---|
| ドライバ | CLI解釈・段の駆動・バックエンド分岐 | `src/cmd/cm/main.cpp`, `src/cmd/cm/build.cpp`, `src/cmd/cm/driver.hpp` |
| 前処理 | import展開・条件コンパイル | `src/internal/preprocessor/` |
| 字句解析 | ソース→トークン列 | `src/internal/syntax/lexer/lexer.hpp`, `token.hpp` |
| 構文解析 | トークン列→AST | `src/internal/syntax/parser/parser.hpp`, `parser_decl.cpp` ほか |
| AST | 構文木データ構造 | `src/internal/syntax/ast/nodes.hpp`, `expr.hpp`, `stmt.hpp`, `decl.hpp` |
| 型検査 | AST上の型推論・診断 | `src/internal/types/checking/checker.hpp`, `decl.cpp` ほか |
| HIR | 名前解決済み高水準IR・脱糖 | `src/internal/hir/nodes.hpp`, `src/internal/hir/lowering/` |
| MIR | CFGベース中間表現・最適化 | `src/internal/mir/nodes.hpp`, `mir/lowering/`, `mir/passes/` |
| LLVM共通 | MIR→LLVM IR変換 | `src/internal/codegen/llvm/core/mir_to_llvm.hpp`, `core/translate/` |
| native | 最適化・オブジェクト生成・リンク | `src/internal/codegen/llvm/native/codegen.cpp`, `src/cmd/cm/backend/llvm.cpp` |
| jit | ORC LLJITによるインメモリ実行 | `src/internal/codegen/llvm/jit/jit_engine.cpp`, `src/cmd/cm/backend/run.cpp` |

## 落とし穴とケア

- HIRの寿命: `MirProgram` は `#[test]` 関数やSV initialブロックでHIR文への生ポインタ（`const HirStmt*`）を保持するため、`hir` はバックエンド完了まで生存させる必要がある（`src/cmd/cm/build.cpp:423-425` のスコープ設計）。この不変条件を壊すとダングリング参照になるので、`hir` と `mir` の宣言スコープを分離してはならない。
- 型検査はAST上で完結し、HIR/MIR loweringは `ast::Expr::type` に推論結果が入っていることを前提にする。loweringだけを単体で呼ぶテストや新しい駆動コードを書くときも、必ず `TypeChecker::check` を先に通すこと。
- 最適化レベルは「MIR最適化」（`build.cpp:494`）と「LLVM最適化」（native: `llvm.cpp:85`、jit: `run.cpp:143`）の2箇所へ独立に渡る二段構成であり、片方だけ変えると挙動比較ができなくなる。
- `cm run`（jit）はホストネイティブ実行なのでターゲットポインタ幅の変更対象外である（`src/cmd/cm/main.cpp:84-85`）。クロスターゲットのポインタ幅依存バグはjitでは再現しないことがある。
- js/ts/webターゲットでは構造体コピーの意味論が異なるため、MIR最適化オプション `no_aggregate_copy_prop` で集約コピー伝播を抑止する（`src/cmd/cm/build.cpp:491-493`）。共有コード（MIRパス）を変更するときはこの境界フラグの意味を維持すること。
- 文字列再代入の旧バッファ解放パス（`StringReassignFree`）はメモリ健全性のための処理であり、最適化が無効なO0でも実行される（`src/cmd/cm/build.cpp:504-509`）。最適化スキップ条件を変更してもこのパスを巻き込んではならない。
- 回帰テスト: パイプライン段階を通すgtestは `tests/regression/`（HIR/MIR lowering・最適化・コード生成）、リリースバイナリへの機能テストは `tests/common` をバックエンドスイート（`make test-llvm` / `make test-interpreter` 等）で実行する。

## 関連資料

- [MIRの設計](mir-design.md)
- [クロージャのlowering](../lowering/closures.md)
- [データ付きenumとmatchのlowering](../lowering/enums-and-match.md)
