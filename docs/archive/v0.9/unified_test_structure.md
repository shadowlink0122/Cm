[English](unified_test_structure.en.html)

# Cm言語 統一テスト構造

## 新しいディレクトリ構造

```
tests/
├── test_programs/           # 共通テストプログラム（.cm + .expect）
│   ├── basic/
│   │   ├── hello_world.cm
│   │   ├── hello_world.expect
│   │   ├── arithmetic.cm
│   │   ├── arithmetic.expect
│   │   ├── variables.cm
│   │   └── variables.expect
│   ├── control_flow/
│   │   ├── if_else.cm
│   │   ├── if_else.expect
│   │   ├── while_loop.cm
│   │   ├── while_loop.expect
│   │   ├── for_loop.cm
│   │   └── for_loop.expect
│   ├── functions/
│   │   ├── basic_func.cm
│   │   ├── basic_func.expect
│   │   ├── recursion.cm
│   │   └── recursion.expect
│   ├── overload/
│   │   ├── overload_func.cm
│   │   └── overload_func.expect
│   ├── types/
│   │   ├── struct.cm
│   │   ├── struct.expect
│   │   ├── typedef.cm
│   │   └── typedef.expect
│   └── errors/
│       ├── type_error.cm
│       ├── type_error.expect
│       ├── syntax_error.cm
│       └── syntax_error.expect
│
├── runners/                 # テストランナー
│   ├── test_runner.sh      # 統一テストランナー
│   ├── regression.sh       # リグレッションテスト
│   └── integration.sh      # 統合テスト
│
├── unit/                    # C++ユニットテスト（既存）
├── regression/              # リグレッション固有の設定
└── integration/             # 統合テスト固有の設定
```

## 統一テストランナー仕様

### コマンドライン

```bash
# 基本構文
./test_runner.sh [OPTIONS] [TEST_PATTERN]

# オプション
--backend=BACKEND    # 実行バックエンド (interpreter|rust|typescript|wasm|all)
--suite=SUITE        # テストスイート (basic|control_flow|functions|all)
--mode=MODE          # テストモード (quick|full|regression)
--output=DIR         # 出力ディレクトリ
--verbose            # 詳細出力
--keep-artifacts     # 生成ファイルを保持

# 例
./test_runner.sh --backend=interpreter --suite=basic
./test_runner.sh --backend=rust --suite=all
./test_runner.sh --backend=all --mode=regression
./test_runner.sh --backend=typescript basic/hello_world.cm
```

### バックエンド実行フロー

#### 1. Interpreter Backend
```bash
cm --run test.cm > output.txt 2>&1
echo "EXIT: $?" >> output.txt
diff test.expect output.txt
```

#### 2. Rust Backend
```bash
cm --emit-rust test.cm -o test.rs
rustc test.rs -o test_rust
./test_rust > output.txt 2>&1
echo "EXIT: $?" >> output.txt
diff test.expect output.txt
```

#### 3. TypeScript Backend
```bash
cm --emit-typescript test.cm -o test.ts
tsc test.ts --outFile test.js
node test.js > output.txt 2>&1
echo "EXIT: $?" >> output.txt
diff test.expect output.txt
```

#### 4. WASM Backend
```bash
cm --emit-wasm test.cm -o test.wasm
wasmtime test.wasm > output.txt 2>&1
echo "EXIT: $?" >> output.txt
diff test.expect output.txt
```

## バックエンド互換性マトリックス

| 機能 | Interpreter | Rust | TypeScript | WASM |
|------|------------|------|------------|------|
| 基本演算 | ✅ | 🔧 | 🔧 | 🔧 |
| 制御フロー | ✅ | 🔧 | 🔧 | 🔧 |
| 関数 | 🔧 | 🔧 | 🔧 | 🔧 |
| オーバーロード | ❌ | 🔧 | 🔧 | ❌ |
| ジェネリクス | ❌ | 🔧 | 🔧 | ❌ |
| 構造体 | ❌ | 🔧 | 🔧 | 🔧 |
| async/await | ❌ | 🔧 | 🔧 | ❌ |

## テスト設定ファイル

### test_config.yaml
```yaml
# tests/test_config.yaml
backends:
  interpreter:
    enabled: true
    command: "cm --run"
    file_extension: ""

  rust:
    enabled: false  # 実装後にtrue
    command: "cm --emit-rust"
    compiler: "rustc"
    file_extension: ".rs"

  typescript:
    enabled: false
    command: "cm --emit-typescript"
    compiler: "tsc"
    runtime: "node"
    file_extension: ".ts"

  wasm:
    enabled: false
    command: "cm --emit-wasm"
    runtime: "wasmtime"
    file_extension: ".wasm"

test_suites:
  basic:
    description: "基本機能テスト"
    backends: [interpreter, rust, typescript, wasm]

  control_flow:
    description: "制御フローテスト"
    backends: [interpreter, rust, typescript, wasm]

  functions:
    description: "関数テスト"
    backends: [rust, typescript, wasm]

  overload:
    description: "オーバーロードテスト"
    backends: [rust, typescript]

  types:
    description: "型システムテスト"
    backends: [rust, typescript, wasm]
```

## 期待結果フォーマット拡張

### 基本フォーマット（.expect）
```
出力行1
出力行2
EXIT: 0
```

### 条件付き期待結果（.expect.yaml）
```yaml
# バックエンド固有の期待結果を定義可能
default:
  output: |
    Hello, World!
  exit_code: 0

backends:
  typescript:
    output: |
      Hello, World!
      undefined  # TypeScript特有の出力
    exit_code: 0

  wasm:
    # WASM固有の制限事項
    skip: true
    reason: "WASM doesn't support println yet"
```

## CI/CD統合

### GitHub Actions Workflow
```yaml
name: All Backend Tests

on: [push, pull_request]

strategy:
  matrix:
    backend: [interpreter, rust, typescript, wasm]
    suite: [basic, control_flow, functions, types]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2

      - name: Setup Environment
        run: |
          # バックエンド固有のセットアップ
          case ${{ matrix.backend }} in
            rust)
              curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
              ;;
            typescript)
              npm install -g typescript
              ;;
            wasm)
              curl https://wasmtime.dev/install.sh -sSf | bash
              ;;
          esac

      - name: Build Compiler
        run: |
          cmake -B build
          cmake --build build

      - name: Run Tests
        run: |
          ./tests/runners/test_runner.sh \
            --backend=${{ matrix.backend }} \
            --suite=${{ matrix.suite }}
```

## リグレッションテストとの統合

```bash
# regression.sh
#!/bin/bash
# 全バックエンドでリグレッションテストを実行

BACKENDS="interpreter rust typescript wasm"
FAILED=0

for backend in $BACKENDS; do
    echo "Running regression tests for $backend..."
    ./test_runner.sh --backend=$backend --mode=regression
    if [ $? -ne 0 ]; then
        echo "Regression failed for $backend"
        FAILED=1
    fi
done

exit $FAILED
```

## 利点

1. **単一ソース**: すべてのテストタイプで同じ.cm/.expectファイルを使用
2. **柔軟性**: バックエンドごとに異なる実行方法をサポート
3. **段階的実装**: バックエンドを個別に有効/無効化可能
4. **CI対応**: マトリックステストで全組み合わせを自動検証
5. **拡張性**: 新しいバックエンドの追加が容易