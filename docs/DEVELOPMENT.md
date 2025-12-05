# 開発環境

## Docker（推奨）

ローカルとCI環境の一致を保証するため、Docker使用を推奨します。

### 必要なもの

- Docker Engine 24+
- Docker Compose v2+

### クイックスタート

```bash
# 開発環境に入る
docker compose run --rm dev

# 内部でビルド
cmake -B build -G Ninja
cmake --build build
```

### コマンド一覧

| コマンド | 説明 |
|---------|------|
| `docker compose run --rm dev` | 開発シェル |
| `docker compose run --rm build-clang` | Clangビルド |
| `docker compose run --rm build-gcc` | GCCビルド |
| `docker compose run --rm lint` | コードフォーマット確認 |
| `docker compose run --rm test` | テスト実行 |
| `docker compose run --rm coverage` | カバレッジ生成 |

### イメージビルド

```bash
docker compose build
```

---

## ローカル環境（Dockerなし）

### 必要なツール

| ツール | バージョン | 備考 |
|--------|-----------|------|
| GCC | 13+ | または Clang 17+ |
| Clang | 17+ | 推奨（高速、良いエラー） |
| CMake | 3.16+ | |
| Ninja | 1.10+ | 推奨（高速） |
| clang-format | 17+ | コードフォーマット |

### macOS

```bash
brew install gcc llvm cmake ninja
```

### Ubuntu

```bash
sudo apt install gcc-13 g++-13 clang-17 clang-format-17 cmake ninja-build
```

### ビルド

```bash
# Clang（推奨）
CC=clang CXX=clang++ cmake -B build -G Ninja
cmake --build build

# GCC
CC=gcc CXX=g++ cmake -B build-gcc -G Ninja
cmake --build build-gcc
```

---

## コンパイラ選択

| 用途 | 推奨 | 理由 |
|------|------|------|
| 開発時 | **Clang** | 高速コンパイル、分かりやすいエラー |
| リリース | GCC または Clang | 最適化は同等 |
| 静的解析 | Clang (clang-tidy) | 充実したチェック |

---

## ディレクトリ構成

```
build/          # Clangビルド出力
build-gcc/      # GCCビルド出力
build-release/  # リリースビルド
```
