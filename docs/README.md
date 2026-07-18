# Cm言語ドキュメント

## 📁 ディレクトリ構造

```
docs/
├── QUICKSTART.md             # クイックスタートガイド
├── DEVELOPMENT.md            # 開発環境ガイド
├── FEATURES.md               # 実装済み機能一覧
├── PROJECT_STRUCTURE.md      # プロジェクト構造
├── CONTRIBUTING.md           # 貢献ガイド
│
├── design/                   # 設計文書
│   ├── CANONICAL_SPEC.md     # ⭐ 正式言語仕様
│   ├── cm_grammar.md         # 文法定義
│   ├── backend_support_matrix.md # 機能×バックエンドのサポート表
│   ├── error_handling_policy.md  # エラーハンドリング方針
│   ├── roadmap_v1.0.0.md     # ロードマップ
│   └── v<バージョン>/        # 開発中バージョンの実装設計
│
├── tutorials/                # チュートリアル（ja / en）
│   ├── ja/{basics,types,advanced,stdlib,compiler,internals}/
│   └── en/{basics,types,advanced,compiler,internals}/
│
├── releases/                 # リリースノート
│
└── archive/                  # アーカイブ済み文書（旧設計・実装済み設計）
```

## 🚀 はじめに

1. **[QUICKSTART.md](QUICKSTART.md)** - 5分でCm言語を始める
2. **[tutorials/ja/index.md](tutorials/ja/index.md)** - チュートリアルで段階的に学ぶ
3. **[design/CANONICAL_SPEC.md](design/CANONICAL_SPEC.md)** - 正式言語仕様

## 📖 重要なドキュメント

| ドキュメント | 説明 |
|------------|------|
| [design/CANONICAL_SPEC.md](design/CANONICAL_SPEC.md) | **正式言語仕様**（最優先） |
| [QUICKSTART.md](QUICKSTART.md) | クイックスタート |
| [FEATURES.md](FEATURES.md) | 実装済み機能一覧 |
| [design/backend_support_matrix.md](design/backend_support_matrix.md) | 機能×バックエンドのサポート表 |
| [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) | プロジェクト構造 |
| [DEVELOPMENT.md](DEVELOPMENT.md) | 開発環境 |

## 🔍 テスト

- `make test-unit` - C++単体テスト
- `make test-regression` - C++回帰テスト
- `make test-interpreter` / `make test-llvm` / `make test-llvm-wasm` / `make test-js` / `make test-sv` - バックエンドスイート
- `make test` - すべて実行

詳細は [DEVELOPMENT.md](DEVELOPMENT.md) を参照してください。

## 📝 ルート文書

プロジェクトルートにある重要な文書：

- **[../README.md](../README.md)** - プロジェクト概要
- **[../CONTRIBUTING.md](../CONTRIBUTING.md)** - 貢献ガイド
