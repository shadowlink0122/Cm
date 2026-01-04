#!/bin/bash

# Optimization Level Test Runner for Cm Language
# Tests all optimization levels (O0-O3) for each test case

# 設定
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# 実行ファイル
CM_EXECUTABLE="$PROJECT_ROOT/cm"
TEST_DIR="$PROJECT_ROOT/tests/test_programs"
TEMP_DIR="$PROJECT_ROOT/.tmp/opt_test"

# カラー出力
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# デフォルト値
BACKEND="llvm"
CATEGORIES=""
VERBOSE=false
TIMEOUT=5
QUICK_MODE=false

# タイムアウトコマンドの検出
TIMEOUT_CMD=""
if command -v gtimeout >/dev/null 2>&1; then
    TIMEOUT_CMD="gtimeout"
elif command -v timeout >/dev/null 2>&1; then
    TIMEOUT_CMD="timeout"
fi

# 統計情報
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0
# 通常の配列で統計を管理（macOS互換性のため）
O0_passed=0
O0_failed=0
O1_passed=0
O1_failed=0
O2_passed=0
O2_failed=0
O3_passed=0
O3_failed=0

# ヘルプメッセージ
usage() {
    echo "Usage: $0 [OPTIONS]"
    echo "Options:"
    echo "  -b, --backend <backend>    Backend: llvm|llvm-wasm (default: llvm)"
    echo "  -c, --category <category>  Test categories (comma-separated)"
    echo "  -q, --quick                Quick mode - test only one file per category"
    echo "  -v, --verbose              Show detailed output"
    echo "  -t, --timeout <seconds>    Test timeout in seconds (default: 5)"
    echo "  -h, --help                 Show this help message"
    echo ""
    echo "This script tests all optimization levels (O0-O3) for each test case."
    exit 0
}

# オプション解析
while [[ $# -gt 0 ]]; do
    case $1 in
        -b|--backend)
            BACKEND="$2"
            shift 2
            ;;
        -c|--category)
            CATEGORIES="$2"
            shift 2
            ;;
        -q|--quick)
            QUICK_MODE=true
            shift
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -t|--timeout)
            TIMEOUT="$2"
            shift 2
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo "Unknown option: $1"
            usage
            ;;
    esac
done

# ディレクトリ作成
mkdir -p "$TEMP_DIR"

# カテゴリの自動検出
if [ -z "$CATEGORIES" ]; then
    CATEGORIES=""
    for dir in "$TEST_DIR"/*; do
        if [ -d "$dir" ]; then
            basename_dir=$(basename "$dir")
            # advanced_modulesとmodulesは除外（モジュール関連は別途テスト）
            if [[ "$basename_dir" != "advanced_modules" && "$basename_dir" != "modules" ]]; then
                if [ -z "$CATEGORIES" ]; then
                    CATEGORIES="$basename_dir"
                else
                    CATEGORIES="$CATEGORIES,$basename_dir"
                fi
            fi
        fi
    done
fi

# カテゴリを配列に変換
IFS=',' read -ra CATEGORY_ARRAY <<< "$CATEGORIES"

echo "=========================================="
echo " Cm Optimization Level Test Runner"
echo "=========================================="
echo "Backend: $BACKEND"
echo "Categories: ${CATEGORY_ARRAY[*]}"
echo "Timeout: ${TIMEOUT}s"
echo "Quick Mode: $QUICK_MODE"
echo ""

# タイムアウト付き実行関数
run_with_timeout() {
    if [ -n "$TIMEOUT_CMD" ]; then
        $TIMEOUT_CMD "$TIMEOUT" "$@"
    else
        "$@"
    fi
}

# 単一ファイルの全最適化レベルテスト
test_file_all_opts() {
    local test_file="$1"
    local test_name="$(basename "$test_file" .cm)"
    local test_category="$(basename "$(dirname "$test_file")")"
    local all_passed=true
    local opt_results=""

    if $VERBOSE; then
        echo -e "\n${BLUE}Testing: $test_category/$test_name${NC}"
    else
        printf "  %-50s " "$test_category/$test_name"
    fi

    # 各最適化レベルでテスト
    for opt_level in 0 1 2 3; do
        local output_file="$TEMP_DIR/${test_name}_O${opt_level}_output.txt"
        local exec_file="$TEMP_DIR/${test_name}_O${opt_level}"
        local exit_code=0

        # テストファイルのディレクトリに移動
        local test_dir="$(dirname "$test_file")"
        local test_basename="$(basename "$test_file")"

        case "$BACKEND" in
            llvm)
                # コンパイル
                (cd "$test_dir" && run_with_timeout "$CM_EXECUTABLE" compile \
                    --emit-llvm "$test_basename" -O${opt_level} -o "$exec_file" \
                    > /dev/null 2>&1) || exit_code=$?

                if [ $exit_code -eq 0 ] && [ -f "$exec_file" ]; then
                    # 実行
                    run_with_timeout "$exec_file" > "$output_file" 2>&1 || exit_code=$?
                fi
                ;;

            llvm-wasm)
                # WebAssemblyコンパイル
                local wasm_file="$TEMP_DIR/${test_name}_O${opt_level}.wasm"
                (cd "$test_dir" && run_with_timeout "$CM_EXECUTABLE" compile \
                    --emit-llvm-wasm "$test_basename" -O${opt_level} -o "$wasm_file" \
                    > /dev/null 2>&1) || exit_code=$?

                if [ $exit_code -eq 0 ] && [ -f "$wasm_file" ]; then
                    # wasmtime実行
                    if command -v wasmtime >/dev/null 2>&1; then
                        run_with_timeout wasmtime "$wasm_file" > "$output_file" 2>&1 || exit_code=$?
                    else
                        echo "wasmtime not found" > "$output_file"
                        exit_code=127
                    fi
                fi
                ;;
        esac

        # 結果記録
        if [ $exit_code -eq 0 ]; then
            opt_results="${opt_results}${GREEN}O${opt_level}✓${NC} "
            # 変数名を動的に構築して更新
            eval "((O${opt_level}_passed++))"
        else
            opt_results="${opt_results}${RED}O${opt_level}✗${NC} "
            eval "((O${opt_level}_failed++))"
            all_passed=false
        fi

        # エラー出力（詳細モード）
        if $VERBOSE && [ $exit_code -ne 0 ]; then
            echo -e "  ${RED}O${opt_level} failed (exit: $exit_code)${NC}"
            if [ -f "$output_file" ] && [ -s "$output_file" ]; then
                echo "  Output:"
                head -5 "$output_file" | sed 's/^/    /'
            fi
        fi
    done

    # 結果表示
    if $VERBOSE; then
        echo -e "  Results: $opt_results"
    else
        echo -e "$opt_results"
    fi

    if $all_passed; then
        ((PASSED_TESTS++))
    else
        ((FAILED_TESTS++))
    fi
    ((TOTAL_TESTS++))
}

# メインテストループ
for category in "${CATEGORY_ARRAY[@]}"; do
    category_dir="$TEST_DIR/$category"

    if [ ! -d "$category_dir" ]; then
        echo -e "${YELLOW}Warning: Category '$category' not found${NC}"
        continue
    fi

    echo -e "\n${BLUE}=== Testing category: $category ===${NC}"

    # テストファイルを収集
    test_files=()
    while IFS= read -r -d '' file; do
        # 期待値ファイルとエラーファイルは除外
        if [[ ! "$file" =~ \.(expect|error)$ ]]; then
            test_files+=("$file")
        fi
    done < <(find "$category_dir" -name "*.cm" -type f -print0 | sort -z)

    # クイックモードの場合は最初の1つだけ
    if $QUICK_MODE && [ ${#test_files[@]} -gt 0 ]; then
        test_files=("${test_files[0]}")
        echo "  (Quick mode: testing only first file)"
    fi

    # 各ファイルをテスト
    for test_file in "${test_files[@]}"; do
        test_file_all_opts "$test_file"
    done
done

# 統計情報表示
echo ""
echo "=========================================="
echo " Test Results Summary"
echo "=========================================="
echo -e "Total tests: $TOTAL_TESTS"
echo -e "Passed: ${GREEN}$PASSED_TESTS${NC}"
echo -e "Failed: ${RED}$FAILED_TESTS${NC}"
echo ""
echo "Optimization Level Breakdown:"
for opt in 0 1 2 3; do
    # 変数名を動的に取得
    passed=$(eval "echo \$O${opt}_passed")
    failed=$(eval "echo \$O${opt}_failed")
    total=$((passed + failed))
    if [ $total -gt 0 ]; then
        success_rate=$((passed * 100 / total))
        echo -e "  O${opt}: ${GREEN}$passed passed${NC}, ${RED}$failed failed${NC} (${success_rate}% success)"
    fi
done

# 終了コード
if [ $FAILED_TESTS -eq 0 ]; then
    echo -e "\n${GREEN}✅ All optimization levels passed!${NC}"
    exit 0
else
    echo -e "\n${RED}❌ Some tests failed${NC}"
    exit 1
fi