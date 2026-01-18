# Cm言語 Linter/Formatter 実装ロードマップ

**最終更新:** 2026-01-14
**バージョン:** v0.12.0

## 1. 現在の実装状況

### ✅ 完了

| コンポーネント | ファイル | 状態 |
|--------------|---------|------|
| DiagnosticLevel | `src/diagnostics/levels.hpp` | ✅ 完了 |
| DiagnosticCatalog | `src/diagnostics/catalog.hpp` | ✅ 基本構造完了 |
| DiagnosticEngine | `src/diagnostics/engine.hpp` | ✅ 基本構造完了 |
| Error定義 (E001-E299) | `src/diagnostics/definitions/errors.hpp` | ✅ 定義のみ |
| Warning定義 (W001-W099) | `src/diagnostics/definitions/warnings.hpp` | ✅ 定義のみ |
| Lint定義 (L001-L199) | `src/diagnostics/definitions/lints.hpp` | ✅ 定義のみ |
| パーサーエラー回復 | `src/frontend/parser/parser_stmt.cpp` | ✅ switch文修正済み |
| パーサーエラー重複抑制 | `src/frontend/parser/parser.hpp` | ✅ 同一行エラー抑制 |
| `cm check` 警告表示 | `src/main.cpp` | ✅ 完了 |
| **W001: 未使用変数検出** | `scope.hpp`, `utils.cpp` | ✅ 完了 |
| **L100: 関数名チェック** | `decl.cpp` | ✅ snake_case |
| **L101: 変数名チェック** | `stmt.cpp` | ✅ snake_case |
| **L102: 定数名チェック** | `stmt.cpp` | ✅ UPPER_SNAKE |
| **L103: 型名チェック** | `decl.cpp` | ✅ PascalCase |

### ⏳ 未実装

| 機能 | 優先度 | 関連Doc |
|-----|-------|---------|
| `-r` 再帰チェック | 高 | 082 |
| 自動修正 (--fix) | 中 | 078 |
| 設定ファイル (.cmconfig.yml) | 中 | **084** |
| `cm lint` / `cm fmt` 本格実装 | 低 | 078 |

---

## 2. 実装フェーズ

### Phase 1: 基本Lint実行 ✅ 完了
W001 未使用変数検出

### Phase 2: 命名規則チェック ✅ 完了
L100-L103 命名規則チェック

---

### Phase 3: 複数ファイル対応 ✅ 完了

**目標**: `-r` オプションと複数ファイル引数

```
[x] 複数ファイル引数のサポート
[x] -r オプションで再帰チェック
[x] 除外パターン (--exclude)
```

---

### Phase 4: 設定システム ✅ 完了

**目標**: `.cmconfig.yml` による設定 (084_lint_configuration_system.md)

```
[x] .cmconfig.yml パーサー
[x] ルール有効/無効設定
[ ] エラーレベルカスタマイズ
[ ] インラインコメント制御 (@cm-disable-next-line)
[ ] プリセット (minimal/recommended/strict)
```

---

## 4. 今後の拡張（optional）

- エラーレベルカスタマイズ
- インラインコメント制御 (`@cm-disable-next-line`)
- プリセット (minimal/recommended/strict)
- 自動修正は `cm fmt` で対応

---

## 3. 参照ドキュメント

| 文書 | 内容 |
|-----|------|
| [078_linter_formatter_design.md](./claude/078_linter_formatter_design.md) | Linter/Formatter設計 |
| [079_unified_diagnostic_system.md](./claude/079_unified_diagnostic_system.md) | 統一診断システム |
| [080_diagnostic_catalog.md](./claude/080_diagnostic_catalog.md) | E/W/L診断カタログ |
| [081_implementation_guide.md](./claude/081_implementation_guide.md) | 実装ガイド |
| [082_recursive_module_analysis.md](./claude/082_recursive_module_analysis.md) | 再帰モジュール解析 |
| [083_optimized_import_resolution.md](./claude/083_optimized_import_resolution.md) | import最適化 |
| [084_lint_configuration_system.md](./claude/084_lint_configuration_system.md) | **Lint設定システム** |

---

## 4. アーキテクチャ概要

```
┌───────────────────────────────────────────┐
│                CLI (cm check/lint/fmt)     │
└─────────────────────┬─────────────────────┘
                      │
                      ▼
┌───────────────────────────────────────────┐
│              TypeChecker                   │
│     - Diagnostics (W001, L100-L103)        │
└─────────────────────┬─────────────────────┘
                      │
     ┌────────────────┼────────────────┐
     ▼                ▼                ▼
┌─────────┐    ┌─────────┐    ┌─────────────┐
│ Errors  │    │Warnings │    │ Lint Rules  │
│E001-E999│    │W001-W999│    │ L001-L999   │
└─────────┘    └─────────┘    └─────────────┘
```
