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
| **Ubuntu 22.04** | x86_64 | ✅ |

### 必要環境
- C++20対応コンパイラ（Clang 17+推奨）
- CMake 3.16+
- LLVM 17+（オプション、ネイティブコンパイル用）

### ビルド

```bash
# プロジェクトルートで
cmake -B build -DCM_USE_LLVM=ON
cmake --build build -j4
```

## 📝 基本的な使い方

### インタプリタで実行（推奨）

```bash
# プログラムを実行
./cm run examples/hello.cm

# デバッグモード
./cm run examples/hello.cm -d
```

### ネイティブコンパイル（LLVM必須）

```bash
# ネイティブ実行ファイル生成
./cm compile examples/hello.cm -o hello
./hello

# WASMコンパイル
./cm compile examples/hello.cm --target=wasm -o hello.wasm
```

### JavaScriptコンパイル

```bash
# JSへコンパイル
./cm compile --target=js examples/hello.cm -o hello.js
node hello.js
```

### 構文チェック

```bash
./cm check examples/hello.cm
```

### 中間表現の表示

```bash
# AST表示
./cm run examples/hello.cm --ast

# HIR表示
./cm run examples/hello.cm --hir

# MIR表示
./cm run examples/hello.cm --mir
```

## 📊 サンプルプログラム

### Hello World

```cm
int main() {
    println("Hello, World!");
    return 0;
}
```

### 変数と演算

```cm
int main() {
    int x = 10;
    int y = 20;
    println("x + y = {x + y}");  // 文字列補間
    return 0;
}
```

### 構造体とメソッド

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

### モジュールシステム

```cm
// math.cm
export int add(int a, int b) {
    return a + b;
}

// main.cm
import "math";

int main() {
    int result = math::add(10, 20);
    println("Result: {result}");
    return 0;
}
```

## 🧪 テスト実行

```bash
# C++ユニットテスト
ctest --test-dir build

# インタプリタテスト（203テスト）
make tip

# LLVMテスト
make tlp

# WASMテスト
make tlwp
```

## 📖 詳細ドキュメント

- [言語仕様](docs/spec/) - 型システム、構文
- [設計文書](docs/design/) - モジュールシステム、アーキテクチャ
- [チュートリアル](docs/tutorials/) - 詳細な使い方

## ✅ 実装済み機能（v0.10.0）

- **型システム**: int, uint, float, double, bool, char, string, ポインタ, 配列
- **構造体**: ネスト、配列メンバ、リテラル構文
- **インターフェース**: interface/impl、with自動実装（Eq, Ord, Clone, Hash）
- **ジェネリクス**: 関数、構造体、インターフェース
- **パターンマッチ**: match式、パターンガード
- **モジュール**: import/export、名前空間、相対/絶対パス
- **バックエンド**: インタプリタ、LLVM Native、WASM、JavaScript

---

**最終更新:** 2026-02-10
