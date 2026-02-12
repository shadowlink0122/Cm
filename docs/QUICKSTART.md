---
layout: default
title: Quick Start
---

[English](QUICKSTART.en.html)

# Cm言語コンパイラ - クイックスタート

## 🚀 ビルド方法

### サポート環境

| OS | アーキテクチャ | ステータス |
|----|-------------|----------|
| **macOS 14+** | ARM64 (Apple Silicon) | ✅ |
| **macOS 14+** | x86_64 (Intel) | ✅ |
| **Ubuntu 22.04** | x86_64 | ✅ |

### 必要環境

- C++20対応コンパイラ（Clang 17+推奨）
- CMake 3.20+
- Make
- LLVM 17（ネイティブコンパイル・JIT実行用）
- macOSの場合: Xcodeコマンドラインツール（Objective-C++含む）

> 📖 詳細な環境構築手順は [環境構築チュートリアル](tutorials/ja/basics/setup.html) を参照してください。

### ビルド

```bash
git clone https://github.com/shadowlink0122/Cm.git
cd Cm

# makeでビルド（推奨）
make build

# アーキテクチャ指定ビルド
make build ARCH=arm64     # Apple Silicon
make build ARCH=x86_64    # Intel / Linux
```

> CMakeの直接実行や詳細なオプションについては [環境構築](tutorials/ja/basics/setup.html) を参照。

## 📝 基本的な使い方

### JITで実行

```bash
./cm run hello.cm

# デバッグモード
./cm run hello.cm -d
```

### コンパイル

```bash
# ネイティブ実行ファイル
./cm compile hello.cm -o hello
./hello

# WebAssembly（wasmtime必要）
./cm compile hello.cm --target=wasm -o hello.wasm
wasmtime hello.wasm

# JavaScript（Node.js必要）
./cm compile --target=js hello.cm -o hello.js
node hello.js
```

### 構文チェック・品質ツール

```bash
# 構文・型チェック（コンパイルなし）
./cm check hello.cm

# 静的解析（コード品質チェック）
./cm lint hello.cm

# コードフォーマット
./cm fmt -w hello.cm
```

> 📖 各ツールの詳細は [Linter](tutorials/ja/compiler/linter.html) / [Formatter](tutorials/ja/compiler/formatter.html) を参照。

## 📊 サンプルプログラム

### Hello World

```cm
int main() {
    println("Hello, World!");
    return 0;
}
```

### 変数と文字列補間

```cm
int main() {
    int x = 10;
    int y = 20;
    println("x + y = {x + y}");
    return 0;
}
```

### 構造体とwith自動実装

```cm
struct Point with Eq {
    int x;
    int y;
}

int main() {
    Point p1 = {x: 10, y: 20};
    Point p2 = {x: 10, y: 20};
    
    if (p1 == p2) {
        println("Points are equal!");
    }
    return 0;
}
```

### Enumとパターンマッチ

```cm
enum Color {
    Red,
    Green,
    Blue,
    Custom(int, int, int)
}

int main() {
    Color c = Color::Custom(255, 128, 0);
    match (c) {
        Color::Red => println("Red");
        Color::Custom(r, g, b) => println("RGB: {r}, {g}, {b}");
        _ => println("Other");
    }
    return 0;
}
```

### インラインユニオン型とnull型

```cm
typedef MaybeInt = int | null;

int main() {
    // typedefユニオン型
    MaybeInt x = null;
    MaybeInt y = 42 as MaybeInt;
    
    // インラインユニオン型（typedef不要）
    int | null a = null;
    int | string | null b = null;
    return 0;
}
```

## 🧪 テスト実行

```bash
# C++ユニットテスト
make t

# JITテスト（並列）
make tip

# LLVMテスト（並列）
make tlp

# WASMテスト（並列・wasmtime必要）
make tlwp

# JSテスト（並列・Node.js必要）
make tjp
```

> 📖 makeコマンドの全一覧は [環境構築](tutorials/ja/basics/setup.html) を参照。

## 📖 ドキュメント

| ドキュメント | 内容 |
|------------|------|
| [チュートリアル](tutorials/ja/) | 40+ファイルの段階的な学習ガイド |
| [環境構築](tutorials/ja/basics/setup.html) | 依存関係・makeコマンド一覧 |
| [コンパイラの使い方](tutorials/ja/compiler/usage.html) | コマンド・オプション |
| [JSバックエンド](tutorials/ja/compiler/js-compilation.html) | JavaScript出力ガイド |
| [VSCode拡張機能](../vscode-extension/README.html) | 構文ハイライト・アイコン・開発ガイド |
| [機能リファレンス](FEATURES.html) | 実装済み機能一覧 |
| [言語仕様](design/CANONICAL_SPEC.html) | 完全な言語仕様 |
| [リリースノート](releases/) | バージョン履歴 |

## ✅ 実装済み機能（v0.14.0）

- **型システム**: int, uint, float, double, bool, char, string, ポインタ, 配列
- **ユニオン型**: インラインユニオン (`int | null`)、typedefユニオン、null型
- **構造体**: コンストラクタ (`self()`)、デストラクタ (`~self()`)、ネスト
- **インターフェース**: interface/impl、with自動実装（Eq, Ord, Clone, Hash）
- **ジェネリクス**: 関数・構造体・ジェネリックコンストラクタ・複数型パラメータ
- **Enum/Tagged Union**: Associated Data、match分解、Option, Result
- **パターンマッチ**: match式、パターンガード、網羅性チェック
- **所有権**: 移動セマンティクス、借用チェック
- **モジュール**: import/export、名前空間
- **標準ライブラリ**: Vector\<T\>, Queue\<T\>, HashMap\<K,V\>, HTTP/HTTPS, Thread, Mutex, GPU
- **バックエンド**: JIT, LLVM Native (x86_64/ARM64), WASM, JavaScript, UEFI
- **ツール**: cm lint, cm fmt, cm check, プリプロセッサ (#ifdef)

---

**最終更新:** 2026-02-12
