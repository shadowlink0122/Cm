#!/bin/bash

# Cm リグレッションテストランナー
# 全バックエンドでテストを実行し、前回の結果と比較

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_RUNNER="$SCRIPT_DIR/test_runner.sh"
REGRESSION_DIR="$SCRIPT_DIR/../regression"
RESULTS_DIR="$REGRESSION_DIR/results"

# カラー出力
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# 設定
BACKENDS="interpreter"  # 実装済みのバックエンドのみ
# 将来的に: BACKENDS="interpreter rust typescript wasm"
SUITES="basic control_flow"  # 現在のテストスイート
# 将来的に: SUITES="basic control_flow functions types overload"

# オプション解析
COMPARE_MODE=false
SAVE_BASELINE=false
VERBOSE=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --compare)
            COMPARE_MODE=true
            ;;
        --save-baseline)
            SAVE_BASELINE=true
            ;;
        --verbose|-v)
            VERBOSE=true
            ;;
        --help|-h)
            echo "Cm Regression Test Runner"
            echo ""
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --compare        Compare with previous baseline"
            echo "  --save-baseline  Save current results as baseline"
            echo "  --verbose        Verbose output"
            echo "  --help           Show this help"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
    shift
done

# 結果ディレクトリを作成
mkdir -p "$RESULTS_DIR"

# タイムスタンプ
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
CURRENT_RESULTS="$RESULTS_DIR/results_${TIMESTAMP}.txt"

echo -e "${BLUE}=== Cm Regression Test ===${NC}"
echo "Timestamp: $TIMESTAMP"
echo "Backends: $BACKENDS"
echo "Suites: $SUITES"
echo ""

# 全体の結果
TOTAL_PASSED=0
TOTAL_FAILED=0
TOTAL_SKIPPED=0
FAILED_TESTS=""

# 各バックエンドでテストを実行
for backend in $BACKENDS; do
    echo -e "\n${BLUE}=== Testing Backend: $backend ===${NC}"

    for suite in $SUITES; do
        echo -e "${BLUE}--- Suite: $suite ---${NC}"

        # テストランナーを実行
        OUTPUT=$($TEST_RUNNER --backend=$backend --suite=$suite 2>&1 || true)
        echo "$OUTPUT"

        # 結果を解析
        if echo "$OUTPUT" | grep -q "All tests passed"; then
            PASSED=$(echo "$OUTPUT" | grep "^Passed:" | awk '{print $2}')
            TOTAL_PASSED=$((TOTAL_PASSED + PASSED))
        else
            # 失敗したテストを記録
            FAILED=$(echo "$OUTPUT" | grep "^Failed:" | awk '{print $2}')
            SKIPPED=$(echo "$OUTPUT" | grep "^Skipped:" | awk '{print $2}')
            TOTAL_FAILED=$((TOTAL_FAILED + FAILED))
            TOTAL_SKIPPED=$((TOTAL_SKIPPED + SKIPPED))

            # 失敗したテストの詳細を記録
            FAILED_DETAIL=$(echo "$OUTPUT" | grep "✗")
            if [ -n "$FAILED_DETAIL" ]; then
                FAILED_TESTS="${FAILED_TESTS}\n${backend}/${suite}:\n${FAILED_DETAIL}"
            fi
        fi

        # 結果を保存
        echo "=== Backend: $backend, Suite: $suite ===" >> "$CURRENT_RESULTS"
        echo "$OUTPUT" >> "$CURRENT_RESULTS"
        echo "" >> "$CURRENT_RESULTS"
    done
done

# サマリー表示
echo ""
echo -e "${BLUE}=== Regression Test Summary ===${NC}"
echo "Total Passed: $TOTAL_PASSED"
echo "Total Failed: $TOTAL_FAILED"
echo "Total Skipped: $TOTAL_SKIPPED"

if [ -n "$FAILED_TESTS" ]; then
    echo ""
    echo -e "${RED}Failed Tests:${NC}"
    echo -e "$FAILED_TESTS"
fi

# ベースラインと比較
if [ "$COMPARE_MODE" = true ]; then
    BASELINE="$RESULTS_DIR/baseline.txt"
    if [ -f "$BASELINE" ]; then
        echo ""
        echo -e "${BLUE}=== Comparing with Baseline ===${NC}"

        # 簡易的な比較（Pass/Fail数の変化をチェック）
        BASELINE_PASSED=$(grep "^Total Passed:" "$BASELINE" | tail -1 | awk '{print $3}')
        BASELINE_FAILED=$(grep "^Total Failed:" "$BASELINE" | tail -1 | awk '{print $3}')

        if [ "$TOTAL_FAILED" -gt "$BASELINE_FAILED" ]; then
            echo -e "${RED}⚠ Regression detected!${NC}"
            echo "  Baseline failures: $BASELINE_FAILED"
            echo "  Current failures: $TOTAL_FAILED"
            REGRESSION_DETECTED=true
        elif [ "$TOTAL_FAILED" -lt "$BASELINE_FAILED" ]; then
            echo -e "${GREEN}✓ Improvement detected!${NC}"
            echo "  Baseline failures: $BASELINE_FAILED"
            echo "  Current failures: $TOTAL_FAILED"
        else
            echo -e "${GREEN}✓ No regression${NC}"
        fi
    else
        echo -e "${YELLOW}No baseline found. Run with --save-baseline to create one.${NC}"
    fi
fi

# ベースラインを保存
if [ "$SAVE_BASELINE" = true ]; then
    cp "$CURRENT_RESULTS" "$RESULTS_DIR/baseline.txt"
    echo ""
    echo -e "${GREEN}✓ Baseline saved${NC}"
fi

# 終了コード
if [ "$TOTAL_FAILED" -gt 0 ]; then
    echo ""
    echo -e "${RED}Regression test failed${NC}"
    exit 1
else
    echo ""
    echo -e "${GREEN}Regression test passed${NC}"
    exit 0
fi