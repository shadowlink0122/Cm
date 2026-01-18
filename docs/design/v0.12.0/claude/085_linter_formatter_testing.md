# Cm言語 Linter/Formatter テスト設計

**文書番号:** 085
**作成日:** 2026-01-13
**バージョン:** v0.12.0
**ステータス:** テスト設計

## 1. 概要

Linter、Checker、Formatterのテストシステム設計。特にFormatterによるファイル変更を安全にテストする仕組みを提供。

### 設計原則
1. **非破壊的**: 元ファイルを変更しない
2. **再現性**: 同じ入力に対して同じ結果
3. **独立性**: テスト間の相互影響なし
4. **CI統合**: GitHub Actionsで自動実行

## 2. ディレクトリ構造

```
tests/linter/
├── fixtures/                    # テスト用固定ファイル（読み取り専用）
│   ├── valid/                  # 正しいコード
│   │   ├── basic.cm
│   │   ├── functions.cm
│   │   └── structs.cm
│   ├── invalid/                # 問題のあるコード
│   │   ├── bad_indent.cm      # インデント違反
│   │   ├── bad_naming.cm      # 命名規則違反
│   │   ├── unused_vars.cm     # 未使用変数
│   │   └── complex_func.cm    # 複雑度違反
│   ├── edge_cases/            # エッジケース
│   │   ├── empty.cm
│   │   ├── unicode.cm
│   │   └── large_file.cm
│   └── formatter/             # フォーマッター用
│       ├── input/             # フォーマット前
│       │   ├── messy_indent.cm
│       │   ├── long_lines.cm
│       │   └── mixed_style.cm
│       └── expected/          # 期待される結果
│           ├── messy_indent.expected.cm
│           ├── long_lines.expected.cm
│           └── mixed_style.expected.cm
│
├── workspace/                  # テスト実行用作業ディレクトリ
│   └── .gitignore             # *.cm, *.tmp を無視
│
├── configs/                    # テスト用設定ファイル
│   ├── default.yml            # デフォルト設定
│   ├── strict.yml             # 厳格設定
│   ├── minimal.yml            # 最小設定
│   └── custom/                # カスタム設定
│       ├── no_indent.yml      # インデントチェック無効
│       └── long_lines.yml     # 長い行を許可
│
├── scripts/                    # テストスクリプト
│   ├── run_tests.sh           # メインテストランナー
│   ├── setup_workspace.sh     # ワークスペース準備
│   ├── compare_results.sh     # 結果比較
│   └── clean_workspace.sh     # クリーンアップ
│
├── expected/                   # 期待される診断結果
│   ├── bad_indent.expected    # JSON形式の診断
│   ├── bad_naming.expected
│   └── ...
│
└── Makefile                    # テスト実行用
```

## 3. テストフレームワーク設計

### 3.1 基本テストクラス

```cpp
// tests/linter/test_framework.h
class LinterTestFramework {
private:
    string workspace_dir;
    string fixtures_dir;
    string config_file;

public:
    // テスト環境のセットアップ
    void setup() {
        // 作業ディレクトリを作成
        workspace_dir = create_temp_dir("linter_test_");

        // fixturesディレクトリの確認
        fixtures_dir = get_fixtures_path();
        if (!exists(fixtures_dir)) {
            throw runtime_error("Fixtures directory not found");
        }
    }

    // ファイルを作業ディレクトリにコピー
    string prepare_test_file(const string& fixture_path) {
        auto source = fixtures_dir + "/" + fixture_path;
        auto dest = workspace_dir + "/" + basename(fixture_path);

        // ファイルをコピー（元ファイルは変更しない）
        copy_file(source, dest);

        return dest;
    }

    // Linterテスト実行
    TestResult run_linter_test(const string& fixture,
                               const string& config = "default.yml") {
        auto test_file = prepare_test_file(fixture);

        // Linter実行
        LinterEngine linter;
        linter.load_config(configs_dir + "/" + config);
        auto diagnostics = linter.analyze(test_file);

        return {
            .file = test_file,
            .diagnostics = diagnostics,
            .exit_code = diagnostics.has_errors() ? 1 : 0
        };
    }

    // Formatterテスト実行
    TestResult run_formatter_test(const string& input_fixture,
                                  const string& expected_fixture) {
        auto test_file = prepare_test_file("formatter/input/" + input_fixture);
        auto expected = read_file(fixtures_dir + "/formatter/expected/" + expected_fixture);

        // Formatter実行
        FormatterEngine formatter;
        formatter.format_file(test_file);  // ファイルを直接変更

        // 結果を比較
        auto actual = read_file(test_file);

        return {
            .file = test_file,
            .success = (actual == expected),
            .diff = generate_diff(expected, actual)
        };
    }

    // クリーンアップ
    void teardown() {
        if (!workspace_dir.empty()) {
            remove_directory(workspace_dir);
        }
    }
};
```

### 3.2 個別テストケース

```cpp
// tests/linter/test_indent.cpp
TEST(LinterTest, BadIndent) {
    LinterTestFramework fw;
    fw.setup();

    auto result = fw.run_linter_test("invalid/bad_indent.cm");

    // 診断結果の検証
    EXPECT_TRUE(result.has_diagnostic("L001"));  // インデント違反
    EXPECT_EQ(result.count_diagnostics("L001"), 3);

    // 期待される診断と比較
    auto expected = load_expected_diagnostics("bad_indent.expected");
    EXPECT_EQ(result.diagnostics, expected);

    fw.teardown();
}

TEST(FormatterTest, MessyIndent) {
    LinterTestFramework fw;
    fw.setup();

    auto result = fw.run_formatter_test(
        "messy_indent.cm",
        "messy_indent.expected.cm"
    );

    EXPECT_TRUE(result.success);
    if (!result.success) {
        // 差分を表示
        cout << "Diff:\n" << result.diff << endl;
    }

    fw.teardown();
}

TEST(LinterTest, ConfigOverride) {
    LinterTestFramework fw;
    fw.setup();

    // カスタム設定でテスト
    auto result = fw.run_linter_test(
        "invalid/bad_indent.cm",
        "custom/no_indent.yml"  // インデントチェック無効
    );

    EXPECT_FALSE(result.has_diagnostic("L001"));  // 検出されない

    fw.teardown();
}
```

## 4. Makefile統合

```makefile
# tests/linter/Makefile

# 変数定義
BUILD_DIR = ../../build
LINTER = $(BUILD_DIR)/bin/cm-lint
FORMATTER = $(BUILD_DIR)/bin/cm-fmt
CHECKER = $(BUILD_DIR)/bin/cm-check

FIXTURES_DIR = fixtures
WORKSPACE_DIR = workspace
TEMP_DIR = /tmp/cm_linter_test_$$$$

# カラー出力
RED = \033[0;31m
GREEN = \033[0;32m
YELLOW = \033[0;33m
NC = \033[0m # No Color

# デフォルトターゲット
.PHONY: all
all: test

# テスト実行
.PHONY: test
test: test-linter test-checker test-formatter

# Linterテスト
.PHONY: test-linter
test-linter: prepare
	@echo "$(GREEN)Running Linter tests...$(NC)"
	@for fixture in $(FIXTURES_DIR)/invalid/*.cm; do \
		echo "Testing: $$(basename $$fixture)"; \
		cp $$fixture $(WORKSPACE_DIR)/; \
		$(LINTER) $(WORKSPACE_DIR)/$$(basename $$fixture) \
			--config configs/default.yml \
			--format json > $(WORKSPACE_DIR)/$$(basename $$fixture .cm).result; \
		python3 scripts/compare_results.py \
			$(WORKSPACE_DIR)/$$(basename $$fixture .cm).result \
			expected/$$(basename $$fixture .cm).expected || exit 1; \
	done
	@echo "$(GREEN)✓ Linter tests passed$(NC)"

# Checkerテスト（型チェック）
.PHONY: test-checker
test-checker: prepare
	@echo "$(GREEN)Running Checker tests...$(NC)"
	@for fixture in $(FIXTURES_DIR)/invalid/*.cm; do \
		echo "Testing: $$(basename $$fixture)"; \
		cp $$fixture $(WORKSPACE_DIR)/; \
		$(CHECKER) $(WORKSPACE_DIR)/$$(basename $$fixture) \
			--types-only || true; \
	done
	@echo "$(GREEN)✓ Checker tests passed$(NC)"

# Formatterテスト
.PHONY: test-formatter
test-formatter: prepare
	@echo "$(GREEN)Running Formatter tests...$(NC)"
	@for input in $(FIXTURES_DIR)/formatter/input/*.cm; do \
		base=$$(basename $$input .cm); \
		echo "Testing: $$base"; \
		cp $$input $(WORKSPACE_DIR)/$$base.cm; \
		$(FORMATTER) $(WORKSPACE_DIR)/$$base.cm; \
		diff -u $(FIXTURES_DIR)/formatter/expected/$$base.expected.cm \
			$(WORKSPACE_DIR)/$$base.cm || \
			(echo "$(RED)✗ Formatter test failed: $$base$(NC)" && exit 1); \
	done
	@echo "$(GREEN)✓ Formatter tests passed$(NC)"

# 作業ディレクトリの準備
.PHONY: prepare
prepare:
	@mkdir -p $(WORKSPACE_DIR)
	@rm -f $(WORKSPACE_DIR)/*.cm
	@rm -f $(WORKSPACE_DIR)/*.result

# クリーンアップ
.PHONY: clean
clean:
	@rm -rf $(WORKSPACE_DIR)
	@echo "$(YELLOW)Cleaned workspace$(NC)"

# 診断結果の更新（新しい期待値を生成）
.PHONY: update-expected
update-expected:
	@echo "$(YELLOW)Updating expected results...$(NC)"
	@for fixture in $(FIXTURES_DIR)/invalid/*.cm; do \
		$(LINTER) $$fixture --format json > \
			expected/$$(basename $$fixture .cm).expected; \
	done
	@for input in $(FIXTURES_DIR)/formatter/input/*.cm; do \
		base=$$(basename $$input .cm); \
		cp $$input $(TEMP_DIR)/$$base.cm; \
		$(FORMATTER) $(TEMP_DIR)/$$base.cm; \
		cp $(TEMP_DIR)/$$base.cm \
			$(FIXTURES_DIR)/formatter/expected/$$base.expected.cm; \
	done
	@echo "$(GREEN)✓ Expected results updated$(NC)"

# 統計情報
.PHONY: stats
stats:
	@echo "Test Statistics:"
	@echo "  Linter fixtures: $$(ls $(FIXTURES_DIR)/invalid/*.cm 2>/dev/null | wc -l)"
	@echo "  Formatter fixtures: $$(ls $(FIXTURES_DIR)/formatter/input/*.cm 2>/dev/null | wc -l)"
	@echo "  Config files: $$(ls configs/*.yml 2>/dev/null | wc -l)"
	@echo "  Expected results: $$(ls expected/*.expected 2>/dev/null | wc -l)"

# 個別テスト実行
.PHONY: test-single
test-single: prepare
	@if [ -z "$(FILE)" ]; then \
		echo "Usage: make test-single FILE=bad_indent.cm"; \
		exit 1; \
	fi
	@echo "Testing single file: $(FILE)"
	@cp $(FIXTURES_DIR)/invalid/$(FILE) $(WORKSPACE_DIR)/
	@$(LINTER) $(WORKSPACE_DIR)/$(FILE) --config configs/default.yml
```

## 5. テストフィクスチャ例

### 5.1 インデント違反（bad_indent.cm）

```cm
// fixtures/invalid/bad_indent.cm
int main() {
  if (true) {  // 2スペース（エラー：L001）
   int x = 10; // 3スペース（エラー：L001）
    return x;  // 正しい4スペース
  }
}
```

### 5.2 期待される診断（bad_indent.expected）

```json
{
  "diagnostics": [
    {
      "id": "L001",
      "level": "error",
      "message": "Indentation should be 4 spaces",
      "location": {
        "file": "bad_indent.cm",
        "line": 2,
        "column": 1
      },
      "expected": 4,
      "actual": 2
    },
    {
      "id": "L001",
      "level": "error",
      "message": "Indentation should be 4 spaces",
      "location": {
        "file": "bad_indent.cm",
        "line": 3,
        "column": 1
      },
      "expected": 8,
      "actual": 3
    }
  ],
  "summary": {
    "errors": 2,
    "warnings": 0,
    "hints": 0
  }
}
```

### 5.3 フォーマッター入力（messy_indent.cm）

```cm
// fixtures/formatter/input/messy_indent.cm
int main(){
if(x){
y++;
}else{
z++;
}
}
```

### 5.4 フォーマッター期待結果（messy_indent.expected.cm）

```cm
// fixtures/formatter/expected/messy_indent.expected.cm
int main() {
    if (x) {
        y++;
    } else {
        z++;
    }
}
```

## 6. CI統合（GitHub Actions）

```yaml
# .github/workflows/linter-tests.yml
name: Linter/Formatter Tests

on:
  push:
    branches: [main, develop]
    paths:
      - 'src/linter/**'
      - 'src/formatter/**'
      - 'tests/linter/**'
  pull_request:
    branches: [main]

jobs:
  test-linux:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Setup build environment
        run: |
          sudo apt-get update
          sudo apt-get install -y cmake build-essential

      - name: Build tools
        run: |
          mkdir build && cd build
          cmake .. -DCMAKE_BUILD_TYPE=Debug -DCM_BUILD_LINTER=ON
          make cm-lint cm-fmt cm-check -j$(nproc)

      - name: Run Linter tests
        run: |
          cd tests/linter
          make test-linter

      - name: Run Formatter tests
        run: |
          cd tests/linter
          make test-formatter

      - name: Run Checker tests
        run: |
          cd tests/linter
          make test-checker

      - name: Upload test results
        if: always()
        uses: actions/upload-artifact@v3
        with:
          name: test-results
          path: tests/linter/workspace/*.result

  test-macos:
    runs-on: macos-latest
    steps:
      - uses: actions/checkout@v4

      - name: Setup build environment
        run: |
          brew install cmake

      - name: Build tools
        run: |
          mkdir build && cd build
          cmake .. -DCMAKE_BUILD_TYPE=Debug -DCM_BUILD_LINTER=ON
          make cm-lint cm-fmt cm-check -j$(sysctl -n hw.logicalcpu)

      - name: Run tests
        run: |
          cd tests/linter
          make test

  test-windows:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4

      - name: Setup MSVC
        uses: microsoft/setup-msbuild@v1

      - name: Build tools
        run: |
          mkdir build
          cd build
          cmake .. -G "Visual Studio 17 2022" -DCM_BUILD_LINTER=ON
          cmake --build . --config Debug

      - name: Run tests
        shell: bash
        run: |
          cd tests/linter
          # Windowsではbashスクリプトを使用
          bash scripts/run_tests.sh
```

## 7. 結果比較スクリプト

```python
#!/usr/bin/env python3
# tests/linter/scripts/compare_results.py

import json
import sys
from deepdiff import DeepDiff

def load_diagnostics(filepath):
    with open(filepath, 'r') as f:
        return json.load(f)

def compare_diagnostics(actual_file, expected_file):
    actual = load_diagnostics(actual_file)
    expected = load_diagnostics(expected_file)

    # 診断の比較（順序を無視）
    diff = DeepDiff(expected, actual, ignore_order=True)

    if diff:
        print(f"❌ Diagnostics mismatch:")
        print(json.dumps(diff, indent=2))
        return False

    print(f"✅ Diagnostics match")
    return True

def main():
    if len(sys.argv) != 3:
        print("Usage: compare_results.py <actual> <expected>")
        sys.exit(1)

    actual_file = sys.argv[1]
    expected_file = sys.argv[2]

    if compare_diagnostics(actual_file, expected_file):
        sys.exit(0)
    else:
        sys.exit(1)

if __name__ == "__main__":
    main()
```

## 8. テストヘルパー関数

```cpp
// tests/linter/test_helpers.h

// 診断結果の検証ヘルパー
class DiagnosticMatcher {
public:
    static bool has_error(const vector<Diagnostic>& diags, const string& id) {
        return any_of(diags.begin(), diags.end(),
            [&](const auto& d) {
                return d.id == id && d.level == DiagnosticLevel::Error;
            });
    }

    static int count_diagnostics(const vector<Diagnostic>& diags,
                                 const string& id) {
        return count_if(diags.begin(), diags.end(),
            [&](const auto& d) { return d.id == id; });
    }

    static bool at_line(const vector<Diagnostic>& diags,
                        const string& id, int line) {
        return any_of(diags.begin(), diags.end(),
            [&](const auto& d) {
                return d.id == id && d.location.line == line;
            });
    }
};

// ファイル差分生成
string generate_unified_diff(const string& expected,
                             const string& actual,
                             const string& filename = "file") {
    // diffライブラリを使用してunified diff生成
    return diff::unified_diff(expected, actual, filename);
}

// テスト用の一時ファイル管理
class TempFileManager {
    vector<string> temp_files;

public:
    string create_temp_file(const string& content,
                           const string& suffix = ".cm") {
        char temp_name[] = "/tmp/cm_test_XXXXXX";
        int fd = mkstemp(temp_name);
        close(fd);

        string filename = string(temp_name) + suffix;
        write_file(filename, content);
        temp_files.push_back(filename);

        return filename;
    }

    ~TempFileManager() {
        for (const auto& file : temp_files) {
            remove(file.c_str());
        }
    }
};
```

## 9. 統合テスト例

```cpp
// tests/linter/integration_test.cpp

TEST(IntegrationTest, FullPipeline) {
    // 1. Lintチェック
    auto lint_result = run_linter("fixtures/invalid/complex_file.cm");
    EXPECT_GT(lint_result.diagnostics.size(), 0);

    // 2. フォーマット
    TempFileManager tm;
    auto formatted = tm.create_temp_file(
        read_file("fixtures/invalid/complex_file.cm")
    );
    run_formatter(formatted);

    // 3. 再度Lintチェック（フォーマット後）
    auto post_lint = run_linter(formatted);

    // スタイル違反は解消されているはず
    EXPECT_FALSE(DiagnosticMatcher::has_error(post_lint.diagnostics, "L001"));
    EXPECT_FALSE(DiagnosticMatcher::has_error(post_lint.diagnostics, "L002"));

    // ロジックエラーは残っているはず
    EXPECT_TRUE(DiagnosticMatcher::has_error(post_lint.diagnostics, "W001"));
}

TEST(PerformanceTest, LargeFile) {
    auto start = chrono::high_resolution_clock::now();

    // 10000行のファイルをテスト
    run_linter("fixtures/edge_cases/large_file.cm");

    auto end = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::milliseconds>(end - start);

    // 1秒以内に完了すべき
    EXPECT_LT(duration.count(), 1000);
}
```

## 10. テストカバレッジ計測

```makefile
# カバレッジ計測
.PHONY: coverage
coverage:
	@echo "$(GREEN)Running coverage analysis...$(NC)"
	@mkdir -p coverage
	@$(BUILD_DIR)/bin/cm-lint-test --gtest_output=xml:coverage/test_results.xml
	@lcov --capture --directory $(BUILD_DIR) --output-file coverage/coverage.info
	@lcov --remove coverage/coverage.info '/usr/*' --output-file coverage/coverage.info
	@genhtml coverage/coverage.info --output-directory coverage/html
	@echo "Coverage report: coverage/html/index.html"
```

## 11. トラブルシューティング

### 11.1 よくある問題と対処

```bash
# 権限エラー
chmod +x scripts/*.sh

# ワークスペースが残っている
make clean

# 期待値の更新
make update-expected

# 単一ファイルのデバッグ
make test-single FILE=bad_indent.cm
```

## 12. 関連ドキュメント

- [078_linter_formatter_design.md](./078_linter_formatter_design.md) - Linter/Formatter設計
- [079_unified_diagnostic_system.md](./079_unified_diagnostic_system.md) - 診断システム
- [084_lint_configuration_system.md](./084_lint_configuration_system.md) - 設定システム