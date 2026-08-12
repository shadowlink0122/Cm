# LLVMレベルの最適化構成

native/jitバックエンドのLLVM IR最適化は、いずれも新PassManager（PassBuilder）の `buildPerModuleDefaultPipeline` で標準パイプラインを構築し、Cmの最適化レベル（-O0〜-O3）をLLVMの `OptimizationLevel` へ写像して実行する。パイプライン構築箇所はnative全体経路・native分割経路・JITの3箇所あり、nativeはO2以上でMergeFunctions（ICF）を明示的に有効化し、サニタイザ計装パスを最適化後に別パイプラインとして追加する。最適化暴走への防御はRecursionLimiterの属性前置・PassDebuggerの個別パス計測・生成前バリデーションの多層で構成し、パターン検出による最適化レベルの暗黙ダウングレードは行わない方針を採る。

## 概要

PassBuilderによるパイプライン構築は3箇所にある。native全体経路は `LLVMCodeGen::optimize()`（`src/internal/codegen/llvm/native/codegen.cpp:759-922`）、native分割経路（`CM_MODULE_CODEGEN=1`）はワーカ内の最適化ブロック（`codegen.cpp:380-409`）、JITは `JITEngine::optimizeModule()`（`src/internal/codegen/llvm/jit/jit_engine.cpp:102-180`）である。これらとは別に、サニタイザ計装専用のパイプラインをnativeは `instrumentSanitizers()`（`codegen.cpp:945-1002`）、JITはbounds専用ブロック（`jit_engine.cpp:224-238`）として最適化後に実行する。

native全体経路のコンパイル手順は `LLVMCodeGen::compile()`（`codegen.cpp:35-105`）に一本道で並ぶ: MIRパターン検出→初期化（TargetMachine生成）→IR変換→verifyModule→`CM_DUMP_IR=1` ダンプ→IRパターン検出→optimize()→`CM_DUMP_IR=2` ダンプ→サニタイザ計装→emit。IR最適化（PassBuilder）とコード生成最適化（TargetMachineの `CodeGenOptLevel`）は同じ `options.optimizationLevel` から導出され、前者はIR変形、後者はISel・レジスタ割り付け・スケジューリングの積極度を決める。オブジェクト出力そのもの（fork隔離・タイムアウト）は[オブジェクトファイル出力](object-emission.md)、JITの実行系全体は[LLJITエンジン](../codegen-jit/lljit-engine.md)、分割経路のキャッシュ・並列化は[インクリメンタルビルドと並列コード生成](../build/incremental-and-parallel-codegen.md)を参照。

## データ構造とアルゴリズム

### PassBuilderパイプライン構築（native全体経路）

`optimize()` はTargetMachineと `PipelineTuningOptions` を渡してPassBuilderを構築し、4種の解析マネージャを登録して標準パイプラインを実行する。TargetMachineを渡すことでベクトル化のコストモデルがターゲットCPUの機能（SIMD幅等）を反映する。

```cpp
// codegen.cpp:839-842 — O2以上でMergeFunctions（ICF）を有効化
llvm::TargetMachine* TM = targetManager ? targetManager->getTargetMachine() : nullptr;
llvm::PipelineTuningOptions pipelineTuning;
pipelineTuning.MergeFunctions = (options.optimizationLevel >= 2);
llvm::PassBuilder passBuilder(TM, pipelineTuning);
```

解析マネージャは `LoopAnalysisManager`・`FunctionAnalysisManager`・`CGSCCAnalysisManager`・`ModuleAnalysisManager` を `registerXXXAnalyses` で登録し `crossRegisterProxies` で相互接続する（`codegen.cpp:845-854`）。この5点セット（PB構築→4マネージャ登録→crossRegisterProxies→buildPerModuleDefaultPipeline→run）は3箇所のパイプライン構築すべてで同型である。

レベル写像は `1→O1`・`2→O2`・`3→O3`・`-1→Oz`（範囲外は `O2`、`codegen.cpp:857-873`）。`-1` はCLIからは指定できず、wasm・ベアメタルARMの `TargetConfig` 既定値（`src/internal/codegen/llvm/core/context.hpp:47,88`）としてのみ現れるサイズ最適化指定である。さらにパイプライン構築直前にターゲット別の上書きがあり、Wasmは `Oz`、Baremetalは `Os` を強制する（`codegen.cpp:903-911`）。BaremetalUEFIは `optimize()` 冒頭で即return（`codegen.cpp:762-764`）するため、そもそもIR最適化を一切実行しない（O2のインライン展開+DCEが `efi_main` のcall/retを削除しフォールスルークラッシュを起こすため）。

MergeFunctions（ICF）は、単相化が生成するレイアウト同一の特殊化（`pick__int`/`pick__uint` 等）のIR本体が構造的に完全同一な場合のみ正準実装へのサンクに折り畳む。名前ベースのフロント側エイリアス化は符号性が比較・除算・拡張の意味論を変えるため不採用で、IR構造比較のみで折り畳む（`codegen.cpp:834-838`）。分割経路のワーカ内でも同じ条件で有効化しており（`codegen.cpp:386-387`）、両経路の成果物同一性の前提になっている（詳細は[インクリメンタルビルドと並列コード生成](../build/incremental-and-parallel-codegen.md)）。

`options.useCustomOptimizations`（既定false、`codegen.hpp:50`）を立てると自作の `OptimizationManager`（Peephole/InstCombine/Vectorize/LoopUnroll、`src/internal/codegen/llvm/optimizations/optimization_manager.hpp`）を標準パイプラインの前に追加実行できるが、どのドライバもこのフラグを設定しておらず、本番経路は常に標準PassBuilderパイプラインのみである。同種の休眠実装として `TBAAManager`（`src/internal/codegen/llvm/core/tbaa.hpp`）があり、型ベースエイリアス解析（TBAA）メタデータの生成器を定義するが、どの変換・最適化経路からも参照されていない。したがって生成IRにTBAAメタデータは付与されず、TBAA前提のベクトル化促進は現状効いていない。

### OptimizationLevelとCodeGenOptLevelの対応

CLIの `-O<n>` は0〜3のみ受理され（`src/cmd/cm/options.cpp:201-208`）、`initialize()` で `TargetConfig::optLevel` へ転写される（UEFIのみ0へ強制、`codegen.cpp:704-710`）。`TargetManager::initialize()` はこれをTargetMachine生成時の `CodeGenOptLevel` へ写像する（`src/internal/codegen/llvm/native/target.cpp:94-146`）。

| Cmレベル | IR最適化（PassBuilder） | コード生成（CodeGenOptLevel） |
|---|---|---|
| 0 | パイプライン実行なし | None |
| 1 | O1 | Less |
| 2 | O2 | Default |
| 3 | O3 | Aggressive |
| -1（wasm/ベアメタル既定） | Oz（nativeターゲット別上書きでOz/Os） | Default |

IR側とコード生成側は独立に効くため、UEFIのようにIR最適化だけを止めてもISelは `optLevel=0` 指定（None）で別途弱める必要がある点に注意（`codegen.cpp:704-710` が両方を制御する理由）。

### サニタイザ計装パス（明示追加パス）

`--sanitize` 指定時は `instrumentSanitizers()` が最適化後に専用のPassBuilder+ModulePassManagerを組み、以下のパスを明示追加する（`codegen.cpp:987-1001`）。

```cpp
// codegen.cpp:988-1000
if (options.sanitizeBounds) {
    MPM.addPass(llvm::createModuleToFunctionPassAdaptor(llvm::BoundsCheckingPass()));
}
if (options.sanitizeAddress) {
    MPM.addPass(llvm::AddressSanitizerPass(llvm::AddressSanitizerOptions()));
}
if (options.sanitizeThread) {
    MPM.addPass(llvm::ModuleThreadSanitizerPass());
    MPM.addPass(llvm::createModuleToFunctionPassAdaptor(llvm::ThreadSanitizerPass()));
}
if (options.sanitizeMemory) {
    MPM.addPass(llvm::MemorySanitizerPass(llvm::MemorySanitizerOptions()));
}
```

ASan/TSan/MSanは計装パス実行前に本体を持つ各関数へ `SanitizeAddress` 等の属性を付与する（宣言のみの関数と、prologue/epilogueが無くredzone操作が壊れるNaked関数は除外、`codegen.cpp:958-974`）。BoundsCheckingPassは静的にサイズが分かるアクセスへ検査を挿入し違反時 `llvm.trap` で停止するランタイム不要方式で、スライス側はMIR計装が補完する（[境界検査](../slices/bounds-checking.md)）。計装を最適化後に置くのは、最適化前に挿入した検査が変形で重複・冗長化するのを避けるためで、`optimize()` と独立しているためO0でも計装される（`codegen.cpp:98-99`）。

### 複雑度ガードと最適化暴走への防御

防御は「パイプライン実行前の属性前置」「実行中の切り分け」「実行後（生成前）の検証」の3層で、いずれもユーザー指定の最適化レベル自体は変えないことを原則とする。

- **RecursionLimiter（前置）**: `optimize()` 冒頭の `RecursionLimiter::preprocessModule`（`codegen.cpp:771`、実装 `src/internal/codegen/llvm/optimizations/recursion_limiter.hpp`）が呼び出しグラフのDFSで再帰サイクルを検出し、再帰関数へ `NoInline`+`OptimizeNone` を付与して最適化パイプラインの対象から外す。再帰のインライン展開が展開停止条件を見失って暴走するクラスの防御である。さらに名前パターン（`closure`/`iter`/`lambda`/`$_`）に合致する関数は呼び出し数・ブロック数に応じて `NoInline` を付与し、レベル別のインライン閾値（O3はO2より厳しい命令数上限）を関数サイズ判定で適用する（`recursion_limiter.hpp:153-163`）。
- **PassDebugger（実行中の切り分け）**: `--verbose` かつO2以上のとき、問題を起こしやすいInstCombine・SimplifyCFG・GVNを個別スレッドでタイムアウト付き実行して所要時間を計測し、タイムアウトした場合は本パイプラインをO1へフォールバックする（`codegen.cpp:876-897`、実装 `src/internal/codegen/llvm/native/pass_debugger.hpp:39-197`）。verbose限定の調査導線であり、通常経路の挙動には影響しない。
- **生成前バリデーション（実行後）**: 最適化済みモジュールはオブジェクト出力直前に `PreCodeGenValidator::validate`（`src/internal/codegen/llvm/native/loop_detector.cpp:166-199`）を通る。無条件分岐で自分自身へ戻る単一ブロック自己ループを「明白な無限ループ」として検出し、巨大関数数の上限も検査する。正当な無限ループ（halt/hang）が正常であるベアメタル/UEFI tripleでは検証自体をスキップする（`safe_codegen.hpp:276-284`）。検証を通っても真に暴走する場合はfork隔離のタイムアウト（`CM_CODEGEN_TIMEOUT`）が最終防衛線になる（[オブジェクトファイル出力](object-emission.md)）。

誤検知対策の要点は複雑度スコアの算出方法にある。ループ深度による乗算は撤去され、静的な命令数ベースの評価に一本化されている。

```cpp
// loop_detector.cpp:57-61 — 誤検知対策のコメント（要旨）
// 注: 以前はループ深度で複雑度を乗算していたが、ループの実行回数はコード生成のコストとは無関係
// （コード生成コストは静的な命令数にほぼ比例する）。この乗算は多数の関数を持つ正当なコード
// （ジェネリック展開等）で複雑度を過大評価し誤検出していたため撤去。
// 真の暴走はコード生成のタイムアウトで捕捉する。
```

#### コード生成中の動的監視（CompilationGuard）

上記3層が守る最適化パイプラインより前段の、MIR→LLVM変換そのものにも変換ループの暴走を検出する動的モニタ群がある。`CompilationGuard`（`src/internal/codegen/llvm/monitoring/compilation_guard.hpp:13-69`）は `CodeGenMonitor`・`BlockMonitor`・`OutputMonitor` を束ねるスレッドローカルの統合ガード（`compilation_guard.cpp:158-164`）で、RAIIの `ScopedFunctionGuard`/`ScopedBlockGuard`（`compilation_guard.hpp:75-100`）として変換コードへ組み込まれている。

- **組み込み位置**: 関数変換 `MIRToLLVM::convertFunction` の冒頭が関数名とブロック数から求めたハッシュで `ScopedFunctionGuard` を張り（`src/internal/codegen/llvm/core/translate/function.cpp:31-39`）、各ブロックの変換ループを `try` で包んで検出例外を `handle_infinite_loop_error`（統計印字と `-O0`/`--debug` を案内するヒント表示、`compilation_guard.cpp:114-126`）へ通してから再送出する（`function.cpp:700-723`）。ブロック変換 `convertBasicBlock` は `ScopedBlockGuard` で訪問を記録し（`function.cpp:751-755`）、文・ターミネータごとに種別と代入先ローカル・呼び出し先関数名を織り込んだ命令記述を `add_instruction` へ流す（`function.cpp:806, 849`）。
- **CodeGenMonitor（同一関数の過剰再生成）**: 関数ごとの生成回数が上限を超えると例外を送出し（`monitoring/codegen_monitor.cpp:17-21`）、コードハッシュの履歴に周期2〜5の繰り返しパターンが規定回数現れた場合も無限ループとして検出する（`codegen_monitor.cpp:41-74`）。
- **BlockMonitor（同一ブロックの過剰訪問）**: ブロックごとの訪問回数・命令数の上限検査と、直前命令とのハッシュ比較による同一命令の連続生成検査を行う（`monitoring/block_monitor.cpp:21-25, 42-60`）。かつての周期的命令パターン検出はループ展開や手書きの反復コードなど正当な繰り返しを誤検出するため撤去され、命令数上限が暴走対策を担う旨がコメントに明記されている（`block_monitor.cpp:72-73`）。
- **OutputMonitor（出力サイズ異常）**: ファイル別・全体の出力バイト数上限と書き込み速度の異常を検査する（`monitoring/output_monitor.cpp:26-70`）が、記録フック（`ScopedOutputGuard`/`write_output`）を呼ぶ出力経路が現在はなく、実効的な出力サイズ防御はfork隔離側の書きかけファイル監視が担う（[オブジェクトファイル出力](object-emission.md)）。

閾値はドライバがCLIの `--max-output-size` から `configure` で設定し、`--debug` 時は進入ログと統計収集を有効化する（`src/cmd/cm/backend/llvm.cpp:139-147`、既定値は `compilation_guard.hpp:47-49`）。全体では、このCompilationGuardによる「生成中の動的監視」→ `PreCodeGenValidator` による「生成後の静的検証」→ fork隔離の `CM_CODEGEN_TIMEOUT` による「最終防衛」という補完関係になっており、変換ループ自体の暴走は例外で即停止し、変換が正常終了しても危険なIR形状は生成前に拒否し、それでもすり抜けたLLVM内部の暴走はSIGKILLで計算資源ごと回収する（[オブジェクトファイル出力](object-emission.md)）。

### 最適化レベルの自動調整は「情報提供のみ」

`compile()` はMIR段の `MIRPatternDetector::adjustOptimizationLevel`（`codegen.cpp:46-54`）とIR段の `OptimizationPassLimiter::adjustOptimizationLevel`（`codegen.cpp:74-86`、`optimize()` 内 `codegen.cpp:773-788`）を呼ぶが、両者とも現在は複雑度スコア（クロージャ・イテレータ・ラムダの個数、最大ブロック数）を集計して高複雑度の警告をstderrへ出すだけで、常に要求レベルをそのまま返す（`src/internal/codegen/llvm/optimizations/pass_limiter.hpp:25-86`・`mir_pattern_detector.hpp:13-71`）。「暗黙的なダウングレードは行わない」がCmのポリシーとしてコメントに明記されており、`compile()` 側の調整結果受け取り・O0時スキップの分岐は将来の再有効化に備えた配管として残っている。関数数・命令数の閾値検査 `SafeCodeGenerator::checkComplexity`（`safe_codegen.hpp:373-400`）も同様に警告のみで続行する。つまり成果物へ影響する自動レベル変更は、UEFIの無条件O0化とverbose時PassDebuggerのO1フォールバックの2つだけである。

### JIT側のパイプライン（optimizeModule）

`JITEngine::optimizeModule()` はLLVMバージョンで分岐する。LLVM 17以降は `JITTargetMachineBuilder::detectHost()` で作ったホストCPU向けTargetMachineをPassBuilderへ渡し、`1→O1`・`2→O2`・それ以外（3含む）→`O3` の写像で `buildPerModuleDefaultPipeline` を実行する（`jit_engine.cpp:136-179`）。LLVM 14〜16はレガシー `PassManagerBuilder` で関数パス・モジュールパスを構成し、O2以上でインライナを設定する互換経路を持つ（`jit_engine.cpp:106-135`）。`optLevel <= 0` は無変換で返す（`jit_engine.cpp:103-104`）。

JITはnativeと異なり、`PipelineTuningOptions` を渡さないためMergeFunctionsは有効化されず、`Oz`/`Os` やターゲット別上書きも持たない。最適化対象は `CloneModule` した複製であり、`--sanitize=bounds` 時は最適化後に別のPassBuilderで `BoundsCheckingPass` だけを追加実行する（`jit_engine.cpp:217-238`）。ここでも「検査挿入は最適化パイプラインの後」という順序不変条件はnativeと共通である。

### verifyModuleの位置づけ

両バックエンドとも `llvm::verifyModule` を最適化パイプラインより前に置き、MIR→LLVM変換の欠陥を最適化パス内部のクラッシュ・誤コンパイルへ化けさせずその場でエラー化する。失敗時の扱いは経路ごとに異なる。

- native全体経路: `LLVMCodeGen::verifyModule()`（`codegen.cpp:745-756`）が失敗時にIR全文をstderrへ印字してから `std::runtime_error` を送出する。`options.verifyIR` はドライバが常にtrueを設定する（`src/cmd/cm/backend/llvm.cpp` のオプション転写）。
- native分割経路: ワーカ内でモジュール名付きのエラーメッセージとして送出し、`exception_ptr` 経由でjoin後に再送出される（`codegen.cpp:367-374`）。
- JIT: 例外ではなく `JITResult.errorMessage` に検証エラーを載せて返す（`jit_engine.cpp:211-215`）。

### CM_DUMP_IRデバッグダンプ経路

環境変数 `CM_DUMP_IR` はIRをstderrへダンプする調査導線で、経路ごとに意味が異なる。

- native全体経路: `CM_DUMP_IR=1` で最適化前（verify直後）、`CM_DUMP_IR=2` で最適化後のIRをダンプする（`codegen.cpp:67-72, 91-96`、実体は `LLVMContext::dumpIRToStderr`）。
- native分割経路: `1`/`2` いずれでも `;; ===== module: <名前> =====` ヘッダ付きでモジュール別に最適化後IRをダンプする（`codegen.cpp:411-417`）。
- JIT: 値を問わず設定されていれば実行直前（verify前・最適化前）のIRをダンプする（`jit_engine.cpp:207-210`）。

このほかCLIの `--lir-opt` が最適化後IRをstdoutへ表示して終了し（`llvm.cpp:200-206`）、`--verbose` は生成直後と最適化後のIRを両方stderrへ印字する（`codegen.cpp:737-741, 915-919`）。ターゲット構成の調査は `CM_DUMP_TARGET=1`（[オブジェクトファイル出力](object-emission.md)）。

### native/jitの最適化構成対比

| 項目 | native（compile） | jit（cm run / cm test） |
|---|---|---|
| パイプライン構築箇所 | `LLVMCodeGen::optimize()`（分割経路はワーカ内） | `JITEngine::optimizeModule()` |
| PassBuilderへのTargetMachine | `TargetManager` のTargetMachine（クロス構成対応） | `detectHost()` によるホストCPU向け |
| レベル写像 | 1/2/3→O1/O2/O3、-1→Oz、ターゲット別にOz/Os上書き | 1/2/3→O1/O2/O3のみ（0以下は無変換） |
| MergeFunctions（ICF） | O2以上で有効（全体・分割両経路） | なし |
| レガシーPassManager互換 | なし（新PassBuilderのみ） | LLVM 17未満でPassManagerBuilder経路 |
| 最適化対象 | 変換したモジュールそのもの | `CloneModule` した複製 |
| 複雑度ガード | RecursionLimiter・PassDebugger・生成前バリデーション | なし（プロセス内実行のため暴走はユーザー操作で停止） |
| verify失敗時 | IR全文印字+例外 | `JITResult` のエラーとして返却 |
| bounds計装 | `instrumentSanitizers()`（ASan/TSan/MSanも同居） | 最適化後の専用ブロック（boundsのみ） |
| UEFI/ベアメタル特例 | UEFIはIR最適化・CodeGen最適化とも無効化 | 対象外（nativeターゲットのみ） |

## 実装箇所

| ファイル | 役割 |
|---|---|
| `src/internal/codegen/llvm/native/codegen.cpp:759-922` | native全体経路の `optimize()`（PassBuilder構築・レベル写像・ICF・PassDebugger連携） |
| `src/internal/codegen/llvm/native/codegen.cpp:380-409` | 分割経路ワーカ内のパイプライン構築（ICF条件を全体経路と同期） |
| `src/internal/codegen/llvm/native/codegen.cpp:945-1002` | `instrumentSanitizers()`（属性付与+ASan/TSan/MSan/BoundsCheckingの明示追加） |
| `src/internal/codegen/llvm/native/codegen.cpp:745-756` | native側verifyModule（失敗時IR印字+例外） |
| `src/internal/codegen/llvm/jit/jit_engine.cpp:102-180` | JIT側 `optimizeModule()`（LLVMバージョン分岐・detectHost TargetMachine） |
| `src/internal/codegen/llvm/jit/jit_engine.cpp:205-238` | JIT側verify・CM_DUMP_IR・bounds計装 |
| `src/internal/codegen/llvm/native/target.cpp:94-146` | Cmレベル→`CodeGenOptLevel` 写像とTargetMachine生成 |
| `src/internal/codegen/llvm/optimizations/recursion_limiter.hpp` | 再帰検出とNoInline/OptimizeNone前置・インライン閾値 |
| `src/internal/codegen/llvm/optimizations/pass_limiter.hpp` | IR複雑度の情報収集（暗黙ダウングレードなし） |
| `src/internal/codegen/llvm/optimizations/mir_pattern_detector.hpp` | MIR複雑度の情報収集（暗黙ダウングレードなし） |
| `src/internal/codegen/llvm/optimizations/optimization_manager.hpp` | 自作最適化パス群（`useCustomOptimizations` 限定の休眠経路） |
| `src/internal/codegen/llvm/native/pass_debugger.hpp` | verbose時の個別パス計測とタイムアウト検出 |
| `src/internal/codegen/llvm/native/loop_detector.cpp` | 生成前バリデーション（自己ループ検出・命令数ベース複雑度） |
| `src/internal/codegen/llvm/monitoring/`（compilation_guard/block_monitor/codegen_monitor/output_monitor） | CompilationGuard一式（MIR→LLVM変換中の過剰訪問・過剰再生成・出力サイズの動的監視） |

## 落とし穴とケア

- **ICF条件は2箇所同期**: `MergeFunctions = (optimizationLevel >= 2)` は全体経路（`codegen.cpp:841`）と分割経路（`codegen.cpp:387`）の両方にあり、片方だけ変更すると `CM_MODULE_CODEGEN` の有無で成果物のサイズ・シンボル構成が乖離する。ICFの正当性は tests/common/generics/layout_equal_specialization_test.cm が全バックエンドで検証する。
- **UEFI分岐の到達不能コード**: `optimize()` はUEFIで冒頭returnするため、パイプライン選択部のUEFI向けO2分岐（`codegen.cpp:907-908`）には到達しない。UEFIの最適化を再有効化する際は冒頭return・`initialize()` の `optLevel=0` 強制・分割経路の `isUefiModule` スキップ（`codegen.cpp:377-379`）の3箇所を併せて見直すこと。
- **計装は必ず最適化の後**: サニタイザ計装を `optimize()` より前へ移すと、挿入した検査が最適化で重複・削除され計装漏れや冗長化を起こす。またO0時に `optimize()` が早期returnしても計装される現在の分離構造（`codegen.cpp:98-99`）を壊さないこと。
- **verifyは最適化の前**: 不正IRを最適化パスへ入れるとパス内部のクラッシュとして現れ原因究明が難しくなる。JIT側では例外でなく `JITResult` で返す境界（`jit_engine.cpp:211-215`）も維持する。
- **暗黙ダウングレード禁止のポリシー**: `adjustOptimizationLevel` 系を「調整あり」へ戻すと、ユーザー指定の `-O` と実際の成果物が黙って乖離する。ポリシーコメント（`pass_limiter.hpp:25-26`・`mir_pattern_detector.hpp:13-14`）を変更理由なしに覆さないこと。成果物へ影響してよい自動変更はUEFIのO0化とverbose時のO1フォールバックのみという現状を保つ。
- **RecursionLimiterは名前パターン依存**: クロージャ・イテレータの特別処理は関数名の部分一致（`closure`/`iter`/`lambda`/`$_`）で判定するため、[ラムダの命名規約](../lowering/closures.md)を変更するとインライン抑制の対象が黙って変わる。また再帰関数への `OptimizeNone` は当該関数の最適化を丸ごと止めるため、再帰ホットループの性能問題を調査する際はまずこの属性付与を疑う。
- **生成前バリデーションの自己ループ検出**: 無条件自己分岐の検出（`loop_detector.cpp:65-79`）は最適化でブロックが畳まれた後のIR形状に反応するため、O0では通るコードがO2で拒否されることがある（エラーメッセージが `-O1`/`-O0` を案内する理由）。正当な無限ループを持つベアメタル/UEFIはtriple判定でスキップされる不変条件（`safe_codegen.hpp:276-284`）を崩さないこと。
- **PassDebuggerのスレッドdetach**: タイムアウト時のdetach（`pass_debugger.hpp:172-173`）は破棄済みスタック参照のリスクを持つ意図的なデバッグ専用妥協であり、verbose経路以外へ持ち出さないこと。本番の暴走対策はfork隔離+SIGKILL（[オブジェクトファイル出力](object-emission.md)）が担う。

## 関連資料

- [オブジェクトファイル出力](object-emission.md) — TargetMachine構成・fork隔離タイムアウト・生成前バリデーションの呼び出し側
- [LLJITエンジン](../codegen-jit/lljit-engine.md) — JIT実行系の全体像とシンボル解決
- [インクリメンタルビルドと並列コード生成](../build/incremental-and-parallel-codegen.md) — 分割経路のキャッシュキー・ワーカ並列とICFの動機
- [MIRの設計](../pipeline/mir-design.md) — LLVMより前段のMIR最適化パス構成
- [境界検査](../slices/bounds-checking.md) — BoundsCheckingPassとMIR計装の補完関係
