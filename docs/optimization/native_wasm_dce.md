# HIR→MIRとnative/wasmの最適化調査（DCE中心）

## 目的
- HIR→MIRの流れと、native/wasm向けコード生成におけるデッドコード削除（DCE）が適用されているかを確認する。

## パイプラインの全体像（native/wasm）
- HIR生成: `src/hir/lowering/*`
- MIR生成: `src/mir/lowering/*`
- MIR最適化（compileのみ）:
  - CopyPropagation: `src/mir/optimizations/copy_propagation.hpp`
  - ConstantFolding: `src/mir/optimizations/constant_folding.hpp`
  - DeadCodeElimination（関数単位）: `src/mir/optimizations/dead_code_elimination.hpp`
  - ProgramDeadCodeElimination（プログラム単位 / compileのみ）:
    `src/mir/optimizations/program_dce.hpp`
- LLVM IR生成（native/wasm共通）: `src/codegen/llvm/core/mir_to_llvm.cpp`
- LLVM最適化（native/wasm共通）: `src/codegen/llvm/native/codegen.hpp`

## 実際にMIR最適化が走る条件
- `cm compile` のみ実行される（`cm run` / `cm check` は対象外）。
  - 実行箇所: `src/main.cpp`
- `cm compile` は `-O` 指定に従うが、最低でもレベル1が適用される。
  - `-O0` でも `mir_opt_level = 1` として最適化が実行される。
  - レベル2以上は fixpoint まで繰り返し適用。
- 実行形態:
  - `OptimizationPipeline` により標準パスを実行。
  - パス構成は `src/mir/optimizations/all_passes.hpp` に定義。

## 関数単位DCEの仕様
実装: `src/mir/optimizations/dead_code_elimination.hpp`

1) 到達不能ブロックの削除
- `BasicBlock::update_successors()` で後続を再構築し、BFSで到達可能ブロックを判定。

2) 未使用ローカルへの代入除去
- `assign.place.projections.empty()` の場合のみ対象。
- 右辺が「副作用なし」と判定された場合のみ削除。
  - 現状 `has_side_effects()` は常に false を返す（呼び出し等は終端命令にあるため）。
- `StorageLive/Dead` も未使用変数ならNop化。

3) Nop削除
- Nopになった文をリストから削除。

補足（捕捉変数）:
- クロージャローカルに関しては `LocalDecl::captured_locals` を参照して使用扱いにする。
  - これにより、キャプチャ変数がDCEで落ちるのを防止。

## プログラム単位DCEの仕様
実装: `src/mir/optimizations/program_dce.hpp`

- `main` と `_start` は常に使用扱い。
- 一部の組み込み関数は常に使用扱い（固定リスト）。
- 呼び出しグラフ探索:
  - `MirTerminator::Call` の呼び出し先を追跡。
  - `MirStatement::Assign` 内の `FunctionRef` 代入も追跡。
- インターフェースメソッド:
  - `Interface__method` が呼び出されている場合、`Type__method` 実装も保持。
- 未使用関数と未使用構造体を削除。

## LLVM最適化（native/wasm共通）
実装: `src/codegen/llvm/native/codegen.hpp`

- `optimizationLevel > 0` の場合にLLVMのデフォルトパイプラインを適用。
- `-O0` の場合はLLVM最適化をスキップ。
- `optimizationLevel` は `cm compile -O1/-O2/-O3` により設定される。

※ wasm も同じ `LLVMCodeGen` を通るため最適化パイプラインは共通。

## 現状の注意点（DCE観点）
1) MIR最適化は `-O` で強化されるが、最低レベルは常に有効
- `-O0` でも `CopyPropagation / ConstantFolding / DCE` は走る。

2) 関数単位DCEは単純なデータフローのみ
- 代入先がフィールド/インデックスの場合は削除対象外。
- 間接呼び出し（関数ポインタ経由）を完全には追跡できない。
- 別関数に引数として渡された関数参照は、呼び出し先の解析がないため保持されない可能性がある。

3) Program DCEの組み込み関数リストは固定
- ランタイム関数追加時はここに追記しないと削除対象になり得る。

## 確認コマンド（調査用）
```
# MIR（最適化前）
./cm compile <file.cm> --mir

# MIR（最適化後: CopyProp/ConstFold/DCE/ProgramDCE 後）
./cm compile <file.cm> --mir-opt

# LLVM最適化OFF
./cm compile -O0 <file.cm>
```
