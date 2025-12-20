# Cm言語 統合テストフレームワーク

## 概要

インタープリタとコンパイラの両方で同一のテストプログラムを実行し、結果の一貫性を保証するテストフレームワーク。

## テスト構造

```
tests/integration/
├── suite/                    # テストスイート
│   ├── basic/               # 基本機能
│   │   ├── hello_world.cm
│   │   ├── hello_world.expect
│   │   ├── arithmetic.cm
│   │   └── arithmetic.expect
│   ├── control_flow/        # 制御構造
│   │   ├── if_else.cm
│   │   ├── if_else.expect
│   │   ├── while_loop.cm
│   │   └── while_loop.expect
│   ├── functions/           # 関数
│   │   ├── simple_func.cm
│   │   └── simple_func.expect
│   └── errors/              # エラーケース
│       ├── type_error.cm
│       └── type_error.expect
├── runner.sh                # テストランナー
└── compare.py               # 結果比較ツール
```

## ファイル形式

### .cm ファイル（ソースコード）

```cm
// test_name.cm
int main() {
    int x = 42;
    println(x);
    return 0;
}
```

### .expect ファイル（期待される出力）

```
42
```

特殊な形式：
- `EXIT: 0` - 終了コード
- `ERROR: ...` - エラーメッセージ
- `COMPILE_ERROR: ...` - コンパイルエラー

## テストランナー仕様

### 基本動作

1. **インタープリタモード**
   ```bash
   ./cm --run test.cm > output.txt 2>&1
   echo "EXIT: $?" >> output.txt
   ```

2. **コンパイラモード**
   ```bash
   # Rustへトランスパイル
   ./cm --emit-rust test.cm -o test.rs
   rustc test.rs -o test.exe
   ./test.exe > output.txt 2>&1
   echo "EXIT: $?" >> output.txt
   ```

3. **結果比較**
   ```bash
   diff test.expect output.txt
   ```

### テストランナースクリプト

```bash
#!/bin/bash
# tests/integration/runner.sh

MODE=$1  # "interpreter" or "compiler"
SUITE=$2 # "basic", "control_flow", etc.

run_interpreter() {
    local test_file=$1
    local expect_file=${test_file%.cm}.expect
    local output_file=${test_file%.cm}.out

    ./cm --run "$test_file" > "$output_file" 2>&1
    echo "EXIT: $?" >> "$output_file"

    diff "$expect_file" "$output_file"
}

run_compiler() {
    local test_file=$1
    local expect_file=${test_file%.cm}.expect
    local output_file=${test_file%.cm}.out
    local rust_file=${test_file%.cm}.rs
    local exe_file=${test_file%.cm}.exe

    # Rustへトランスパイル
    ./cm --emit-rust "$test_file" -o "$rust_file"
    if [ $? -ne 0 ]; then
        echo "COMPILE_ERROR" > "$output_file"
    else
        rustc "$rust_file" -o "$exe_file"
        if [ $? -ne 0 ]; then
            echo "RUST_COMPILE_ERROR" > "$output_file"
        else
            "./$exe_file" > "$output_file" 2>&1
            echo "EXIT: $?" >> "$output_file"
        fi
    fi

    diff "$expect_file" "$output_file"
}
```

## テストケース設計

### レベル1: 基本機能（インタープリタ必須）

| テスト | 説明 | インタープリタ | コンパイラ |
|--------|------|--------------|-----------|
| hello_world.cm | 基本出力 | ✅ | 🔧 |
| arithmetic.cm | 四則演算 | ✅ | 🔧 |
| variables.cm | 変数宣言・代入 | ✅ | 🔧 |
| types.cm | 基本型 | ✅ | 🔧 |

### レベル2: 制御構造

| テスト | 説明 | インタープリタ | コンパイラ |
|--------|------|--------------|-----------|
| if_else.cm | 条件分岐 | ✅ | 🔧 |
| while_loop.cm | whileループ | ✅ | 🔧 |
| for_loop.cm | forループ | 🔧 | 🔧 |
| nested_control.cm | ネスト制御 | 🔧 | 🔧 |

### レベル3: 関数

| テスト | 説明 | インタープリタ | コンパイラ |
|--------|------|--------------|-----------|
| function_call.cm | 関数呼び出し | 🔧 | 🔧 |
| recursion.cm | 再帰 | 🔧 | 🔧 |
| overload.cm | オーバーロード | ❌ | 🔧 |

### レベル4: 高度な機能

| テスト | 説明 | インタープリタ | コンパイラ |
|--------|------|--------------|-----------|
| struct.cm | 構造体 | ❌ | 🔧 |
| generics.cm | ジェネリクス | ❌ | 🔧 |
| async.cm | 非同期処理 | ❌ | 🔧 |

凡例：
- ✅ 実装済み
- 🔧 実装予定
- ❌ 非対応（コンパイラのみ）

## CI統合

### GitHub Actions

```yaml
name: Integration Tests

on: [push, pull_request]

jobs:
  interpreter-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Build
        run: cmake -B build && cmake --build build
      - name: Run Interpreter Tests
        run: ./tests/integration/runner.sh interpreter all

  compiler-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Install Rust
        uses: actions-rs/toolchain@v1
      - name: Build
        run: cmake -B build && cmake --build build
      - name: Run Compiler Tests
        run: ./tests/integration/runner.sh compiler all
```

## 実装ステップ

1. **Phase 1**: インタープリタテスト基盤
   - [x] MIRインタープリタ実装
   - [ ] 基本テストケース作成
   - [ ] テストランナー（インタープリタモード）

2. **Phase 2**: コンパイラテスト基盤
   - [ ] Rustトランスパイラ最小実装
   - [ ] テストランナー（コンパイラモード）
   - [ ] 結果比較ツール

3. **Phase 3**: テスト拡充
   - [ ] 制御構造テスト
   - [ ] 関数テスト
   - [ ] エラーケーステスト

## 利点

1. **一貫性保証**: 同じソースコードで両実装を検証
2. **回帰テスト**: 新機能追加時の既存機能保護
3. **ドキュメント性**: expectファイルが仕様として機能
4. **段階的実装**: インタープリタ先行で早期フィードバック