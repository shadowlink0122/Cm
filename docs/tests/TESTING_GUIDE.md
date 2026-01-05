# Cm言語 テストガイド

このドキュメントは、Cm言語コンパイラのテスト作成と実行方法について説明します。

## 目次
1. [テストの概要](#テストの概要)
2. [テストディレクトリ構造](#テストディレクトリ構造)
3. [テストファイルの種類](#テストファイルの種類)
4. [テストの作成方法](#テストの作成方法)
5. [テストの実行方法](#テストの実行方法)
6. [並列実行](#並列実行)
7. [バックエンド別の考慮事項](#バックエンド別の考慮事項)
8. [ベストプラクティス](#ベストプラクティス)

## テストの概要

Cm言語コンパイラは3つの主要なバックエンドをサポートしています：
- **インタプリタ** - MIRを直接実行
- **LLVM** - ネイティブコードにコンパイル
- **WASM** - WebAssemblyにコンパイル

各バックエンドには固有の制限があり、適切にテストを設計する必要があります。

## テストディレクトリ構造

```
tests/
├── test_programs/          # Cmテストプログラム
│   ├── basic/              # 基本機能
│   ├── array/              # 配列機能
│   ├── asm/                # インラインアセンブリ
│   ├── control_flow/       # 制御フロー
│   ├── defer/              # defer文
│   ├── errors/             # エラーケース
│   ├── formatting/         # フォーマット文字列
│   ├── function_ptr/       # 関数ポインタ
│   ├── functions/          # 関数機能
│   ├── generics/           # ジェネリクス
│   ├── interface/          # インターフェース
│   ├── match/              # match式
│   ├── memory/             # メモリ管理
│   ├── modules/            # モジュールシステム
│   ├── modules_v4/         # v4モジュールシステム
│   ├── pointer/            # ポインタ操作
│   ├── strings/            # 文字列操作
│   └── with/               # with自動実装
├── unified_test_runner.sh  # 統合テストランナー
└── regression/             # 回帰テスト

```

### テストカテゴリの新規作成

新機能を実装する際は、`tests/test_programs/`以下に専用のディレクトリを作成します：

```bash
mkdir tests/test_programs/新機能名
```

## テストファイルの種類

各テストは以下のファイルで構成されます：

### 1. ソースファイル（必須）
- **`test_name.cm`** - テスト対象のCmプログラム

### 2. 期待値ファイル（いずれか1つ）
- **`test_name.expect`** - 正常実行時の期待される出力
- **`test_name.error`** - コンパイルエラーを期待する場合（優先）

### 3. スキップファイル（オプション）
- **`.skip`** - カテゴリ全体を特定のバックエンドでスキップする場合（ディレクトリ直下）
- **`test_name.skip`** - 個別のテストを特定のバックエンドでスキップする場合

### 4. バックエンド固有の期待値（オプション）
- **`test_name.expect.llvm`** - LLVM固有の期待値
- **`test_name.expect.llvm-wasm`** - WASM固有の期待値
- **`test_name.expect.interpreter`** - インタプリタ固有の期待値

## テストの作成方法

### 基本的なテスト

#### 1. 正常実行テスト

`tests/test_programs/basic/hello.cm`:
```cm
int main() {
    println("Hello, World!");
    return 0;
}
```

`tests/test_programs/basic/hello.expect`:
```
Hello, World!
```

#### 2. エラーテスト

`tests/test_programs/errors/type_mismatch.cm`:
```cm
int main() {
    int x = "string";  // 型エラー
    return 0;
}
```

`tests/test_programs/errors/type_mismatch.error`:
```
（空ファイル - エラーが発生することを期待）
```

#### 3. カテゴリ全体のスキップ

インラインアセンブリは、インタプリタとWASMでは実行できないため、カテゴリ全体をスキップします。

`tests/test_programs/asm/.skip`:
```
interpreter
llvm-wasm
```

これにより、`asm/` ディレクトリ内のすべてのテストが指定されたバックエンドでスキップされます。

#### 4. 個別テストのスキップ

特定のテストだけをスキップする場合：

`tests/test_programs/modules/test_selective_import.skip`:
```
llvm
llvm-wasm
interpreter
```

### 複雑なテストケース

#### フォーマット文字列テスト

`tests/test_programs/formatting/interpolation.cm`:
```cm
int main() {
    int x = 42;
    float pi = 3.14159;
    println(f"x = {x}, pi = {pi:.2f}");
    return 0;
}
```

`tests/test_programs/formatting/interpolation.expect`:
```
x = 42, pi = 3.14
```

#### ジェネリクステスト

`tests/test_programs/generics/basic_generic.cm`:
```cm
<T> T identity(T value) {
    return value;
}

int main() {
    println(f"{identity(42)}");
    println(f"{identity(3.14)}");
    return 0;
}
```

`tests/test_programs/generics/basic_generic.expect`:
```
42
3.14
```

## テストの実行方法

### 単一バックエンドのテスト

```bash
# インタプリタのみ
make test-interpreter

# LLVM（ネイティブ）のみ
make test-llvm

# WASM のみ
make test-llvm-wasm

# すべてのLLVMバックエンド（ネイティブ + WASM）
make test-llvm-all
```

### 特定のテストディレクトリのみ実行

```bash
# 基本テストのみ
./tests/unified_test_runner.sh -b llvm tests/test_programs/basic/*.cm

# ジェネリクステストのみ
./tests/unified_test_runner.sh -b interpreter tests/test_programs/generics/*.cm
```

### 個別テストの実行

```bash
# 直接実行（インタプリタ）
./build/bin/cm run tests/test_programs/basic/hello.cm

# コンパイルして実行（LLVM）
./build/bin/cm compile tests/test_programs/basic/hello.cm -o hello
./hello

# WASMにコンパイル
./build/bin/cm compile --target=wasm tests/test_programs/basic/hello.cm -o hello.wasm
wasmtime hello.wasm
```

## 並列実行

テストの実行時間を短縮するため、並列実行を推奨します。

### Makefileによる並列実行

```bash
# 8並列でLLVMテストを実行
make test-llvm-parallel -j8

# すべてのバックエンドを並列実行
make test-all-parallel -j8
```

### unified_test_runnerの並列オプション

```bash
# 並列実行を有効化（CPUコア数に基づいて自動調整）
./tests/unified_test_runner.sh -p -b llvm

# 並列数を指定
./tests/unified_test_runner.sh -j 4 -b interpreter
```

### 並列実行時の注意点

1. **一時ファイルの衝突を避ける**
   - 各テストは独自の一時ディレクトリを使用
   - テスト名にユニークなIDを付与

2. **出力の順序**
   - 並列実行時は出力順序が不定
   - 失敗したテストはまとめて最後に表示

3. **リソース制限**
   - メモリ使用量に注意
   - I/O集約的なテストは並列度を下げる

## バックエンド別の考慮事項

### インタプリタ
- **利点**: 最も高速に起動、デバッグが容易
- **制限**:
  - インラインアセンブリ未対応
  - 一部の最適化機能なし
  - extern関数の制限

### LLVM（ネイティブ）
- **利点**: 最高のパフォーマンス、完全な機能サポート
- **制限**:
  - コンパイル時間が長い
  - プラットフォーム依存

### WASM
- **利点**: ポータブル、サンドボックス実行
- **制限**:
  - インラインアセンブリ未対応
  - 浮動小数点の出力形式が異なる場合あり
  - ファイルI/Oの制限

## ベストプラクティス

### 1. テスト命名規則
- 説明的な名前を使用: `test_array_bounds_check.cm`
- カテゴリ別にプレフィックスを付ける: `err_null_pointer.cm`

### 2. 期待値ファイルの管理
```bash
# 期待値の自動生成（慎重に使用）
./build/bin/cm run test.cm > test.expect

# バックエンド固有の期待値が必要な場合
./build/bin/cm run test.cm > test.expect.interpreter
./build/bin/cm compile test.cm -o test && ./test > test.expect.llvm
```

### 3. エラーメッセージのテスト
エラーメッセージの詳細は変更される可能性があるため、`.error`ファイルは空にして、エラーの発生のみを確認することを推奨。

### 4. テストの独立性
- 各テストは独立して実行可能であること
- 外部ファイルへの依存を避ける
- グローバル状態を変更しない

### 5. カバレッジの確保
新機能を追加する際は、以下をカバーするテストを作成：
- 正常ケース
- エッジケース
- エラーケース
- 他の機能との組み合わせ

### 6. 回帰テストの追加
バグ修正時は、必ず回帰テストを追加：
```bash
# tests/regression/issue_123.cm
// Regression test for issue #123: null pointer dereference
int main() {
    int* p = null;
    // This should error at compile time
    int x = *p;
    return 0;
}
```

## トラブルシューティング

### テストが失敗する場合

1. **期待値の確認**
```bash
# 実際の出力を確認
./build/bin/cm run failing_test.cm
```

2. **デバッグモードで実行**
```bash
./build/bin/cm run --debug failing_test.cm
```

3. **バックエンド固有の問題**
```bash
# 特定のバックエンドでのみ失敗する場合
./tests/unified_test_runner.sh -b llvm failing_test.cm -v
```

### パフォーマンスの問題

1. **タイムアウト設定**
```bash
# タイムアウトを10秒に設定
TIMEOUT=10 ./tests/unified_test_runner.sh -b llvm
```

2. **並列度の調整**
```bash
# CPUコア数の半分で実行
make test-llvm-parallel -j$(expr $(nproc) / 2)
```

## まとめ

Cm言語のテストシステムは、複数のバックエンドをサポートし、包括的なテストカバレッジを提供します。並列実行を活用することで、効率的にテストを実行できます。新機能の追加時は、このガイドに従って適切なテストを作成してください。