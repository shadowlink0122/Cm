[English](README.en.html)

# Cm言語仕様

このディレクトリにはCm言語の正式な仕様が含まれます。

**ステータス**: 🚧 開発中（機能実装に応じて追記）

## ドキュメント

| ファイル | 内容 | 状態 |
|----------|------|------|
| [grammar.md](grammar.html) | 文法定義（BNF） | 🔨 作成中 |
| [types.md](types.html) | 型システム | 📋 予定 |
| [memory.md](memory.html) | メモリモデル | 📋 予定 |
| [modules.md](modules.html) | モジュールシステム | 📋 予定 |
| [stdlib.md](stdlib.html) | 標準ライブラリ | 📋 予定 |

## 言語概要

- **構文**: C++風（戻り値型が先）
- **型システム**: 静的型付け、ジェネリクス
- **メモリ**: 手動管理 (new/delete) + 参照/ポインタ（所有権なし）
- **ファイル拡張子**: `.cm`