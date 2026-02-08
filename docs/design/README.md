[English](README.en.html)

# Cm 設計ドキュメント

このディレクトリには、Cm言語処理系の設計に関するドキュメントが含まれています。

**最終更新:** 2026-02-08

## ドキュメント一覧

### 言語仕様

| ドキュメント | 説明 |
|-------------|------|
| [CANONICAL_SPEC.md](CANONICAL_SPEC.html) | 正式言語仕様 |
| [cm_grammar.md](cm_grammar.html) | EBNF文法定義 |

### v0.14.0 設計（計画中）

| ドキュメント | 説明 |
|-------------|------|
| [v0.14.0/baremetal.md](v0.14.0/baremetal.html) | ベアメタル対応設計 |
| [v0.14.0/uefi_hello_world_roadmap.md](v0.14.0/uefi_hello_world_roadmap.html) | UEFI Hello Worldロードマップ |

### v0.13.1 設計

| ドキュメント | 説明 |
|-------------|------|
| [v0.13.1/001_overview.md](v0.13.1/001_overview.html) | v0.13.1概要 |

### v0.13.0 設計

| ドキュメント | 説明 |
|-------------|------|
| [v0.13.0/001_technical_comparison.md](v0.13.0/001_technical_comparison.html) | 技術比較 |
| [v0.13.0/002_implementation_plan.md](v0.13.0/002_implementation_plan.html) | 実装計画 |
| [v0.13.0/enum_design.md](v0.13.0/enum_design.html) | Enum設計 |
| [v0.13.0/language_extensions.md](v0.13.0/language_extensions.html) | 言語拡張 |
| [v0.13.0/breaking_changes_analysis.md](v0.13.0/breaking_changes_analysis.html) | 破壊的変更分析 |
| [v0.13.0/std_asm_library.md](v0.13.0/std_asm_library.html) | std::asmライブラリ |
| [v0.13.0/typed_macros_and_concurrency.md](v0.13.0/typed_macros_and_concurrency.html) | 型付きマクロと並行処理 |
| [v0.13.0/exportable_macros.md](v0.13.0/exportable_macros.html) | エクスポート可能マクロ |
| [v0.13.0/package_manager.md](v0.13.0/package_manager.html) | パッケージマネージャ |

### 過去のバージョン

v0.12.0以前の設計ドキュメントは [リリースノート](../releases/) を参照してください。

## 設計原則

1. **クロスバックエンド一致**: インタプリタとコンパイラで同一結果
2. **マルチプラットフォーム**: macOS/Linux対応（ARM64/x86_64）
3. **拡張性**: 新バックエンドの追加容易
4. **良いエラーメッセージ**: ソース位置情報の保持