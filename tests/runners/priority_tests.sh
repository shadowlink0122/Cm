#!/bin/bash

# Cm優先度別テストランナー
# 開発優先順位に従ってテストを実行

set -e

# カラー出力
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# 設定
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
CM_EXEC="$PROJECT_ROOT/cm"
TEST_DIR="$PROJECT_ROOT/tests/test_programs"

# テストカウンター
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0
SKIPPED_TESTS=0

# オプション
PHASE="all"
VERBOSE=false
STOP_ON_FAIL=false

# ヘルプ
show_help() {
    echo "Cm Priority Test Runner"
    echo ""
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --phase=N        Run specific phase (0-4, all)"
    echo "  --verbose, -v    Show detailed output"
    echo "  --stop-on-fail   Stop on first failure"
    echo "  --help, -h       Show this help"
    echo ""
    echo "Phases:"
    echo "  0 - Basic output (Hello World)"
    echo "  1 - Basic features (variables, arithmetic)"
    echo "  2 - Control flow (if, while, for)"
    echo "  3 - Functions (not implemented)"
    echo "  4 - Data structures (not implemented)"
    exit 0
}

# オプション解析
while [[ $# -gt 0 ]]; do
    case $1 in
        --phase=*)
            PHASE="${1#*=}"
            ;;
        --verbose|-v)
            VERBOSE=true
            ;;
        --stop-on-fail)
            STOP_ON_FAIL=true
            ;;
        --help|-h)
            show_help
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
    shift
done

# テスト実行
run_test() {
    local test_file=$1
    local expect_file=${test_file%.cm}.expect
    local test_name=$(basename $test_file .cm)

    ((TOTAL_TESTS++))

    if [ ! -f "$expect_file" ]; then
        echo -e "${YELLOW}⊘${NC} $test_name (no expect file)"
        ((SKIPPED_TESTS++))
        return
    fi

    # インタープリタで実行
    local output_file="/tmp/${test_name}.out"
    local exit_code=0

    if $CM_EXEC --run --quiet "$test_file" > "$output_file" 2>&1; then
        echo "EXIT: 0" >> "$output_file"
    else
        exit_code=$?
        echo "EXIT: $exit_code" >> "$output_file"
    fi

    # エラーケースの処理
    if [[ "$test_name" == err_* ]]; then
        # エラーケースは異なる処理
        if grep -q "COMPILE_ERROR" "$expect_file"; then
            if [ $exit_code -ne 0 ]; then
                echo -e "${GREEN}✓${NC} $test_name (error case)"
                ((PASSED_TESTS++))
            else
                echo -e "${RED}✗${NC} $test_name (should have failed)"
                ((FAILED_TESTS++))
                if [ "$VERBOSE" = true ]; then
                    echo "  Output:"
                    cat "$output_file" | head -3 | sed 's/^/    /'
                fi
                if [ "$STOP_ON_FAIL" = true ]; then
                    exit 1
                fi
            fi
        fi
    else
        # 通常のテスト
        if diff -q "$expect_file" "$output_file" > /dev/null 2>&1; then
            echo -e "${GREEN}✓${NC} $test_name"
            ((PASSED_TESTS++))
        else
            echo -e "${RED}✗${NC} $test_name"
            ((FAILED_TESTS++))
            if [ "$VERBOSE" = true ]; then
                echo "  Expected:"
                cat "$expect_file" | head -3 | sed 's/^/    /'
                echo "  Got:"
                cat "$output_file" | head -3 | sed 's/^/    /'
            fi
            if [ "$STOP_ON_FAIL" = true ]; then
                exit 1
            fi
        fi
    fi

    rm -f "$output_file"
}

# フェーズ実行
run_phase() {
    local phase_num=$1
    local phase_dir=""
    local phase_name=""

    case $phase_num in
        0)
            phase_dir="p0_basics"
            phase_name="Phase 0: Basic Output"
            ;;
        1)
            phase_dir="p1_basic_features"
            phase_name="Phase 1: Basic Features"
            ;;
        2)
            phase_dir="p2_control_flow"
            phase_name="Phase 2: Control Flow"
            ;;
        3)
            phase_dir="p3_functions"
            phase_name="Phase 3: Functions"
            ;;
        4)
            phase_dir="p4_data_structures"
            phase_name="Phase 4: Data Structures"
            ;;
        errors)
            phase_dir="errors"
            phase_name="Error Cases"
            ;;
        *)
            echo "Unknown phase: $phase_num"
            return
            ;;
    esac

    if [ ! -d "$TEST_DIR/$phase_dir" ]; then
        echo -e "${YELLOW}Phase $phase_num not found (not implemented yet)${NC}"
        return
    fi

    echo -e "\n${BLUE}=== $phase_name ===${NC}"

    for test_file in "$TEST_DIR/$phase_dir"/*.cm; do
        [ -f "$test_file" ] || continue
        run_test "$test_file"
    done
}

# メイン処理
echo -e "${CYAN}Cm Priority Test Runner${NC}"
echo "Running tests for phase: $PHASE"
echo ""

# cmコンパイラをビルド（必要な場合）
if [ ! -f "$CM_EXEC" ]; then
    echo "Building cm compiler..."
    cd "$PROJECT_ROOT"
    cmake -B build
    cmake --build build
    cd -
fi

# フェーズ実行
if [ "$PHASE" == "all" ]; then
    # 実装済みフェーズのみ実行
    for phase in 0 1 2; do
        run_phase $phase
    done
    # エラーケースも実行
    run_phase errors
else
    run_phase $PHASE
fi

# サマリー
echo ""
echo -e "${BLUE}=== Test Summary ===${NC}"
echo "Total:   $TOTAL_TESTS"
echo -e "Passed:  ${GREEN}$PASSED_TESTS${NC}"
echo -e "Failed:  ${RED}$FAILED_TESTS${NC}"
echo -e "Skipped: ${YELLOW}$SKIPPED_TESTS${NC}"

# 実装状況
echo ""
echo -e "${BLUE}=== Implementation Status ===${NC}"
echo -e "Phase 0 (Basic):     ${GREEN}✓${NC} Tests ready"
echo -e "Phase 1 (Features):  ${GREEN}✓${NC} Tests ready"
echo -e "Phase 2 (Control):   ${GREEN}✓${NC} Tests ready"
echo -e "Phase 3 (Functions): ${YELLOW}⊘${NC} Not implemented"
echo -e "Phase 4 (Data):      ${YELLOW}⊘${NC} Not implemented"

if [ $FAILED_TESTS -eq 0 ] && [ $TOTAL_TESTS -gt 0 ]; then
    echo -e "\n${GREEN}All tests passed!${NC}"
    exit 0
elif [ $TOTAL_TESTS -eq 0 ]; then
    echo -e "\n${YELLOW}No tests were run${NC}"
    exit 0
else
    echo -e "\n${RED}Some tests failed${NC}"
    echo "Next steps:"
    echo "1. Fix println output connection in MIR interpreter"
    echo "2. Implement missing language features"
    echo "3. Run tests with --verbose for details"
    exit 1
fi