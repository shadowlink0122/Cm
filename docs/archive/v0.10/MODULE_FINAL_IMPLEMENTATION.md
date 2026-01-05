# モジュールシステム完全実装（最終フェーズ）

## 目標
HIR/MIR Loweringを実装し、モジュールシステムを完全に動作させる

## 現状
- ✅ パース層: 完了（namespace構文をパース可能）
- ❌ HIR層: 未実装（namespaceを処理できない）
- ❌ MIR層: 未実装（修飾名を解決できない）

## 実装戦略
1. HIR Loweringで namespace 宣言を処理
2. 名前空間スコープの管理
3. 修飾名（math::add）の解決
4. MIR Loweringでの統合

## 開始
