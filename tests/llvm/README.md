# LLVM バックエンドテスト

## 概要
Cm言語のLLVMバックエンド用テストスイートです。3つのターゲット（native、wasm、baremetal）に対応しています。

## ディレクトリ構造
```
tests/llvm/
├── basic/          # 基本構文テスト（全ターゲット共通）
│   ├── hello_world.cm
│   ├── arithmetic.cm
│   └── control_flow.cm
├── baremetal/      # ベアメタル専用テスト
│   └── no_std.cm
├── wasm/           # WebAssembly専用テスト
│   └── wasm_test.cm
└── run_tests.sh    # スタンドアロンテストスクリプト
```

## テスト実行方法

### Makefileから実行（推奨）

```bash
# プロジェクトルートから
make test-llvm           # ネイティブテスト（既存テストケース使用）
make test-llvm-wasm      # WebAssemblyテスト
make test-llvm-baremetal # ベアメタルテスト
make test-llvm-all       # すべてのLLVMテスト

# ショートカット
make tl   # test-llvm
make tla  # test-llvm-all
```

### 統一テストランナー使用

```bash
# ネイティブターゲット（既存のテストケースを実行）
./tests/unified_test_runner.sh -b llvm

# カテゴリ指定
./tests/unified_test_runner.sh -b llvm -c basic,control_flow

# 詳細出力
./tests/unified_test_runner.sh -b llvm -v
```

### スタンドアロンスクリプト

```bash
cd tests/llvm
./run_tests.sh
```

## ターゲット詳細

### 1. Native（ネイティブ）
- **用途**: デスクトップアプリケーション、CLIツール
- **特徴**: OS機能利用可能、標準ライブラリあり
- **コマンド例**:
  ```bash
  cm compile program.cm --backend=llvm --target=native -o program
  ./program
  ```

### 2. WebAssembly（WASM）
- **用途**: Webブラウザ、Node.js、Cloudflare Workers
- **特徴**: サンドボックス実行、サイズ最適化
- **コマンド例**:
  ```bash
  cm compile app.cm --backend=llvm --target=wasm32 -o app.wasm
  # ブラウザまたはNode.jsで実行
  ```
- **検証**: `wasm-validate`がインストールされていれば自動検証

### 3. Baremetal（ベアメタル）
- **用途**: 組み込みシステム、OS開発、マイコン
- **特徴**: no_std、カスタムアロケータ、リンカスクリプト生成
- **ターゲット**: ARM Cortex-M（thumbv7m-none-eabi）
- **コマンド例**:
  ```bash
  cm compile kernel.cm --backend=llvm --target=baremetal-arm --no-std -o kernel.o
  # link.ldも自動生成される
  arm-none-eabi-ld -T link.ld kernel.o -o kernel.elf
  ```

## テストケース作成

### ネイティブテスト
既存の`tests/test_programs/`にあるテストケースが自動的に使用されます。

### WASM専用テスト
```cm
// wasm/my_test.cm
[[wasm_export]]
int add(int a, int b) {
    return a + b;
}
```

### ベアメタル専用テスト
```cm
// baremetal/my_test.cm
#![no_std]
#![no_main]

[[noreturn]]
void panic(const char* msg) {
    // パニック実装
}

extern "C" void _start() {
    // エントリーポイント
}
```

## トラブルシューティング

### LLVMが見つからない
```bash
# macOS
brew install llvm@17

# Linux
apt install llvm-17

# CMake設定
cmake -B build -DCM_USE_LLVM=ON
```

### WASMテストが失敗する
```bash
# wasm-validateをインストール
npm install -g wasm-validate
```

### ベアメタルリンクエラー
```bash
# ARM toolchainをインストール
# macOS
brew install arm-none-eabi-gcc

# Linux
apt install gcc-arm-none-eabi
```

## 最適化レベル

テスト時の最適化レベルを変更できます：

```bash
cm compile test.cm --backend=llvm -O0  # デバッグ（最適化なし）
cm compile test.cm --backend=llvm -O1  # 基本最適化
cm compile test.cm --backend=llvm -O2  # 標準最適化
cm compile test.cm --backend=llvm -O3  # アグレッシブ最適化
cm compile test.cm --backend=llvm -Os  # サイズ優先
cm compile test.cm --backend=llvm -Oz  # 極小サイズ
```

## CI/CD統合

GitHub Actionsでの例：

```yaml
- name: Test LLVM Backend
  run: |
    make build
    make test-llvm-all
```