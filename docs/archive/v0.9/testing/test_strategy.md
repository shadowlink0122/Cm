[English](test_strategy.en.html)

# テスト戦略

## 概要

Cmコンパイラの品質を担保するための包括的なテスト戦略です。

## テスト種別

| 種別 | 対象 | 目的 | 実行タイミング |
|------|------|------|---------------|
| **単体テスト** | 各コンポーネント | 関数・クラス単位の動作確認 | 毎コミット |
| **結合テスト** | フェーズ間連携 | パイプラインの正常動作 | 毎コミット |
| **E2Eテスト** | 全体 | .cm → 実行結果の検証 | 毎コミット |
| **リグレッションテスト** | 既知バグ | 修正済みバグの再発防止 | 毎コミット |
| **デバッグモードテスト** | ログ出力 | DRY原則、ログ形式確認 | 毎コミット |

## ディレクトリ構造

```
tests/
├── unit/                    # 単体テスト
│   ├── lexer/
│   │   ├── token_test.cpp
│   │   └── lexer_test.cpp
│   ├── parser/
│   │   └── parser_test.cpp
│   ├── type_check/
│   │   └── checker_test.cpp
│   └── hir/
│       └── hir_test.cpp
├── integration/             # 結合テスト
│   ├── lexer_parser_test.cpp
│   ├── parser_hir_test.cpp
│   └── full_pipeline_test.cpp
├── e2e/                     # E2Eテスト
│   ├── interpreter/
│   │   ├── hello_world.cm
│   │   └── hello_world.expected
│   ├── codegen/
│   │   └── rust/
│   ├── run_all.sh
│   └── test_runner.py
├── regression/              # リグレッションテスト
│   ├── issues/
│   │   ├── issue_001.cm
│   │   └── issue_001.expected
│   └── run_regression.sh
└── debug/                   # デバッグモードテスト
    ├── log_format_test.cpp
    └── dry_check_test.cpp
```

## 単体テスト

### フレームワーク

Google Test (gtest) を使用。

```cpp
// tests/unit/lexer/lexer_test.cpp
#include <gtest/gtest.h>
#include "frontend/lexer/lexer.hpp"

TEST(LexerTest, TokenizeSimpleFunction) {
    cm::Lexer lexer("fn main() {}");
    auto tokens = lexer.tokenize();
    
    ASSERT_EQ(tokens.size(), 6);
    EXPECT_EQ(tokens[0].kind, cm::TokenKind::Fn);
    EXPECT_EQ(tokens[1].kind, cm::TokenKind::Ident);
    EXPECT_EQ(tokens[1].text, "main");
}

TEST(LexerTest, HandleStringLiteral) {
    cm::Lexer lexer(R"("hello")");
    auto tokens = lexer.tokenize();
    
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0].kind, cm::TokenKind::String);
    EXPECT_EQ(tokens[0].text, "hello");
}
```

## 結合テスト

```cpp
// tests/integration/lexer_parser_test.cpp
#include <gtest/gtest.h>
#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser.hpp"

TEST(IntegrationTest, LexerToParser) {
    std::string source = R"(
        fn main() {
            let x: Int = 42;
        }
    )";
    
    cm::Lexer lexer(source);
    auto tokens = lexer.tokenize();
    
    cm::Parser parser(tokens);
    auto ast = parser.parse();
    
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->functions.size(), 1);
    EXPECT_EQ(ast->functions[0]->name, "main");
}
```

## E2Eテスト

### テストファイル形式

```
# tests/e2e/interpreter/hello_world.cm
fn main() {
    println("Hello, World!");
}
```

```
# tests/e2e/interpreter/hello_world.expected
Hello, World!
```

### テストランナー

```bash
#!/bin/bash
# tests/e2e/run_all.sh

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CM_BIN="${CM_BIN:-$SCRIPT_DIR/../../build/bin/cm}"
FAILED=0
PASSED=0

for test_file in "$SCRIPT_DIR"/interpreter/*.cm; do
    name=$(basename "$test_file" .cm)
    expected="$SCRIPT_DIR/interpreter/$name.expected"
    
    if [ ! -f "$expected" ]; then
        echo "SKIP: $name (no .expected file)"
        continue
    fi
    
    actual=$("$CM_BIN" run "$test_file" 2>&1)
    expected_content=$(cat "$expected")
    
    if [ "$actual" = "$expected_content" ]; then
        echo "PASS: $name"
        ((PASSED++))
    else
        echo "FAIL: $name"
        echo "  Expected: $expected_content"
        echo "  Actual:   $actual"
        ((FAILED++))
    fi
done

echo ""
echo "Results: $PASSED passed, $FAILED failed"
exit $FAILED
```

## リグレッションテスト

Issue修正時に追加するテスト:

```
# tests/regression/issues/issue_001.cm
# Issue #1: Parser crashes on empty function body
fn empty() {}

fn main() {
    empty();
    println("ok");
}
```

## デバッグモードテスト

### DRY原則検証

```cpp
// tests/debug/dry_check_test.cpp
#include <gtest/gtest.h>

// ログメッセージの重複検出
TEST(DebugModeTest, NoDuplicateLogMessages) {
    // 同じ処理で同じログが複数回出力されていないか検証
}

// フェーズ名の一貫性
TEST(DebugModeTest, ConsistentPhaseNames) {
    // LEXER, PARSER, TYPE_CHECK, HIR_LOWERING, CODEGEN の名前が統一されているか
}
```

## カバレッジ目標

| コンポーネント | 目標 |
|---------------|------|
| Lexer | 90% |
| Parser | 85% |
| TypeCheck | 80% |
| HIR | 80% |
| Backend | 75% |
| **全体** | **80%** |

## CI連携

```yaml
# .github/workflows/ci.yml の test ジョブ
- name: Unit Tests
  run: ctest --test-dir build --output-on-failure -L unit

- name: Integration Tests
  run: ctest --test-dir build --output-on-failure -L integration

- name: E2E Tests
  run: ./tests/e2e/run_all.sh
```