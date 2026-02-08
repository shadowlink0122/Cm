---
title: 環境構築
parent: Tutorials
---

[English](../../en/basics/setup.html)

# 環境構築

**難易度:** 🟢 初級  
**所要時間:** 30分

## 📚 この章で学ぶこと

- 必要な環境
- コンパイラのビルド方法
- テストの実行
- 初めてのプログラム実行

---

## 必要要件

### 必須

- **C++20コンパイラ**
  - Clang 17+ （推奨）
  - GCC 12+
  - MSVC 19.30+（Windows）

- **CMake 3.20+**
  - ビルドシステム

- **Git**
  - ソースコード取得

### オプション

- **LLVM 17**
  - ネイティブコンパイルに必要
  - インタプリタのみならLLVMなしでも可

- **Emscripten**
  - WASMコンパイルに必要（将来）

- **wasmtime**
  - WASMバイナリの実行に必要

---

## インストール手順

### macOS

```bash
# Homebrewでインストール
brew install cmake llvm@17

# Clangのパスを設定
export PATH="/opt/homebrew/opt/llvm@17/bin:$PATH"
```

### Ubuntu/Debian

```bash
# 必須パッケージ

# LLVM（オプション）
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
```

### Windows

```powershell
# Chocolateyでインストール
choco install cmake git llvm

# または Visual Studio 2022をインストール
```

---

## コンパイラのビルド

### 1. リポジトリのクローン

```bash
cd Cm
```

### 2. LLVMなしでビルド（インタプリタのみ）

```bash
cmake -B build
cmake --build build
```

### 3. LLVM有効ビルド（推奨）

```bash
cmake -B build -DCM_USE_LLVM=ON
cmake --build build

# 並列ビルド（高速化）
cmake --build build -j8
```

### 4. ビルド確認

```bash
./build/bin/cm --version
# Cm Language Compiler v0.10.0
```

---

## テストの実行

### すべてのテストを実行

```bash
cd build
ctest
```

### 特定のテストを実行

```bash
# C++ユニットテスト
ctest -R test_lexer
ctest -R test_parser

# Cmプログラムテスト
./bin/cm run ../tests/test_programs/basic/hello.cm
```

### LLVMテスト

```bash
# LLVMバックエンドのテスト
make test-llvm-all

# 特定の機能
make test-llvm-arrays
make test-llvm-pointers
```

---

## 初めてのプログラム

### 1. ファイルを作成

```bash
cat > hello.cm << 'EOF'
    println("Hello, Cm Language!");
    return 0;
}
```

### 2. インタプリタで実行

```bash
./build/bin/cm run hello.cm
# Hello, Cm Language!
```

### 3. LLVMでコンパイル

```bash
# コンパイル
./build/bin/cm compile hello.cm -o hello

# 実行
./hello
# Hello, Cm Language!
```

### 4. WASMにコンパイル

```bash
# WASMコンパイル
./build/bin/cm compile hello.cm --target=wasm -o hello.wasm

# wasmtimeで実行
wasmtime hello.wasm
# Hello, Cm Language!
```

---

## トラブルシューティング

### ビルドエラー

#### CMakeが見つからない

```bash
# バージョン確認
cmake --version

# 最新版をインストール
# macOS
brew install cmake

# Ubuntu
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
# パスを通す
export PATH="$PWD/build/bin:$PATH"

# または絶対パスで実行
/path/to/Cm/build/bin/cm run hello.cm
```

#### ライブラリが見つからない（LLVM）

```bash
# macOS
export DYLD_LIBRARY_PATH="/opt/homebrew/opt/llvm@17/lib:$DYLD_LIBRARY_PATH"

# Linux
export LD_LIBRARY_PATH="/usr/lib/llvm-17/lib:$LD_LIBRARY_PATH"
```

---

## エディタ設定

### VS Code

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

### Q1: インタプリタとLLVMの違いは？

**インタプリタ**
- ✅ ビルドが速い（LLVMなし）
- ✅ デバッグが簡単
- ❌ 実行が遅い

**LLVM**
- ✅ 実行が高速
- ✅ 最適化が効く
- ❌ ビルドに時間がかかる

### Q2: WASMはどう使う？

WebAssemblyにコンパイルすることで：
- ブラウザで実行可能
- サーバーレス環境で実行
- サンドボックス環境

### Q3: デバッグモードは？

```bash
# デバッグ情報付きビルド
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# 実行時デバッグ
./build/bin/cm run hello.cm --debug
```

---

## 次のステップ

✅ 環境構築が完了した  
✅ コンパイラをビルドできた  
✅ テストが実行できた  
⏭️ 次は [Hello, World!](hello-world.html) で最初のプログラムを書きましょう

## 関連リンク

- [プロジェクト構造](../../PROJECT_STRUCTURE.html)
- [コンパイラの使い方](../compiler/usage.html)

---

**前の章:** [はじめに](introduction.html)  
**次の章:** [Hello, World!](hello-world.html)
---

**最終更新:** 2026-02-08
