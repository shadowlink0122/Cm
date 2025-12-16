# テストファイル組織化計画

## 1. 単体テスト（C++）

### tests/unit/lexer_test.cpp
```cpp
TEST(LexerTest, BasicTokens)           // 基本トークン
TEST(LexerTest, Keywords)               // キーワード認識
TEST(LexerTest, Operators)              // 演算子
TEST(LexerTest, StringLiterals)         // 文字列リテラル
TEST(LexerTest, NumericLiterals)        // 数値リテラル
TEST(LexerTest, Comments)               // コメント処理
TEST(LexerTest, PreprocessorDirectives) // #macro, #define
TEST(LexerTest, ErrorCases)             // エラーケース
```

### tests/unit/parser_test.cpp
```cpp
TEST(ParserTest, FunctionDeclaration)   // 関数定義
TEST(ParserTest, OverloadModifier)      // overload修飾子
TEST(ParserTest, GenericSyntax)         // <T: Trait>構文
TEST(ParserTest, ImplBlock)             // impl ブロック
TEST(ParserTest, StructDefinition)      // 構造体
TEST(ParserTest, TypedefDeclaration)    // typedef
TEST(ParserTest, MacroDefinition)       // #macro
TEST(ParserTest, ConditionalCompilation) // #if/#endif
```

### tests/unit/type_checker_test.cpp
```cpp
TEST(TypeCheckerTest, BasicTypes)       // 基本型
TEST(TypeCheckerTest, TypeAlias)        // typedef
TEST(TypeCheckerTest, UnionTypes)       // Ok(T) | Err(E)
TEST(TypeCheckerTest, GenericTypes)     // <T>
TEST(TypeCheckerTest, TraitBounds)      // <T: Trait>
TEST(TypeCheckerTest, OverloadResolution) // オーバーロード解決
```

## 2. リグレッションテスト（*.cm）

### tests/regression/stage1_basics/
```
001_hello_world.cm         // 最小プログラム
002_variables.cm           // 変数宣言
003_arithmetic.cm          // 算術演算
004_control_flow.cm        // if/while/for
005_functions.cm           // 関数定義と呼び出し
006_arrays.cm              // 配列
007_strings.cm             // 文字列操作
```

### tests/regression/stage2_types/
```
101_typedef_basic.cm       // typedef Int = int;
102_typedef_generic.cm     // typedef List<T> = Vec<T>;
103_union_types.cm         // Result<T> = Ok(T) | Err(string)
104_struct_basic.cm        // struct Point { x, y }
105_struct_generic.cm      // struct Vec<T> { }
```

### tests/regression/stage3_overload/
```
201_overload_functions.cm  // overload int add(int, int)
202_overload_constructors.cm // overload self(int)
203_overload_operators.cm  // operator+オーバーロード
204_overload_resolution.cm // 解決規則テスト
205_overload_errors.cm     // エラーケース
```

### tests/regression/stage4_generics/
```
301_generic_functions.cm   // <T> T identity(T x)
302_generic_constraints.cm // <T: Ord> T max(T, T)
303_generic_structs.cm     // struct Vec<T>
304_generic_impl.cm        // impl<T> Vec<T>
305_generic_specialization.cm // 特殊化
```

### tests/regression/stage5_impl/
```
401_impl_constructor.cm    // self()
402_impl_destructor.cm     // ~self()
403_impl_methods.cm        // impl for Interface
404_impl_overload.cm       // overload self(...)
405_impl_generic.cm        // impl<T> Type<T>
```

### tests/regression/stage6_macros/
```
501_define_constants.cm    // #define bool DEBUG = true
502_conditional_compile.cm // #if DEBUG ... #endif
503_macro_functions.cm     // #macro void LOG(msg)
504_macro_generic.cm       // #macro <T> T MIN(T, T)
505_test_bench.cm          // #test, #bench
```

### tests/regression/errors/
```
err_undefined_variable.cm  // 未定義変数
err_type_mismatch.cm       // 型不一致
err_overload_ambiguous.cm  // 曖昧なオーバーロード
err_missing_overload.cm    // overload修飾子なし
err_circular_typedef.cm    // 循環typedef
```

## 3. コード生成テスト

### tests/codegen/rust/
```
basic_types.cm → basic_types.rs
functions.cm → functions.rs
structs.cm → structs.rs
overloads.cm → overloads_mangled.rs
generics.cm → generics.rs
```

**検証方法:**
```bash
# Cmコードをトランスパイル
cm transpile --rust tests/codegen/rust/basic_types.cm

# 生成されたRustコードをコンパイル
rustc build/transpiled/rust/basic_types.rs

# 実行結果を比較
./basic_types | diff expected_output.txt -
```

### tests/codegen/typescript/
```
classes.cm → classes.ts
overloads.cm → overloads.ts
async_await.cm → async_await.ts
modules.cm → modules.ts
```

### tests/codegen/wasm/
```
arithmetic.cm → arithmetic.wasm
functions.cm → functions.wasm
memory.cm → memory.wasm
```

## 4. End-to-Endテスト

### tests/e2e/programs/
```
hello_world.cm             // 基本動作確認
fibonacci.cm               // 再帰関数
quicksort.cm               // ジェネリック配列操作
linked_list.cm             // ポインタ操作
async_http.cm              // 非同期処理
```

**実行スクリプト:**
```bash
#!/bin/bash
# tests/e2e/run_all.sh

for file in tests/e2e/programs/*.cm; do
    echo "Testing: $file"

    # コンパイルと実行
    cm run $file > output.txt

    # 期待結果と比較
    expected="${file%.cm}.expected"
    if diff output.txt "$expected" > /dev/null; then
        echo "  ✓ PASS"
    else
        echo "  ✗ FAIL"
        diff output.txt "$expected"
    fi
done
```

## 5. パフォーマンステスト

### tests/performance/
```
compile_time/
  ├── large_file.cm        // 10000行のファイル
  └── many_files/          // 1000個の小ファイル

runtime/
  ├── sorting_bench.cm     // ソートアルゴリズム
  ├── string_bench.cm      // 文字列操作
  └── memory_bench.cm      // メモリアロケーション
```

## テスト実行コマンド

```bash
# 全テスト実行
make test

# 段階別実行
ctest -L unit              # 単体テスト
ctest -L regression        # リグレッション
ctest -L codegen          # コード生成
ctest -L e2e              # E2E

# 個別実行
./build/tests/lexer_test
cm test tests/regression/stage1_basics/

# カバレッジ測定
make coverage
```

## CI設定（GitHub Actions）

```yaml
# .github/workflows/test.yml
name: Test Suite

on: [push, pull_request]

jobs:
  unit-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Build
        run: cmake -B build && cmake --build build
      - name: Run Unit Tests
        run: ctest --test-dir build -L unit

  regression-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Build Compiler
        run: cmake -B build && cmake --build build
      - name: Run Regression Tests
        run: |
          for stage in tests/regression/stage*; do
            echo "Testing $stage"
            ./build/bin/cm test "$stage"
          done

  codegen-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Install Rust
        uses: actions-rs/toolchain@v1
      - name: Install Node
        uses: actions/setup-node@v2
      - name: Test Code Generation
        run: make test-codegen
```

## テストカバレッジ目標

| コンポーネント | 目標 | 現在 |
|--------------|------|------|
| Lexer | 95% | ✅ 95% |
| Parser | 90% | 🔧 70% |
| Type Checker | 85% | 🔧 60% |
| HIR Lowering | 90% | ✅ 90% |
| MIR Lowering | 85% | ✅ 85% |
| Code Generation | 80% | ❌ 0% |

## テスト追加ガイドライン

新機能追加時：
1. まず失敗するテストを書く（TDD）
2. `tests/regression/` に実例を追加
3. `tests/unit/` にユニットテストを追加
4. エラーケースも必ずテスト
5. ドキュメントにテスト例を記載