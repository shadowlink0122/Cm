[English](mir_optimization_survey.en.html)

# MIR最適化の調査メモ（共通最適化の前提）

## 目的
- MIRは全バックエンド（インタプリタ/LLVM/wasm/JS）共通のため、
  **安全で再現性のある最適化**をMIR段階で行う方針を整理する。
- 現状実装の挙動と制約を確認し、追加すべきMIR専用最適化を選定する。

## MIRの特徴（最適化観点）
実装: `src/mir/nodes.hpp`

- CFG + BasicBlock + Statement/Terminator 構造。
- Place/Operand/Rvalue で値とメモリアクセスを表現。
- ローカル変数は再代入されうる（純粋SSAではない）。
- `Projection` により Field/Index/Deref が表現される。

## 現在のMIR最適化パイプライン
実装: `src/mir/optimizations/all_passes.hpp`, `src/main.cpp`

共通固定レベル（現在2）で fixpoint まで実行:
1) SparseConditionalConstantPropagation (SCCP)
2) ConstantFolding
3) CopyPropagation
4) DeadCodeElimination
5) SimplifyControlFlow

## 各パスの挙動と制約

### SCCP（Sparse Conditional Constant Propagation）
実装: `src/mir/optimizations/sccp.hpp`

対象:
- CFGを考慮した定数伝播（Use/BinaryOp/UnaryOp）
- `SwitchInt` の分岐先を定数化し `Goto` に変換
- 同一遷移の `SwitchInt` を `Goto` に簡約
- 到達不能ブロックの削除

制約:
- `Aggregate/Cast/FormatConvert/Ref` は定数化対象外。
- メモリ経由の値やProjection付きのLoad/Storeは保守的にOverdefined。
- 参照/エイリアス解析は未実装。

### ConstantFolding
実装: `src/mir/optimizations/constant_folding.hpp`

対象:
- `Use` / `BinaryOp` / `UnaryOp` のみ
- `SwitchInt` の discriminant が定数なら `Goto` に変換

制約:
- ブロック単位の逐次走査（CFG非考慮）。
- `Cast` / `FormatConvert` / `Aggregate` / `Ref` は対象外。
- 複数回代入されるローカルは追跡しない。

### CopyPropagation
実装: `src/mir/optimizations/copy_propagation.hpp`

対象:
- `_x = _y` 形式の単純コピー
- Rvalue/Terminator 内のオペランド置換

制約:
- ブロック単位の逐次走査（CFG非考慮）。
- 複数回代入されるローカルは追跡しない。
- Projection 付きの store はベースのみ伝播。

### DeadCodeElimination
実装: `src/mir/optimizations/dead_code_elimination.hpp`

対象:
- 到達不能ブロックの削除
- 未使用ローカルへの代入削除（単純代入のみ）
- `StorageLive/Dead` のNop化

制約:
- 副作用判定は未実装（呼び出しは基本的に削除しない）。
- Projection 付きの store は対象外。

### SimplifyControlFlow
実装: `src/mir/optimizations/all_passes.hpp`

対象:
- 「空ブロック + 単一Goto」のみ統合

制約:
- if/switch の単純化や条件の縮約は未実装。

### ProgramDCE（compileのみ）
実装: `src/mir/optimizations/program_dce.hpp`

対象:
- 使用されない関数/構造体を削除
制約:
- 組み込み関数リストは固定
- 間接呼び出しは静的解析できないため保守的

## 調査結果（現状の課題）
1) **CFG非考慮の伝播の残存**
   - SCCPはCFG対応だが、ConstantFolding/CopyPropagationは依然ブロック順。
   - `Aggregate/Cast/FormatConvert` などの定数化は未対応。

2) **副作用/エイリアスの扱いが弱い**
   - DCEは呼び出しを削除しない前提で保守的。
   - Storeの削除範囲が狭く、Projectionや参照経由の最適化が難しい。

3) **MIR専用の強化余地**
   - Cast/FormatConvert/Ref/Aggregate への定数伝播が未対応。
   - CFG簡約（branch folding, switch simplification, 同一遷移統合など）が限定的。

## 共通最適化として追加/検討すべき候補（優先度順）

### A. CFG対応の定数伝播（SCCP）
目的: 支配関係を考慮した安全な定数伝播。  
効果: 条件分岐の削減、後続DCEの効果拡大。  
適用範囲: 全バックエンドで安全。  
状況: 実装済み（`sccp.hpp`）

### B. GVN/CSE（共通部分式削除）
目的: 同一式の再計算を削除。  
効果: MIRの冗長な一時変数を削減。  
適用範囲: 参照/エイリアス情報が限定的でも、
           「副作用なし」の式から始められる。

### C. Dead Store Elimination（DSE）
目的: 最終的に使われないstoreを削除。  
効果: メモリ使用量と実行回数の削減。  
注意: Projection/参照が絡む場合は保守的に。

### D. Cast/FormatConvert の定数畳み込み
目的: 定数変換をMIR段階で簡約。  
効果: 文字列フォーマットの簡略化、固定変換の削除。  
適用範囲: 全バックエンドで安全。

### E. CFG簡約の拡張
目的: if/switch が同じ行き先になる場合の縮約。  
効果: ブロック数削減、後続DCEの効果拡大。  
適用範囲: 全バックエンドで安全。
補足: `SwitchInt` の同一遷移はSCCPで簡約済み。

## 追加候補（中期）
- Loop-Invariant Code Motion（LICM）
- 簡易インライン化（小さな関数のみ）
- Bound Check Elimination（配列/スライス）
- SROA（Aggregateの分解）

## 実装の前提（共通最適化の安全性）
- Call は基本的に副作用ありとして扱う。
- 副作用がない既知の組み込み関数のみ例外的に扱う。
- MIRの定義/使用解析（def-use, dominator）を追加する必要がある。

## 次の検討ステップ（提案）
1) GVN/CSE（副作用なし式限定）
2) DSE（projectionなしのstore限定から拡張）
3) Cast/FormatConvertの定数畳み込み