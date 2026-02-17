---
layout: default
title: Development
---

[English](DEVELOPMENT.en.html)

# 開発環境ガイド

*最終更新: 2026年2月*

## 必要環境

| ツール | バージョン | 備考 |
|--------|-----------|------|
| C++コンパイラ | C++20対応 | Clang 17+ 推奨 |
| CMake | 3.20+ | |
| Make | - | ビルド・テスト実行 |
| LLVM | 17+ | ネイティブ/WASMコンパイル用（オプション） |
| Ninja | 1.10+ | 高速ビルド（オプション） |

### オプション依存

| ツール | 用途 |
|--------|------|
| OpenSSL (libssl-dev) | HTTPS通信サポート |
| GTest (libgtest-dev) | C++ユニットテスト |
| wasmtime | WASM実行 |
| Node.js | JS実行 |

## クイックスタート

### macOS

```bash
# Homebrew
brew install llvm@17 cmake ninja

# ビルド（コンパイラ + ランタイムライブラリ）
make build    # コンパイラのみ
make libs     # ランタイムライブラリ
make all      # 上記両方（推奨）
```

### Ubuntu/Debian

```bash
# 依存関係
sudo apt install clang-17 llvm-17-dev cmake ninja-build \
    libssl-dev libgtest-dev pkg-config

# ビルド
make all
```

## ビルドコマンド

| コマンド | 説明 |
|---------|------|
| `make build` | コンパイラのみビルド（CMake） |
| `make libs` | ランタイムライブラリのみビルド |
| `make all` | コンパイラ + ランタイム |
| `make build-all` | テスト込み全ビルド |
| `make release` | リリースビルド（最適化あり） |
| `make clean` | ビルドディレクトリ削除 |
| `make rebuild` | クリーン → ビルド |

### ビルドアーキテクチャ

```bash
# 分離ビルドの流れ
make build                # コンパイラのみ（CMake）
make libs                 # ランタイム（Make, libs/native/Makefile）

# アーキテクチャ指定
make build ARCH=arm64     # Apple Silicon
make build ARCH=x86_64    # Intel / Linux
```

> `./cm` はシンボリックリンクで `build/bin/cm` を参照します。

## テスト実行

```bash
# C++ユニットテスト（GTest必須）
make test

# JITテスト（並列）
make tip

# LLVMテスト
make tlp

# WASMテスト（wasmtime必要）
make twp

# 全テスト
make tall
```

## Docker（CI環境）

Docker環境ではnamed volumeにより、ローカルのbuild/を汚さずにビルド・テストできます。

```bash
# Dockerイメージビルド
docker compose build dev

# 開発シェル
docker compose run --rm dev

# ビルド + テスト
docker compose run --rm test

# Clangビルド
docker compose run --rm build-clang
```

> Dockerコンテナ内の`build/`はnamed volumeに隔離されるため、ローカルのビルド成果物と干渉しません。

## ディレクトリ構成

```
src/                # コンパイラソースコード
├── frontend/       # Lexer, Parser, TypeChecker
├── hir/            # 高レベル中間表現
├── mir/            # 中間表現（SSA形式）
├── codegen/        # コード生成（インタプリタ、LLVM）
└── preprocessor/   # import処理

libs/native/        # ランタイムライブラリ（C/C++）
├── Makefile        # ランタイムビルド（make libs）
├── net/            # TCP/UDP
├── http/           # HTTP/HTTPS
├── sync/           # Mutex/Channel
├── thread/         # Thread
└── gpu/            # GPU (Metal, macOSのみ)

tests/              # テストファイル
docs/               # ドキュメント
std/                # 標準ライブラリ（Cm）
examples/           # サンプルコード
```

## コーディング規約

- C++20を使用
- clang-formatでフォーマット
- 1ファイル1000行目安
- デバッグ出力は `[STAGE]` 形式

詳細: [CONTRIBUTING.md](../CONTRIBUTING.html)