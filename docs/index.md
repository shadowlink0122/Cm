---
layout: default
title: ホーム
nav_order: 1
---

# Cm プログラミング言語

**C++の構文とRustにインスパイアされた機能を併せ持つ、モダンなシステムプログラミング言語**

---

## 🚀 クイックリンク

- [📚 はじめる](QUICKSTART.html)
- [📖 チュートリアル](tutorials/ja/index.html)
- [📋 リリースノート](releases/)
- [🏗️ プロジェクト構造](PROJECT_STRUCTURE.html)
- [🎯 ロードマップ](ROADMAP.html)

---

## 📖 ドキュメント

### ユーザー向け

- **[チュートリアル](tutorials/ja/index.html)** - 段階的な学習ガイド
  - [基本編](tutorials/ja/basics/introduction.html) - 変数、関数、制御構文
  - [型システム編](tutorials/ja/types/structs.html) - 構造体、Enum、インターフェース
  - [高度な機能編](tutorials/ja/advanced/match.html) - ジェネリクス、マクロ、match式
  - [コンパイラ編](tutorials/ja/compiler/usage.html) - LLVMバックエンド、最適化
  - [標準ライブラリ編](tutorials/ja/stdlib/) - HTTP、ネットワーク、スレッド、GPU

- **[クイックスタートガイド](QUICKSTART.html)** - 5分で始めるCm言語

### 開発者向け

- **[プロジェクト構造](PROJECT_STRUCTURE.html)** - コードベースの構成
- **[開発ガイド](DEVELOPMENT.html)** - 開発環境のセットアップ
- **[貢献ガイド](CONTRIBUTING.html)** - 貢献のための規約と手順
- **[設計ドキュメント](design/)** - 言語仕様とアーキテクチャ

### リリース情報

- **[リリースノート](releases/)** - バージョン履歴と変更ログ
- **[機能リファレンス](FEATURES.html)** - 現在の実装状況
- **[ロードマップ](ROADMAP.html)** - 将来の計画

---

## 🎯 言語の特徴

### ✅ 言語コア (v0.13.1)

- **C++ライクな構文** - 親しみやすく読みやすい
- **強力な型システム** - コンパイル時の安全性
- **ジェネリクス** - 型安全なジェネリックプログラミング
- **インターフェース** - トレイトベースのポリモーフィズム
- **パターンマッチング** - 強力なmatch式とガード条件
- **インラインアセンブリ** - `__asm__`によるハードウェアアクセス
- **条件付きコンパイル** - `#ifdef`/`#ifndef`ディレクティブ

### ✅ バックエンド

- **LLVM Native** - ARM64/x86_64 ネイティブコード生成
- **WASM** - WebAssemblyバックエンド

### ✅ 標準ライブラリ (Native向け)

- **コレクション** - `Vector<T>`, `Queue<T>`, `HashMap<K,V>`
- **スレッド** - `std::thread`, `Mutex`, `Channel`
- **ネットワーク** - `std::http` (HTTP/HTTPS、OpenSSL統合)
- **GPU** - `std::gpu` (Apple Metalバックエンド)

### 🔄 進行中

- **パッケージ管理** - `cm pkg init/add`
- **所有権システム** - 借用チェッカーの強化
- **JSバックエンド** - JavaScriptコード生成 (v0.14.0予定)

---

## 💡 コード例

```cm
import std::io::println;

// Hello World
int main() { println("Hello, Cm!"); return 0; }

// ジェネリック関数
<T: Ord> T max(T a, T b) { return a > b ? a : b; }

// インターフェース実装
interface Drawable { void draw(); }
struct Circle { int radius; }
impl Circle for Drawable {
    void draw() { println("Circle({})", self.radius); }
}
```

---

## 🛠️ ソースからのビルド

```bash
# リポジトリをクローン
git clone https://github.com/shadowlink0122/Cm.git
cd Cm

# LLVMバックエンドでビルド
cmake -B build -DCM_USE_LLVM=ON
cmake --build build

# テストの実行
ctest --test-dir build
```

---

## 📊 プロジェクト統計

| コンポーネント | ステータス | カバレッジ |
|-----------|--------|----------|
| Lexer/Parser | ✅ 完了 | 90%+ |
| 型システム | ✅ 完了 | 85%+ |
| HIR/MIR | ✅ 完了 | 80%+ |
| LLVM Backend | ✅ 完了 | 85%+ |
| WASM Backend | ✅ 完了 | 80%+ |
| 標準ライブラリ | 🔄 進行中 | 30%+ |

---

## 🤝 貢献について

貢献を歓迎します！詳細は [貢献ガイド](CONTRIBUTING.html) をご覧ください。

---

## 📄 ライセンス

本プロジェクトは MIT ライセンスの下で公開されています。詳細は LICENSE ファイルをご確認ください。

---

## 🔗 リンク

- [GitHub リポジトリ](https://github.com/shadowlink0122/Cm)
- [課題トラッカー](https://github.com/shadowlink0122/Cm/issues)
- [ロードマップ](ROADMAP.html)

---

**最終更新:** v0.13.1 (2026年2月)

© 2025-2026 Cm Language Project

---
[English Version here](en/index.html)