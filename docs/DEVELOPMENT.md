---
layout: default
title: Development
---

[English](DEVELOPMENT.en.html)

# 開発環境ガイド

*最終更新: 2025年1月*

## 必要環境

| ツール | バージョン | 備考 |
|--------|-----------|------|
| C++コンパイラ | C++20対応 | Clang 17+ 推奨 |
| CMake | 3.16+ | |
| LLVM | 17+ | ネイティブ/WASMコンパイル用（オプション） |
| Ninja | 1.10+ | 高速ビルド（オプション） |

## クイックスタート

### macOS

```bash
# Homebrew
brew install llvm cmake ninja

# ビルド
cmake -B build -DCM_USE_LLVM=ON
cmake --build build -j4
```

### Ubuntu/Debian

```bash
# 依存関係
sudo apt install clang-17 llvm-17-dev cmake ninja-build

# ビルド
cmake -B build -DCM_USE_LLVM=ON
cmake --build build -j4
```

## ビルドオプション

| オプション | デフォルト | 説明 |
|-----------|----------|------|
| `CM_USE_LLVM` | OFF | LLVMバックエンド有効化 |
| `CMAKE_BUILD_TYPE` | Debug | Release でリリースビルド |

```bash
# 開発ビルド（デフォルト）
cmake -B build -DCM_USE_LLVM=ON

# リリースビルド
cmake -B build -DCM_USE_LLVM=ON -DCMAKE_BUILD_TYPE=Release
```

## テスト実行

```bash
# C++ユニットテスト
ctest --test-dir build

# インタプリタテスト（全203テスト）
make tip

# LLVMテスト
make tlp

# WASMテスト
make tlwp

# 全テスト
make tall
```

## Docker（CI環境）

```bash
# 開発シェル
docker compose run --rm dev

# テスト実行
docker compose run --rm test
```

## ディレクトリ構成

```
src/                # コンパイラソースコード
├── frontend/       # Lexer, Parser, TypeChecker
├── hir/            # 高レベル中間表現
├── mir/            # 中間表現（SSA形式）
├── codegen/        # コード生成（インタプリタ、LLVM）
└── preprocessor/   # import処理

tests/              # テストファイル
├── test_programs/  # Cmテストプログラム
└── *.cpp          # C++ユニットテスト

docs/               # ドキュメント
├── design/        # 設計文書
├── guides/        # ガイド
└── spec/          # 言語仕様

std/                # 標準ライブラリ
examples/           # サンプルコード
```

## コーディング規約

- C++20を使用
- clang-formatでフォーマット
- 1ファイル1000行目安
- デバッグ出力は `[STAGE]` 形式

詳細: [CONTRIBUTING.md](../CONTRIBUTING.html)