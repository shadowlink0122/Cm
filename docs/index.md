---
layout: default
title: ホーム
nav_order: 1
---

[English](index.en.html)

# Cm プログラミング言語

**C++の構文とRustにインスパイアされた機能を併せ持つ、モダンなシステムプログラミング言語**

---

## 🚀 クイックリンク

- [📚 はじめる](QUICKSTART.html)

- [📖 チュートリアル](tutorials/ja/)

- [📋 リリースノート](releases/)

- [🏗️ プロジェクト構造](PROJECT_STRUCTURE.html)

- [🎯 ロードマップ](../ROADMAP.html)



---



## 📖 ドキュメント



### ユーザー向け



- **[チュートリアル](tutorials/ja/)** - 段階的な学習ガイド
  - [基本編](tutorials/ja/basics/) - 変数、関数、制御構文
  - [型システム編](tutorials/ja/types/) - 構造体、Enum、インターフェース
  - [高度な機能編](tutorials/ja/advanced/) - ジェネリクス、マクロ、match式
  - [コンパイラ編](tutorials/ja/compiler/) - LLVMバックエンド、最適化



- **[クイックスタートガイド](QUICKSTART.html)** - 5分で始めるCm言語



### 開発者向け



- **[プロジェクト構造](PROJECT_STRUCTURE.html)** - コードベースの構成

- **[開発ガイド](DEVELOPMENT.html)** - 開発環境のセットアップ

- **[貢献ガイド](CONTRIBUTING.html)** - 貢献のための規約と手順

- **[設計ドキュメント](design/)** - 言語仕様とアーキテクチャ



### リリース情報



- **[リリースノート](releases/)** - バージョン履歴と変更ログ

- **[機能リファレンス](features/)** - 現在の実装状況

- **[ロードマップ](../ROADMAP.html)** - 将来の計画



---



## 🎯 言語の特徴



### ✅ 実装済み (v0.10.0)



- **C++ライクな構文** - 親しみやすく読みやすい

- **強力な型システム** - コンパイル時の安全性

- **ジェネリクス** - 型安全なジェネリックプログラミング

- **インターフェース** - トレイトベースのポリモーフィズム

- **LLVMバックエンド** - ネイティブコード生成

- **WASMサポート** - WebAssemblyバックエンド

- **パターンマッチング** - 強力なmatch式

- **ゼロコスト抽象化** - オーバーヘッドのないパフォーマンス



### 🔄 進行中



- **標準ライブラリ** - コアユーティリティとデータ構造

- **モジュールシステム** - 強化されたパッケージ管理

- **所有権システム** - GCなしのメモリ安全性 (v0.11.0以降)



---



## 💡 コード例



```cm

// Hello World

int main() {

    println("Hello, Cm!");

    return 0;

}



// ジェネリック関数

<T: Ord> T max(T a, T b) {

    return a > b ? a : b;

}



// インターフェース実装

interface Drawable {

    void draw();

}



struct Circle {

    int radius;

}



impl Circle for Drawable {

    void draw() {

        println("Drawing circle with radius {}", self.radius);

    }

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

- [ディスカッション](https://github.com/shadowlink0122/Cm/discussions)



---



**最終更新:** v0.10.0 (2025年1月)



© 2025-2025 Cm Language Project



---

[English Version here](./)
