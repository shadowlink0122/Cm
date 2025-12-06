# Cm言語テストシステム

## 概要

Cm言語の統一テストシステムです。同一のテストプログラム（`.cm`ファイル）を使用して、インタープリタと複数のコンパイラバックエンド（Rust/TypeScript/WASM）の動作を検証します。

## ディレクトリ構造

```
tests/
├── test_programs/      # 共通テストプログラム
│   ├── basic/         # 基本機能
│   ├── control_flow/  # 制御構造
│   ├── functions/     # 関数（将来）
│   ├── overload/      # オーバーロード（将来）
│   ├── types/         # 型システム（将来）
│   └── errors/        # エラーケース（将来）
│
├── runners/           # テストランナー
│   ├── test_runner.sh    # 統一テストランナー
│   ├── regression.sh     # リグレッションテスト
│   └── integration.sh    # 統合テスト
│
├── unit/              # C++ユニットテスト
├── run_tests.sh       # メインテストスクリプト
├── test_config.yaml   # テスト設定
└── README.md          # このファイル
```

## クイックスタート

```bash
# 基本的なテストを実行（推奨）
./tests/run_tests.sh quick

# 全テストを実行
./tests/run_tests.sh full

# リグレッションテスト
./tests/run_tests.sh regression
```

## テストランナーの使い方

### 1. 統一テストランナー (`test_runner.sh`)

複数のバックエンドでテストを実行：

```bash
# インタープリタでテスト
./tests/runners/test_runner.sh --backend=interpreter --suite=basic

# Rustバックエンドでテスト（実装後）
./tests/runners/test_runner.sh --backend=rust --suite=all

# TypeScriptバックエンドでテスト（実装後）
./tests/runners/test_runner.sh --backend=typescript --suite=basic

# 全バックエンドでテスト
./tests/runners/test_runner.sh --backend=all --suite=basic

# 特定のテストファイルを実行
./tests/runners/test_runner.sh --backend=interpreter basic/hello_world.cm

# 詳細出力
./tests/runners/test_runner.sh --backend=interpreter --suite=basic --verbose
```

### 2. リグレッションテスト (`regression.sh`)

前回の実行結果と比較：

```bash
# リグレッションテスト実行
./tests/runners/regression.sh

# ベースラインを保存
./tests/runners/regression.sh --save-baseline

# ベースラインと比較
./tests/runners/regression.sh --compare
```

### 3. 統合テスト (`integration.sh`)

複数バックエンド間の一貫性を検証：

```bash
# インタープリタとRustの比較（Rust実装後）
./tests/runners/integration.sh --backends=interpreter,rust

# 厳密モード（完全一致を要求）
./tests/runners/integration.sh --backends=interpreter,rust --strict
```

## テストプログラムの形式

### `.cm` ファイル（ソースコード）

```cm
// tests/test_programs/basic/example.cm
int main() {
    int x = 42;
    println(x);
    return 0;
}
```

### `.expect` ファイル（期待される出力）

```
42
EXIT: 0
```

特殊な形式：
- `EXIT: <code>` - プログラムの終了コード
- `ERROR: <message>` - エラーメッセージ
- `COMPILE_ERROR: <message>` - コンパイルエラー

## バックエンドのサポート状況

| バックエンド | 状態 | コマンド |
|------------|------|----------|
| Interpreter | ✅ 実装済み | `cm --run` |
| Rust | 🔧 計画中 | `cm --emit-rust` |
| TypeScript | 🔧 計画中 | `cm --emit-typescript` |
| WASM | 🔧 計画中 | `cm --emit-wasm` |

## テストスイート

| スイート | 説明 | テスト数 | 状態 |
|---------|------|---------|------|
| basic | 基本機能（変数、演算） | 3 | ✅ |
| control_flow | 制御構造（if、while） | 2 | ✅ |
| functions | 関数定義・呼び出し | 0 | 🔧 |
| overload | オーバーロード | 0 | 🔧 |
| types | 構造体、ジェネリクス | 0 | 🔧 |
| errors | エラーケース | 0 | 🔧 |

## CI/CD統合

### GitHub Actions

`.github/workflows/test.yml`:

```yaml
name: Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Build
        run: cmake -B build && cmake --build build
      - name: Run Tests
        run: ./tests/run_tests.sh all
```

## 新しいテストの追加方法

1. テストプログラムを作成：
   ```bash
   vim tests/test_programs/basic/new_test.cm
   ```

2. 期待される出力を定義：
   ```bash
   vim tests/test_programs/basic/new_test.expect
   ```

3. テストを実行：
   ```bash
   ./tests/runners/test_runner.sh --backend=interpreter basic/new_test.cm
   ```

## トラブルシューティング

### テストが失敗する場合

```bash
# 詳細出力を有効化
./tests/runners/test_runner.sh --backend=interpreter --suite=basic --verbose

# 生成ファイルを保持
./tests/runners/test_runner.sh --backend=interpreter --suite=basic --keep-artifacts

# 出力ディレクトリを確認
ls -la tests/runners/.tmp/
```

### バックエンドが動作しない場合

```bash
# Rustバックエンドの場合
rustc --version  # Rustがインストールされているか確認

# TypeScriptバックエンドの場合
tsc --version    # TypeScriptがインストールされているか確認
node --version   # Node.jsがインストールされているか確認

# WASMバックエンドの場合
wasmtime --version  # wasmtimeがインストールされているか確認
```

## 開発ガイドライン

1. **テストファースト**: 新機能を実装する前にテストを書く
2. **共通テスト**: 全バックエンドで同じテストを使用する
3. **期待値の明確化**: `.expect`ファイルで期待する出力を明確に定義
4. **段階的実装**: インタープリタ → Rust → TypeScript → WASM の順で実装

## 関連ドキュメント

- [統一テスト構造設計](../docs/design/unified_test_structure.md)
- [テストフレームワーク設計](../docs/design/test_framework.md)
- [MIRインタープリタ](../docs/MIR_INTERPRETER_SUMMARY.md)