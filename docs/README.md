# Cm言語ドキュメント

*最終更新: 2025年1月*

## 📁 ディレクトリ構造

```
docs/
├── QUICKSTART.md             # クイックスタートガイド
├── DEVELOPMENT.md            # 開発環境ガイド
├── FEATURES.md               # 実装済み機能一覧
├── PROJECT_STRUCTURE.md      # プロジェクト構造
│
├── design/                   # 設計文書
│   ├── CANONICAL_SPEC.md     # ⭐ 正式言語仕様
│   ├── MODULE_SYSTEM_FINAL.md # モジュールシステム
│   ├── architecture.md       # アーキテクチャ
│   └── ...
│
├── spec/                     # 言語仕様
│   ├── grammar.md            # 文法定義
│   └── memory.md             # メモリモデル
│
├── guides/                   # ガイド
│   ├── MAKEFILE_GUIDE.md     # Makefileガイド
│   └── MODULE_USER_GUIDE.md  # モジュールユーザーガイド
│
├── llvm/                     # LLVMバックエンド
│
├── tutorials/                # チュートリアル
│
├── implementation/           # 実装詳細
│
├── releases/                 # リリースノート
│
└── archive/                  # アーカイブ済み文書
```

## 🚀 はじめに

1. **[QUICKSTART.md](QUICKSTART.md)** - 5分でCm言語を始める
2. **[design/CANONICAL_SPEC.md](design/CANONICAL_SPEC.md)** - 正式言語仕様
3. **[FEATURES.md](FEATURES.md)** - 実装済み機能一覧

## 📖 重要なドキュメント

| ドキュメント | 説明 |
|------------|------|
| [design/CANONICAL_SPEC.md](design/CANONICAL_SPEC.md) | **正式言語仕様**（最優先） |
| [QUICKSTART.md](QUICKSTART.md) | クイックスタート |
| [FEATURES.md](FEATURES.md) | 実装済み機能 |
| [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) | プロジェクト構造 |
| [DEVELOPMENT.md](DEVELOPMENT.md) | 開発環境 |

## 🔍 トピック別

### モジュールシステム
- [design/MODULE_SYSTEM_FINAL.md](design/MODULE_SYSTEM_FINAL.md) - 設計
- [guides/MODULE_USER_GUIDE.md](guides/MODULE_USER_GUIDE.md) - 使い方

### LLVMバックエンド
- [llvm/llvm_backend_implementation.md](llvm/llvm_backend_implementation.md)

### テスト
- `make tip` - インタプリタテスト
- `make tlp` - LLVMテスト
- `make tlwp` - WASMテスト

## 📝 ルート文書

プロジェクトルートにある重要な文書：

- **[../ROADMAP.md](../ROADMAP.md)** - 開発ロードマップ
- **[../CHANGELOG.md](../CHANGELOG.md)** - 変更履歴
- **[../CONTRIBUTING.md](../CONTRIBUTING.md)** - 貢献ガイド
- **[../README.md](../README.md)** - プロジェクト概要

---

**Cm言語プロジェクトへようこそ！** 🎉
