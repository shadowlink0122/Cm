# Cm言語 診断システムドキュメント一覧

**バージョン:** v0.12.0
**最終更新:** 2026-01-13

## 📚 ドキュメント構成

### 1. [079_unified_diagnostic_system.md](./079_unified_diagnostic_system.md)
**メイン設計ドキュメント**
- 統一診断システムのアーキテクチャ
- **型チェックとLintの完全統合**
- 共有コンポーネント（Lexer/Parser/セマンティック解析）
- 約46%の性能向上を実現

### 2. [080_diagnostic_catalog.md](./080_diagnostic_catalog.md)
**診断カタログ**
- E/W/L番号体系の定義
- 全診断ルールの一覧
- **重要設定:**
  - L001: インデント4スペース
  - L100: 関数名snake_case
  - L102: 定数名UPPER_SNAKE_CASE

### 3. [081_implementation_guide.md](./081_implementation_guide.md)
**実装ガイド**
- 統合アーキテクチャの詳細
- 型チェックとLintの統合実装例
- 共有データ構造（シンボルテーブル、CFG）
- インクリメンタル解析の実装

### 4. [078_linter_formatter_design.md](./078_linter_formatter_design.md)
**初期設計（参考）**
- Linter/Formatterの個別設計
- Trivia保持の仕組み
- ※最新設計は079を参照

## 🎯 核心ポイント

### 統合の最大の利点
```
従来: Lexer×2 + Parser×2 + 個別解析 = 130ms
統合: Lexer×1 + Parser×1 + 統合解析 = 70ms
→ 46%高速化
```

### 1回の解析で全診断
```cpp
// 型チェックとLintを同時実行
void analyze_variable(VarDecl* var) {
    check_type(var);        // E100: 型エラー
    check_naming(var);      // L101: 命名規則
    check_const(var);       // L250: const推奨
    track_usage(var);       // W001: 未使用
}
```

## ✅ 実装チェックリスト

- [ ] DiagnosticCatalogクラス
- [ ] UnifiedSemanticAnalyzer
- [ ] Trivia保持Lexer/Parser
- [ ] 自動修正機能
- [ ] VSCode/LSP統合

## 📊 診断数サマリー

- **エラー (E)**: 400番台まで定義
- **警告 (W)**: 300番台まで定義
- **Lint (L)**: 400番台まで定義
- **合計**: 約200種類の診断ルール