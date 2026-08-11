# MIRの設計

MIR（Mid-level IR）はCmコンパイラの中核となる基本ブロックCFGベースの中間表現で、`src/internal/mir/` に定義・変換・解析・最適化のすべてが収まっている。
本書はMIRのデータ構造（`MirFunction`・locals・基本ブロック）、ディレクトリ構成、最適化パスの種類と実行順、O0/O1/O2の差、パス追加時の注意点を記述する。

## 概要

MIRは「式木を三番地コード風の文に平坦化し、制御フローを基本ブロックと終端命令（terminator）で明示化した」表現である。
HIRが持つ構文的な入れ子（if/while/match式）をすべてCFGに落とすことで、支配木・ループ解析にもとづく最適化をバックエンド非依存に一度だけ実装できる。
MIRはSSA形式ではなく、同一ローカルへの再代入を許す通常の命令列であり、各最適化パスは再代入を自前で追跡する。
サイズ・オフセット・アラインメントといったレイアウト情報はMIRに保持せず、LLVMのDataLayoutを唯一の情報源とする方針が明記されている（`src/internal/mir/nodes.hpp:481-483`）。

## データ構造とアルゴリズム

### 基本ID型とPlace

ブロック・ローカル・フィールドは `BlockId` / `LocalId` / `FieldId`（いずれも `uint32_t`、`src/internal/mir/nodes.hpp:42-44`）で参照し、エントリブロックは `ENTRY_BLOCK = 0`（`nodes.hpp:47`）である。
メモリ位置は `struct MirPlace`（`nodes.hpp:81-94`）で、「起点ローカル + 射影列（`PlaceProjection`: Field/Index/Deref、`nodes.hpp:55-79`）」としてフィールドアクセスや配列添字を表す。

### Operand・Rvalue・Statement・Terminator

値は `struct MirOperand`（`nodes.hpp:107-131`）で、`Kind { Move, Copy, Constant, FunctionRef }` と `std::variant<MirPlace, MirConstant, std::string>` を持つ（FunctionRefは関数名文字列）。
右辺値は `struct MirRvalue`（`nodes.hpp:181-249`）で、`Use / BinaryOp / UnaryOp / Ref / Aggregate / Cast / FormatConvert` の各データをvariantで保持する。
文は `struct MirStatement`（`nodes.hpp:254-318`）である。

```cpp
// src/internal/mir/nodes.hpp:255-265
enum Kind {
    Assign,       // 代入: place = rvalue
    StorageLive,  // 変数の有効範囲開始
    StorageDead,  // 変数の有効範囲終了
    Nop,          // 何もしない（最適化で削除される）
    Asm,          // インラインアセンブリ
};
Kind kind;
Span span;
bool no_opt = false;  // 最適化禁止フラグ（must{}ブロック内の文）
```

終端命令は `struct MirTerminator`（`nodes.hpp:323-378`）で、`Goto / SwitchInt / Return / Unreachable / Call` を持ち、`SwitchIntData` は判別値と `(値, 飛び先)` の対リスト + `otherwise` を保持する（`nodes.hpp:342-346`）。
関数呼び出しは文ではなく終端命令 `Call`（`CallData`: 呼び出し先・引数・書き込み先place・継続ブロック `success`、`nodes.hpp:348-365`）であり、CFG解析は `success` を後続ブロックとして扱う。

### 基本ブロック・locals・MirFunction

`struct BasicBlock`（`nodes.hpp:383-402`）は `statements` 列と単一の `terminator`、および `predecessors`/`successors` を持ち、terminatorの付け替えは `set_terminator`/`update_successors`（`nodes.hpp:398-401`）で行って辺情報の整合を保つ。
ローカル変数は `struct LocalDecl`（`nodes.hpp:407-430`）で、名前・型・可変性・ユーザ変数フラグに加え、クロージャ用のサイド情報 `is_closure` / `closure_func_name` / `captured_locals`（`nodes.hpp:417-419`）を持つ（詳細は [クロージャのlowering](../lowering/closures.md)）。
関数は `struct MirFunction`（`nodes.hpp:435-470`）で、引数も含む `std::vector<LocalDecl> locals`（`nodes.hpp:451`）、引数のIDリスト `arg_locals`（`nodes.hpp:452`）、戻り値ローカル `return_local`（`_0`、`nodes.hpp:453`）、`basic_blocks`（`nodes.hpp:454`）を持ち、`build_cfg()`（`nodes.hpp:469`）でpredecessorを再計算する。
プログラム全体は `struct MirProgram`（`nodes.hpp:639-665`）が関数・構造体・enum・インターフェース・vtable・グローバル変数などを集約する。

### ディレクトリ構成

- `mir/nodes.hpp`・`nodes.cpp` — 上記データ構造の定義。
- `mir/lowering/` — HIR→MIR変換（入口 `MirLowering::lower`: `src/internal/mir/lowering/lowering.cpp:19`）。`expr/`・`stmt/` の構文別lowering、`mono/` の単相化、`auto_impl/` のトレイト自動導出などに分割されている。
- `mir/analysis/` — CFG解析。支配木 `DominatorTree`（`src/internal/mir/analysis/dominators.cpp:42-192`、反復データフローによる古典的計算）と自然ループ検出 `LoopAnalysis`（`src/internal/mir/analysis/loop.cpp:21-82`、バックエッジ検出とネスト木構築）。利用者はループ系パス（`passes/loop/const_unroll.cpp`・`passes/loop/licm.cpp`）である。
- `mir/passes/` — 最適化・計装・検証パスの本体。`core/`（基底・パイプライン・パス生成）、`convergence/`（収束判定）、`cleanup/`（dce・dse・simplify_cfg・program_dce・string_reassign_free）、`scalar/`（folding・propagation・sccp）、`redundancy/`（gvn）、`loop/`（const_unroll・licm）、`interprocedural/`（inlining・tail_call_elimination）、`instrumentation/`（bounds・undefined）、`validation/`（no_std_checker）。
- `mir/optimizations/` — `optimization_pipeline.cpp` のみで、`create_standard_passes` へ委譲する薄いラッパである（`src/internal/mir/optimizations/optimization_pipeline.cpp:8-16`）。
- `mir/printer.cpp` — MIRテキストダンプ（`MirPrinter`: `src/internal/mir/printer.hpp:20-67`）。CLIの `--mir`（最適化前ダンプ、`src/cmd/cm/build.cpp:465-469`）と `--mir-opt`（最適化後ダンプして終了、`build.cpp:547-552`）から使われる。
- `mir/mir_splitter.cpp` — モジュール分割コンパイル用に `MirProgram` をモジュール別のゼロコピー参照ビュー `ModuleProgram` へ分割する（`src/internal/mir/mir_splitter.hpp:16-69`）。

### パスの基底クラスと実行機構

全パスは `class OptimizationPass`（`src/internal/mir/passes/core/base.hpp:23-57`）を継承し、`name()` と「変更があればtrueを返す」`run(MirFunction&)` を実装する（プログラム全体パスは `run_on_program` をオーバーライド）。
パス登録は中央レジストリではなく、`create_standard_passes`（`src/internal/mir/passes/core/manager.cpp:25-77`）がレベルに応じて `std::vector<std::unique_ptr<OptimizationPass>>` を組み立てる方式である。
実行は `run_optimization_passes`（`manager.cpp:79-128`）が `OptimizationPipeline::run_until_fixpoint`（`src/internal/mir/passes/core/base.cpp:59` 以降）を呼び、`ConvergenceManager`（`src/internal/mir/passes/convergence/manager.hpp:15-54`）が命令数・ブロック数などの重み付き変更量とプログラムハッシュ履歴で収束・循環を判定する。
1パスあたりの総実行回数上限（`max_pass_runs_total = 30`、`base.cpp:86`）と「前回変更なしのパスのスキップ」により、無限ループと無駄な再実行を防ぐ。

### 標準パスの種類と実行順

`create_standard_passes` の登録順がそのまま1反復内の実行順である（`src/internal/mir/passes/core/manager.cpp:29-74`）。

| 順 | パス | 位置づけ |
|---|---|---|
| 0 | `ConstantLoopUnroll`（`--funroll-loops` 指定時のみ、O0でも有効） | 定数トリップカウントループの静的展開（`manager.cpp:32-34`） |
| 1 | `StringReassignFree` | 文字列再代入時の旧バッファ解放（健全性、`manager.cpp:44`） |
| 2 | `SparseConditionalConstantPropagation` | 条件付き定数伝播（`manager.cpp:47`） |
| 3 | `ConstantFolding` | 定数畳み込み（`manager.cpp:48`） |
| 4 | `GVN` | 共通部分式除去（`manager.cpp:51`） |
| 5 | `CopyPropagation` | コピー伝播（`manager.cpp:52`、集約抑止フラグ付き） |
| 6 | `DeadStoreElimination` | 不要ストア除去（`manager.cpp:55`） |
| 7 | `SimplifyControlFlow` | CFG簡約（`manager.cpp:58`） |
| 8 | `FunctionInlining` | 関数インライン化（`manager.cpp:59`、後述の注意あり） |
| 9 | `TailCallElimination` | 末尾呼び出し最適化（`manager.cpp:61`） |
| 10 | `LoopInvariantCodeMotion` | ループ不変式移動（`manager.cpp:64`） |
| 11 | `DeadCodeElimination` | 不要コード削除（`manager.cpp:67`） |

### O0/O1/O2の差

- O0: `optimization_level == 0` なら標準パスは一切追加されず（`manager.cpp:37-39`）、明示指定された `ConstantLoopUnroll` だけが残る。ただし `StringReassignFree` はメモリ健全性パスとしてドライバがO0でも直接実行する（`src/cmd/cm/build.cpp:501-510`）。
- O1: 上記の標準パス列を1巡分登録し、`run_until_fixpoint` の最大反復回数は3である（`manager.cpp:93-98`）。
- O2: 標準パス列の末尾に `ConstantFolding` + `CopyPropagation` + `DeadCodeElimination` をもう1巡追加し（`manager.cpp:70-74`）、最大反復回数は5である（`manager.cpp:99-104`）。
- O3以上: パス構成はO2と同じで最大反復回数のみ7（超過レベルは実験的に100）へ増える（`manager.cpp:105-118`）。

このほか標準パイプライン外に、Compile時のみの関数DCE・プログラムDCE（`src/cmd/cm/build.cpp:528-544`）と、`--sanitize` 時のMIR計装（`instrumentation/`、`build.cpp:650-657`）がドライバから直接駆動される。

## 実装箇所

| 役割 | ファイル |
|---|---|
| MIRデータ構造 | `src/internal/mir/nodes.hpp`, `src/internal/mir/nodes.cpp` |
| HIR→MIR lowering入口 | `src/internal/mir/lowering/lowering.cpp`, `lowering.hpp` |
| パス基底・パイプライン | `src/internal/mir/passes/core/base.hpp`, `base.cpp` |
| 標準パス構成・レベル別実行 | `src/internal/mir/passes/core/manager.cpp`, `manager.hpp` |
| 収束判定 | `src/internal/mir/passes/convergence/manager.hpp`, `smart.hpp` |
| 支配木・ループ解析 | `src/internal/mir/analysis/dominators.cpp`, `loop.cpp` |
| 各最適化パス | `src/internal/mir/passes/{cleanup,scalar,redundancy,loop,interprocedural}/` |
| サニタイザ計装 | `src/internal/mir/passes/instrumentation/bounds.cpp`, `undefined.cpp` |
| MIRダンプ（--mir / --mir-opt） | `src/internal/mir/printer.cpp`, `src/cmd/cm/options.cpp:127-129` |
| モジュール分割 | `src/internal/mir/mir_splitter.cpp`, `mir_splitter.hpp` |

## 落とし穴とケア

- `no_opt` フラグの尊重が最重要の不変条件である: `must{}` ブロック由来の文は `MirStatement::no_opt = true`（`nodes.hpp:265`）で、全変換パスがこれをスキップする（例: `passes/scalar/propagation.cpp:109-110`、`passes/cleanup/dse.cpp:32-34`、`passes/scalar/folding.cpp:93-94`、`passes/cleanup/dce.cpp:115-116`、`passes/loop/licm.cpp:83-84`）。新パスを追加するときも必ず `no_opt` を検査し、インライン化のような文複製ではフラグを複製先へ引き継ぐこと（`passes/interprocedural/inlining.cpp:234`）。
- MIRはSSAではないため、ローカルへの再代入で解析結果を無効化する処理が各パスに必要である（GVNの値番号無効化など）。SSA前提のアルゴリズムをそのまま移植してはならない。
- インラインasmの出力オペランド変数は、`no_opt` の有無に関わらず定数・コピー追跡から除外する（`propagation.cpp:97`、`folding.cpp:81`）。asmは実行時に変数を書き換えるため、静的な値追跡が成り立たない。
- CFGを書き換えるパスは `set_terminator` / `update_successors` / `build_cfg`（`nodes.hpp:398-401`、`nodes.hpp:469`）でpredecessor/successorの整合を保つこと。`Call` 終端の後続は `success` ブロックである点を忘れると支配木・ループ解析が壊れる（`analysis/dominators.cpp:72-77`、`analysis/loop.cpp:45-49`）。
- パスの実行順には依存関係がある: `StringReassignFree` はloweringが生成した素のMIR形状（`T = concat(...) → X = copy(T)`）を前提に旧バッファ解放位置を分類するため、コピー伝播がこの形状を書き換える前、パイプライン先頭で実行しなければならない（`manager.cpp:41-44` のコメント）。この順序が、旧文字列バッファのリークや二重解放というバグのクラスを防いでいる。
- `FunctionInlining` は登録されているが、loweringが呼び出し先を `FunctionRef` で発行するのに対して旧形式の文字列定数を期待するため実運用では作動せず、最終的なインライン展開はLLVM側インライナが担う（固定化テスト: `tests/regression/cases/mir_optimization/README.md` と `FunctionInlining_CurrentlyDormant`）。この前提を変える場合は `__lambda_` やクロージャのインライン化禁止判定（`inlining.cpp:118-121`）を維持すること。
- 共有コードの境界: js/ts/webターゲットは `MirOptimizationOptions::no_aggregate_copy_prop`（`manager.hpp:14-20`、設定は `src/cmd/cm/build.cpp:491-493`）で集約コピー伝播を抑止し、svターゲットは文除去系パスを実行せず保存モードの `ConstantFolding(fold_terminators=false)` のみ通す（`build.cpp:512-521`）。MIRパスを変更するときは、これらの境界フラグの意味論（jsの深いコピー・svのハードウェアロジック保持）を壊さないこと。
- パス追加の手順: (1) `passes/` 配下の適切なサブディレクトリに `OptimizationPass` 派生を実装し、(2) `create_standard_passes`（`manager.cpp:25`）の適切な位置へ登録し、(3) 制御フラグが必要なら `MirOptimizationOptions`（`manager.hpp:14-20`）へフィールドを追加する。
- 回帰テスト: 手組みMIRでのパス単体検証は `tests/unit/mir_pass_test.cpp`、パイプライン通過の回帰は `tests/regression/mir_optimization_test.cpp`（ケースは `tests/regression/cases/mir_optimization/`）と `tests/regression/mir_lowering_test.cpp`（ケースは `tests/regression/cases/mir_lowering/`）にある。パス別カバレッジの一覧は `tests/regression/cases/mir_optimization/README.md` が単一情報源である。

## 関連資料

- [コンパイルパイプライン全体像](overview.md)
- [クロージャのlowering](../lowering/closures.md)
- [データ付きenumとmatchのlowering](../lowering/enums-and-match.md)
