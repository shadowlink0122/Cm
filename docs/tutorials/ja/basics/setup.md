---
title: 環境構築
parent: Tutorials
---

[English](../../en/basics/setup.html)

# 環境構築

**難易度:** 🟢 初級  
**所要時間:** 30分

## 📚 この章で学ぶこと

- 必要な環境と依存関係
- コンパイラのビルド方法
- makeコマンドの使い方
- テストの実行
- 初めてのプログラム実行
- エディタ設定（VSCode拡張機能）

---

## サポート環境

| OS | アーキテクチャ | ステータス |
|----|-------------|----------|
| **macOS 14+** | ARM64 (Apple Silicon) | ✅ 完全サポート |
| **macOS 14+** | x86_64 (Intel) | ✅ 完全サポート |
| **Ubuntu 22.04** | x86_64 | ✅ 完全サポート |
| Windows | - | ❌ 未サポート |

---

## 必須要件

### 1. C++20コンパイラ

Cmコンパイラ自体がC++20で書かれているため、C++20対応のコンパイラが必要です。

| コンパイラ | 最小バージョン | 推奨バージョン |
|-----------|-------------|-------------|
| **Clang** | 17+ | 17 |
| **GCC** | 12+ | 13 |
| **AppleClang** | 15+ | 自動 (Xcode付属) |

### 2. CMake

| ツール | 最小バージョン |
|-------|-------------|
| **CMake** | 3.20+ |

### 3. Make

ビルドとテスト実行に`make`コマンドを使用します。

### 4. Git

ソースコード取得用。

### 5. LLVM 17

ネイティブコンパイル（`cm compile`、JIT実行 `cm run`）に必要です。

> **Note**: インタプリタのみの使用であればLLVMは不要ですが、ほとんどの機能にはLLVMが必要です。

### 6. Objective-C++ (macOS のみ)

macOSでGPU計算 (Apple Metal) を使用する場合、Objective-C++コンパイラが必要です。
Xcodeをインストールすると自動的に利用可能になります。

```bash
# Xcodeコマンドラインツールをインストール（まだの場合）
xcode-select --install
```

> `std/gpu/gpu_runtime.mm` のビルドにObjective-C++が使用されます。

---

## オプション要件

### wasmtime (WASM実行用)

WASMバイナリの実行に必要です。

```bash
# macOS
brew install wasmtime

# Linux
curl https://wasmtime.dev/install.sh -sSf | bash
```

### Node.js (JS実行 / VSCode拡張機能用)

JSバックエンドで生成されたコードの実行と、VSCode拡張機能のビルドに必要です。

```bash
# Node.js v20+ を推奨
node --version
```

### pnpm (VSCode拡張機能用)

VSCode拡張機能の依存関係管理に必要です（npmでも代替可能）。

```bash
npm install -g pnpm
pnpm --version
```

---

## インストール手順

### macOS (ARM64 / Apple Silicon)

```bash
# Xcodeコマンドラインツール（C++, Objective-C++, Make）
xcode-select --install

# Homebrewでインストール
brew install cmake llvm@17

# LLVMのパスを設定（.zshrcに追加推奨）
export PATH="/opt/homebrew/opt/llvm@17/bin:$PATH"
export LDFLAGS="-L/opt/homebrew/opt/llvm@17/lib"
export CPPFLAGS="-I/opt/homebrew/opt/llvm@17/include"

# オプション: WASM/JS
brew install wasmtime node
```

### macOS (Intel / x86_64)

```bash
# Xcodeコマンドラインツール
xcode-select --install

# Homebrewでインストール
brew install cmake llvm@17

# LLVMのパスを設定（Intelは/usr/local）
export PATH="/usr/local/opt/llvm@17/bin:$PATH"

# オプション
brew install wasmtime node
```

> **Tip**: `uname -m` で自分のアーキテクチャを確認できます。
> `arm64` → Apple Silicon、`x86_64` → Intel

### Ubuntu (x86_64)

```bash
# 必須パッケージ
sudo apt-get update
sudo apt-get install -y cmake build-essential git make pkg-config

# LLVM 17
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 17
sudo apt-get install -y llvm-17-dev clang-17

# OpenSSL（HTTPS通信サポート、オプション）
sudo apt-get install -y libssl-dev

# GTest（C++ユニットテスト、オプション）
sudo apt-get install -y libgtest-dev

# オプション: wasmtime
curl https://wasmtime.dev/install.sh -sSf | bash

# オプション: Node.js
sudo apt-get install -y nodejs
```

---

## インストール確認

すべてが正しくインストールされているか確認しましょう：

```bash
# C++コンパイラ
clang++ --version      # Clang 17+ であること
# または
g++ --version          # GCC 12+ であること

# CMake
cmake --version        # 3.20+ であること

# Make
make --version

# LLVM
llvm-config --version  # 17.x であること

# Git
git --version

# オプション
wasmtime --version     # WASM実行に必要
node --version         # JS実行に必要（v16+）
```

---

## コンパイラのビルド

### 1. リポジトリのクローン

```bash
git clone https://github.com/shadowlink0122/Cm.git
cd Cm
```

### 2. makeでビルド（推奨）

```bash
# 全ビルド（コンパイラ + ランタイムライブラリ）
make all

# またはステップごとに
make build   # コンパイラのみ
make libs    # ランタイムライブラリのみ

# リリースビルド（最適化あり）
make release
```

> `./cm` は `build/bin/cm` へのシンボリックリンクです。

### 3. アーキテクチャ指定ビルド

`ARCH`変数でターゲットアーキテクチャを指定できます：

```bash
# ARM64向けビルド（Apple Silicon）
make build ARCH=arm64

# x86_64向けビルド（Intel / Linux）
make build ARCH=x86_64
```

> デフォルトではLLVMの`llvm-config --host-target`から自動検出されます。

### 4. Docker環境でのビルド

Docker環境ではnamed volumeにより、ローカルのbuild/を汚さずにビルド・テストできます。

```bash
# Dockerイメージのビルド
docker compose build dev

# コンテナ内で開発シェル起動
docker compose run --rm dev

# ビルド + テスト実行
docker compose run --rm test
```

> ローカルのbuild/ディレクトリとDockerのbuild/は独立しています。

### 5. CMake直接ビルド

makeを使わない場合は直接CMakeを使用できます：

```bash
# LLVMなしでビルド（インタプリタのみ）
cmake -B build
cmake --build build

# LLVM有効ビルド（推奨）
cmake -B build -DCM_USE_LLVM=ON
cmake --build build

# 並列ビルド（高速化）
cmake --build build -j8
```

### 6. ビルド確認

```bash
./cm --version
```

---

## Makeコマンド一覧

### ビルドコマンド

| コマンド | エイリアス | 説明 |
|---------|----------|------|
| `make all` | - | コンパイラ + ランタイム（推奨） |
| `make build` | `make b` | コンパイラのみ (CMake) |
| `make libs` | - | ランタイムライブラリのみ (Make) |
| `make build-all` | - | テスト込み全ビルド |
| `make release` | - | リリースビルド (最適化あり) |
| `make clean` | - | ビルドディレクトリ削除 |
| `make rebuild` | - | クリーン → ビルド |

### テストコマンド

| コマンド | エイリアス | 説明 |
|---------|----------|------|
| `make test` | `make t` | C++ユニットテスト (ctest) |
| `make test-jit-parallel` | `make tip` | JIT全テスト（並列） |
| `make test-llvm-parallel` | `make tlp` | LLVM全テスト（並列） |
| `make test-llvm-wasm-parallel` | `make twp` | WASM全テスト（並列） |
| `make test-js-parallel` | `make tjp` | JS全テスト（並列） |
| `make test-lint` | `make tli` | Lintテスト |

### 最適化レベル別テスト

各テストは最適化レベルを指定して実行できます：

```bash
# JITテスト（O0〜O3）
make tip0  # -O0
make tip1  # -O1
make tip2  # -O2
make tip3  # -O3

# LLVMテスト（O0〜O3）
make tlp0 / make tlp1 / make tlp2 / make tlp3

# WASMテスト（O0〜O3）
make twp0 / make twp1 / make twp2 / make twp3

# JSテスト（O0〜O3）
make tjp0 / make tjp1 / make tjp2 / make tjp3
```

### その他

| コマンド | 説明 |
|---------|------|
| `make run` | プログラムを実行 |
| `make format` | コードフォーマット |
| `make format-check` | フォーマットチェック |
| `make help` | コマンド一覧表示 |

---

## 構文チェックツール

Cmには3つの構文チェックツールが組み込まれています：

### cm check - 構文・型チェック

コードをコンパイルせずに、構文と型の正当性だけをチェックします。

```bash
./cm check program.cm
```

**出力例（エラーなし）:**
```
✅ No errors found.
```

**出力例（エラーあり）:**
```
error: type mismatch: expected 'int', got 'string'
  --> program.cm:5:16
  |
5 |     int x = "hello";
  |              ^^^^^^^
```

### cm lint - 静的解析

コードの品質問題を検出し、改善のヒントを提供します。

```bash
# 単一ファイルをチェック
./cm lint program.cm

# ディレクトリを再帰的にチェック
./cm lint src/
```

**出力例:**
```
warning: variable 'myValue' should use snake_case naming [L200]
  --> program.cm:5:9
  |
5 |     int myValue = 10;
  |         ^^^^^^^
  |
  = help: consider renaming to 'my_value'

warning: unused variable 'x' [W100]
  --> program.cm:8:9
  |
8 |     int x = 42;
  |         ^
```

**チェック項目:**

| ルール | コード | 説明 |
|-------|------|------|
| 命名規則 | L200 | snake_case / PascalCase の検出 |
| 未使用変数 | W100 | 使用されない変数の検出 |
| const推奨 | W200 | 変更されない変数への const 推奨 |
| ポインタ警告 | E300 | null参照、const違反 |
| ジェネリクス | E400 | 型引数の不一致 |
| Enum/Match | E500 | 網羅性チェック |

### cm fmt - コードフォーマット

一貫したコードスタイルに自動整形します。

```bash
# フォーマット結果を表示（ファイルは変更しない）
./cm fmt program.cm

# ファイルに直接書き込み
./cm fmt -w program.cm

# ディレクトリを再帰的にフォーマット
./cm fmt -w src/

# CIでフォーマットチェック（差分があれば失敗）
./cm fmt --check src/
```

**フォーマット例:**

```cm
// Before
int main(){
int x=10;
  int y   =  20;
if(x>y){
return x;
}
}

// After
int main() {
    int x = 10;
    int y = 20;
    if (x > y) {
        return x;
    }
}
```

**フォーマットルール:**

| ルール | 説明 |
|-------|------|
| K&Rスタイル | 開き括弧は同じ行 |
| 4スペースインデント | タブではなくスペース |
| 空行の正規化 | 連続する空行は1行に |
| 単一行ブロック保持 | 短い式は1行のまま |

> **詳細:** [Linter](../compiler/linter.html) / [Formatter](../compiler/formatter.html) チュートリアル

---

## 初めてのプログラム

### 1. ファイルを作成

```bash
cat > hello.cm << 'EOF'
int main() {
    println("Hello, Cm Language!");
    return 0;
}
EOF
```

### 2. 構文チェック

```bash
./cm check hello.cm
# ✅ No errors found.
```

### 3. JITで実行

```bash
./cm run hello.cm
# Hello, Cm Language!
```

### 4. ネイティブコンパイル

```bash
./cm compile hello.cm -o hello
./hello
# Hello, Cm Language!
```

### 5. WASMにコンパイル

```bash
./cm compile hello.cm --target=wasm -o hello.wasm
wasmtime hello.wasm
# Hello, Cm Language!
```

### 6. JSにコンパイル

```bash
./cm compile --target=js hello.cm -o hello.js
node hello.js
# Hello, Cm Language!
```

---

## トラブルシューティング

### ビルドエラー

#### CMakeが見つからない

```bash
cmake --version
# macOS
brew install cmake
# Ubuntu
sudo apt-get install cmake
```

#### LLVMが見つからない

```bash
# LLVM_DIR を指定
cmake -B build -DCM_USE_LLVM=ON \
  -DLLVM_DIR=/usr/lib/llvm-17/lib/cmake/llvm
```

#### C++20コンパイラエラー

```bash
# Clangを使用
export CXX=clang++
export CC=clang

# または GCC
export CXX=g++-12
export CC=gcc-12
```

### 実行エラー

#### cmコマンドが見つからない

```bash
# ./cm はシンボリックリンク: build/bin/cm を参照
ls -la ./cm

# パスを通す
export PATH="$PWD:$PATH"
```

#### OpenSSL関連エラー

OpenSSLはHTTPS通信のオプション依存です。未インストールでもビルドは成功しますが、HTTPSは無効になります。

```bash
# macOS
brew install openssl@3

# Ubuntu/Debian
sudo apt-get install libssl-dev
```

> CMakeの出力で `OpenSSL found: x.x.x` が表示されればHTTPSは有効です。  
> `OpenSSL not found - HTTPS support disabled` が表示されてもビルド自体は正常です。

#### GTestが見つからない（ユニットテスト）

```bash
# macOS
brew install googletest

# Ubuntu/Debian
sudo apt-get install libgtest-dev
```

> GTestが無くてもコンパイラのビルドとCmテスト(`make tip`)は実行可能です。  
> C++ユニットテスト(`make test`)のみスキップされます。

#### ライブラリが見つからない（LLVM）

```bash
# macOS
export DYLD_LIBRARY_PATH="/opt/homebrew/opt/llvm@17/lib:$DYLD_LIBRARY_PATH"

# Linux
export LD_LIBRARY_PATH="/usr/lib/llvm-17/lib:$LD_LIBRARY_PATH"
```

#### wasmtime実行エラー

```bash
# wasmtimeがインストールされているか確認
wasmtime --version

# パスが通っているか確認
which wasmtime
```

---

## エディタ設定

### VS Code（推奨）

Cm用VSCode拡張機能をインストールすることで、構文ハイライト、ファイルアイコン、Linter連携が利用できます。

#### 拡張機能のインストール

```bash
# 1. 依存関係のインストール
cd vscode-extension
pnpm install    # または npm install

# 2. パッケージビルド
pnpm run package

# 3. VSCodeにインストール
code --install-extension cm-language-*.vsix
```

> **Note**: Node.js v20+、pnpm（またはnpm）が必要です。

#### 拡張機能の機能

| 機能 | 説明 |
|------|------|
| **構文ハイライト** | キーワード、型、関数、文字列補間、プリプロセッサ等 |
| **ファイルアイコン** | `.cm`ファイルにCmアイコンを表示 |
| **言語設定** | ブラケットマッチング、折りたたみ、インデント支援 |

#### 拡張機能の開発（コントリビューター向け）

拡張機能はTypeScriptで開発され、ESLint + Prettierで品質管理されています。

```bash
cd vscode-extension
pnpm install

# TypeScriptコンパイル
pnpm run compile

# 静的解析（ESLint）
pnpm run lint

# コードフォーマットチェック（Prettier）
pnpm run format:check

# コードフォーマット自動修正
pnpm run format
```

| コマンド | 説明 |
|---------|------|
| `pnpm run compile` | TypeScriptコンパイル |
| `pnpm run lint` | ESLintチェック |
| `pnpm run lint:fix` | ESLint自動修正 |
| `pnpm run format` | Prettier自動フォーマット |
| `pnpm run format:check` | Prettierフォーマットチェック |
| `pnpm run verify-version` | バージョン一致確認 |

> **Tip**: CIでは `extension-lint` ジョブが自動的に compile → lint → format:check を実行します。

#### 拡張機能なしの場合

`.vscode/settings.json`:

```json
{
  "files.associations": {
    "*.cm": "cpp"
  },
  "editor.tabSize": 4,
  "editor.insertSpaces": true
}
```

### Vim

`.vimrc`:

```vim
au BufRead,BufNewFile *.cm set filetype=cpp
```

---

## よくある質問

### Q1: LLVMは必須ですか？

ほとんどの機能にLLVMが必要です。`cm run`（JIT実行）、`cm compile`（ネイティブ/WASM/JS）にはLLVMが必要です。`cm check`、`cm lint`、`cm fmt`はLLVMなしでも使用できます。

### Q2: GPUサポートに何が必要？

macOSでは**Xcode**（Objective-C++コンパイラ含む）と**Metal Framework**が必要です。CMakeが自動的に`std/gpu/gpu_runtime.mm`をObjective-C++としてコンパイルします。LinuxではGPUサポートは利用不可です。

### Q3: ARCHオプションはいつ使う？

クロスコンパイルや特定アーキテクチャ向けにビルドする場合に使用します。通常はデフォルトの自動検出で問題ありません。

```bash
# 明示的にARM64を指定
make build ARCH=arm64

# x86_64を指定（Intel Mac / Linux）
make build ARCH=x86_64
```

---

## 次のステップ

✅ 環境構築が完了した  
✅ コンパイラをビルドできた  
✅ makeコマンドを理解した  
✅ 構文チェックツールを試した  
✅ エディタを設定した（VSCode拡張機能）  
⏭️ 次は [Hello, World!](hello-world.html) で最初のプログラムを書きましょう

## 関連リンク

- [プロジェクト構造](../../PROJECT_STRUCTURE.html)
- [コンパイラの使い方](../compiler/usage.html)
- [Linter](../compiler/linter.html) - 静的解析ツール
- [Formatter](../compiler/formatter.html) - コードフォーマッター
- [JSバックエンド](../compiler/js-compilation.html) - JavaScript出力

---

**前の章:** [はじめに](introduction.html)  
**次の章:** [Hello, World!](hello-world.html)
---

**最終更新:** 2026-02-13
