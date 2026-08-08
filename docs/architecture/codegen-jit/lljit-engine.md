---
title: JITエンジン（LLVM ORC / LLJIT）
---

# JITエンジン（LLVM ORC / LLJIT）

`cm run` と `#[test]` テストランナーは、MIRをLLVM IRへ変換してLLVM ORCのLLJITで即時コンパイル・実行するJITエンジン（`cm::codegen::jit::JITEngine`）を実行系として使う。ネイティブバイナリを生成せずホストプロセス内で関数ポインタとして実行するため、コンパイルから実行までのレイテンシが小さく、ランタイム関数はcmバイナリ自身にリンクされたシンボルを動的解決することで追加のリンク工程なしに動作する。

## 概要

ドライバはコマンド種別で実行系を振り分け、`cm run` は `emit_jit_run` へ到達する（src/cmd/cm/build.cpp:660-662）。`cm test` はCLIパース時に `test_mode` を立てて同じ経路へ入る（src/cmd/cm/options.cpp:92-95）。`emit_jit_run`（src/cmd/cm/backend/run.cpp:27）は最初に `--target` を判定し、`js`/`web`/`ts` はJS生成+Node実行へ、`wasm` と `sv` 系は明示エラーへディスパッチする（run.cpp:34-72）。これは「targetを無視して常にJIT実行するとJS指定でもネイティブ意味論で動いてしまう」誤解を防ぐための設計である。

ネイティブJIT経路の処理は次の一本道である。

```
MIR → LLVM IR変換 → verifyModule → CloneModule → optimizeModule(O1/O2/O3)
    → (BoundsCheckingPass) → addIRModule → lookup(entryPoint) → 関数ポインタ呼び出し
```

エラーはC++例外ではなく `JITResult`（exitCode / errorMessage / success、src/internal/codegen/llvm/jit/jit_engine.hpp:20-24）で呼び出し元へ返す。例外境界はJITエンジン内部とJSコード生成のtryに限定される（run.cpp:1-2）。

## データ構造とアルゴリズム

### JITEngineの構成と初期化

`JITEngine` は `llvm::orc::LLJIT` と `llvm::orc::ThreadSafeContext` を保持するコピー禁止のクラスである（jit_engine.hpp:29-68）。コンストラクタでLLVMのネイティブターゲット初期化を関数ローカルstaticで一度だけ行う（jit_engine.cpp:39-45）。JIT本体の構築は `JITTargetMachineBuilder::detectHost()` でホストCPUのCPU名と機能フラグを自動検出し、それを `LLJITBuilder` に渡して作成する（jit_engine.cpp:54-71）。

```cpp
// jit_engine.cpp:57-64
auto jtmb = llvm::orc::JITTargetMachineBuilder::detectHost();
...
auto jitBuilder = llvm::orc::LLJITBuilder();
jitBuilder.setJITTargetMachineBuilder(std::move(*jtmb));
```

detectHostによりホストのSIMD機能（AVX等）がJITコード生成でも有効になり、AOTコンパイルとの性能差を抑える。

### ホストプロセスからのシンボル解決

ランタイム関数の解決は明示的なシンボル登録を一切行わず、`DynamicLibrarySearchGenerator::GetForCurrentProcess` をメインJITDylibへ追加することで実現する（jit_engine.cpp:83-97)。

```cpp
// jit_engine.cpp:88-91
auto generator = llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
    jit_->getDataLayout().getGlobalPrefix());
if (generator) {
    mainJD.addGenerator(std::move(*generator));
}
```

このジェネレータはlookupで未解決のシンボルをホストプロセス（cm実行ファイル自身とロード済み動的ライブラリ）から探すため、`printf`・`malloc` 等のlibcと `cm_print_*`・`cm_slice_*` 等のCmランタイム関数が自動解決される。前提として、コアランタイム `cm_runtime.o`（runtime.cが runtime_print.c / runtime_format.c / runtime_slice.c 等を単一翻訳単位に束ねたもの、src/internal/codegen/llvm/native/runtime.c:18-25）がcmバイナリ自体にリンクされている（CMakeLists.txt:605-624 でビルドし、CMakeLists.txt:659 の `target_link_libraries(cm PRIVATE ${CM_RUNTIME_OUTPUT})` でリンク）。

### execute: MIRから関数ポインタ実行まで

`execute()`（jit_engine.cpp:182-277）が変換から実行までを担う。

1. `TargetConfig::getNative()` でネイティブ設定の `LLVMContext`（モジュール名 `jit_module`）を作り、`MIRToLLVM::convert` でMIR全体をLLVM IRへ変換する（jit_engine.cpp:195-198）。
2. `llvm::verifyModule` で検証し、失敗時はIR全文ではなく検証エラーを `errorMessage` に載せて返す（jit_engine.cpp:210-214）。環境変数 `CM_DUMP_IR` で実行直前のIRをstderrへダンプでき、プラットフォーム差の調査に使う（jit_engine.cpp:206-209）。
3. `llvm::CloneModule` でモジュールを複製し、複製側を最適化・実行に使う（jit_engine.cpp:217）。元のモジュールは `LLVMContext` ラッパーが所有したまま残り、複製側の所有権だけを `ThreadSafeModule` として `addIRModule` へ移譲する（jit_engine.cpp:239-244）。
4. `optimizeModule` がLLVMバージョンで分岐する。LLVM 17以降は新PassBuilderで `buildPerModuleDefaultPipeline` をO1/O2/O3から選択して実行し、その際 `detectHost` 由来の `TargetMachine` を渡してCPU固有のベクトル化を有効化する（jit_engine.cpp:137-178）。LLVM 14-16はレガシー `PassManagerBuilder` を使う（jit_engine.cpp:106-135）。`optLevel <= 0` は無変換で返す（jit_engine.cpp:103-104）。
5. エントリポイントを `jit_->lookup(entryPoint)` で検索し、`int (*)()` 型の関数ポインタへ変換して呼び出す（jit_engine.cpp:247-267）。実行はtry/catchで包み、例外は `JITResult` のエラーへ変換する（jit_engine.cpp:266-274）。

### `cm run` と `#[test]` のディスパッチ

通常実行は `JITEngine` を1つ作り `execute(mir, "main", ...)` を呼ぶ（run.cpp:136-143)。テストモード（`cm test` または `--test`）は `#[test]` 属性を持つMIR関数を宣言順に収集し（run.cpp:79-90）、テスト関数ごとに独立した `JITEngine` インスタンスを生成して実行する。

```cpp
// run.cpp:114-117
for (const auto* fn : test_fns) {
    cm::codegen::jit::JITEngine jit;
    auto result =
        jit.execute(mir, fn->name, opts.optimization_level, sanitize_bounds_tests);
```

テストごとにLLJITとコンテキストを作り直すため、グローバル変数等の状態が前のテストから漏れない（状態隔離、run.cpp:75-77）。成功判定は「エントリ関数が正常リターンしたこと」で、assert失敗は `exit(1)` による即時停止として扱う。また `step()` はSVプラットフォーム専用のため、テスト本体のMIRを走査してcall終端を検出した場合は実行前に明示エラーにする（run.cpp:95-110）。JIT実行時はstdoutをアンバッファ化して出力の即時性を保証する（run.cpp:111, 139）。

### bounds sanitize（BoundsCheckingPass）

`--sanitize=bounds` 指定時は、最適化パイプラインの後に `llvm::BoundsCheckingPass` を関数パスアダプタ経由で追加実行する（jit_engine.cpp:222-237）。このパスは静的にサイズが分かるメモリアクセス（固定長配列等）へ境界チェックを挿入し、違反時は `llvm.trap` で即時停止するためランタイムライブラリが不要で、JITでもそのまま動作する。スライスアクセスはランタイム関数呼び出し越しでこのパスの対象外のため、MIRレベルの明示検査 `instrument_bounds_checks` が補完する（src/cmd/cm/build.cpp:652-658）。オプションは `opts.sanitizers` から `"bounds"` の有無で判定して `execute` へ渡す（run.cpp:112-113, 141-142）。

## 実装箇所

| ファイル | 役割 |
|---|---|
| src/internal/codegen/llvm/jit/jit_engine.hpp | `JITEngine`/`JITResult` の定義（LLJIT・ThreadSafeContext保持、コピー禁止） |
| src/internal/codegen/llvm/jit/jit_engine.cpp | 初期化・シンボル解決・最適化・execute本体 |
| src/cmd/cm/backend/run.cpp | `cm run`/`cm test` のディスパッチ、テストランナー、bounds指定の受け渡し |
| src/cmd/cm/build.cpp:660-662 | Runコマンドから `emit_jit_run` への振り分け |
| src/cmd/cm/options.cpp:92-95, 146-147 | `cm test`/`--test` による `test_mode` の設定 |
| src/internal/codegen/llvm/core/mir_to_llvm.hpp | MIR→LLVM IR変換器（JIT・AOT共通） |
| src/internal/codegen/llvm/native/runtime.c | JITシンボル解決の対象となるコアランタイム（cmバイナリへリンク） |
| CMakeLists.txt:605-624, 659 | `cm_runtime.o` のビルドとcmバイナリへのリンク |

## 落とし穴とケア

- **ランタイムシンボルの解決はcmバイナリ自身への依存**: JITは `GetForCurrentProcess` に全面依存するため、ランタイム関数を追加したのに `cm_runtime.o` のビルド対象（runtime.cのinclude連結、CMakeLists.txt:613-621のDEPENDS）へ入れ忘れると、コンパイルは通るのに `lookup` 時の未解決シンボルエラーとして初めて顕在化する。不変条件は「LLVM IRが宣言する `cm_*` 関数はすべてcmバイナリがエクスポートしていること」。
- **テストごとの独立JITインスタンスを崩さない**: テストループでJITEngineを共有すると、同名シンボルの二重追加エラーとグローバル状態のテスト間リークが起きる。1テスト=1エンジンの隔離（run.cpp:114-117）が防いでいるバグのクラスであり、性能目的での共有化はこの不変条件を壊す。
- **verifyModuleは最適化より前に行う**: 不正なIRを最適化パスに入れるとパス内部のクラッシュや誤コンパイルになり原因究明が難しい。検証を先に置くことでMIR→LLVM変換の欠陥をその場でエラーメッセージ化する（jit_engine.cpp:210-214）。調査には `CM_DUMP_IR=1` を使う。
- **エントリポイントのABIは固定**: 実行は `int (*)()` へのキャスト呼び出しであり（jit_engine.cpp:257-263）、エントリ関数がこのシグネチャから外れると未定義動作になる。MIR側でmain・テスト関数のシグネチャを保つことが前提。
- **JIT実行後のキャッシュ保存はしない**: JIT実行後に `codegen.compile` でネイティブ成果物を保存する案は、LLVMグローバル状態の再初期化問題とstdout汚染のため意図的に不採用である（run.cpp:150-151）。キャッシュ再利用は `cm compile` 側の成果物のみが対象。
- **回帰テストの場所**: テストランナーの挙動（成功・失敗・`step()` 拒否・SVプラットフォーム振り分け）は tests/cmtest/ の native_pass.cm / native_fail.cm / native_step_error.cm / sv_platform.cm を tests/test_cm_test.sh で検証する。JITバックエンド全体の機能回帰は `make test-jit`（tests/unified_test_runner.sh -b jit）、boundsの検査挙動は tests/sanitize/run_tests.sh（cases/oob 等）が担う。

## 関連資料

- [JITコンパイラ概要（初期設計）](../../archive/v0.11.0/jit/001_jit_compiler_overview.md)
- [JITランタイム関数一覧（初期設計）](../../archive/v0.11.0/jit/003_jit_runtime_functions.md)
- [境界チェック方針（bounds sanitizeの設計）](../../archive/v0.17.0/arrays-slices/bounds-checking-policy.md)
- [サニタイザ設計](../../archive/v0.16.2/06_sanitizer.md)
- [インクリメンタルビルド・並列コード生成（AOT側のビルド基盤）](../build/incremental-and-parallel-codegen.md)
