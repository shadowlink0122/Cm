#!/bin/bash

# Cm 統合テストランナー
# 複数バックエンド間の一貫性を検証

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_RUNNER="$SCRIPT_DIR/test_runner.sh"
INTEGRATION_DIR="$SCRIPT_DIR/../integration"
TEMP_DIR="$INTEGRATION_DIR/.tmp"

# カラー出力
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

# デフォルト設定
BACKENDS=""  # 比較するバックエンド
SUITE="basic"  # テストスイート
VERBOSE=false
STRICT=false  # 厳密モード（全バックエンドで同一出力を要求）

# オプション解析
while [[ $# -gt 0 ]]; do
    case $1 in
        --backends=*)
            BACKENDS="${1#*=}"
            ;;
        --suite=*)
            SUITE="${1#*=}"
            ;;
        --strict)
            STRICT=true
            ;;
        --verbose|-v)
            VERBOSE=true
            ;;
        --help|-h)
            echo "Cm Integration Test Runner"
            echo ""
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --backends=LIST  Backends to compare (comma-separated)"
            echo "                   Default: interpreter"
            echo "                   Example: --backends=interpreter,rust"
            echo "  --suite=SUITE    Test suite to run"
            echo "                   Default: basic"
            echo "  --strict         Require identical output across all backends"
            echo "  --verbose        Verbose output"
            echo "  --help           Show this help"
            echo ""
            echo "This runner compares outputs from different backends"
            echo "to ensure consistency across implementations."
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
    shift
done

# デフォルトバックエンド設定
if [ -z "$BACKENDS" ]; then
    BACKENDS="interpreter"
    echo -e "${YELLOW}Note: Only testing interpreter (other backends not yet implemented)${NC}"
fi

# バックエンドをリスト化
IFS=',' read -ra BACKEND_LIST <<< "$BACKENDS"

# 一時ディレクトリ作成
mkdir -p "$TEMP_DIR"
trap "rm -rf $TEMP_DIR" EXIT

echo -e "${BLUE}=== Cm Integration Test ===${NC}"
echo "Backends: ${BACKEND_LIST[*]}"
echo "Suite: $SUITE"
echo "Mode: $([ "$STRICT" = true ] && echo "Strict" || echo "Lenient")"
echo ""

# テスト結果を格納
declare -A RESULTS
declare -A OUTPUTS
TOTAL_TESTS=0
CONSISTENT_TESTS=0
INCONSISTENT_TESTS=0

# 各バックエンドでテストを実行し結果を収集
collect_outputs() {
    local test_file=$1
    local test_name=$(basename "$test_file" .cm)

    echo -e "\n${CYAN}Testing: $test_name${NC}"

    for backend in "${BACKEND_LIST[@]}"; do
        local output_file="$TEMP_DIR/${test_name}_${backend}.out"

        # テストを実行
        if $TEST_RUNNER --backend="$backend" "$test_file" > "$output_file" 2>&1; then
            # 出力から結果を抽出
            if grep -q "✓.*$test_name" "$output_file"; then
                RESULTS["${backend}_${test_name}"]="PASS"
                # 実際の出力を取得（テスト実行の出力）
                local actual_output="$TEMP_DIR/${test_name}_${backend}.actual"
                $SCRIPT_DIR/../../cm --run "$test_file" > "$actual_output" 2>&1 || true
                echo "EXIT: $?" >> "$actual_output"
                OUTPUTS["${backend}_${test_name}"]=$(cat "$actual_output")
            elif grep -q "✗.*$test_name" "$output_file"; then
                RESULTS["${backend}_${test_name}"]="FAIL"
            else
                RESULTS["${backend}_${test_name}"]="SKIP"
            fi
        else
            RESULTS["${backend}_${test_name}"]="ERROR"
        fi

        if [ "$VERBOSE" = true ]; then
            echo "  $backend: ${RESULTS["${backend}_${test_name}"]}"
        fi
    done
}

# 出力の一貫性を検証
verify_consistency() {
    local test_name=$1
    local reference_backend="${BACKEND_LIST[0]}"
    local reference_result="${RESULTS["${reference_backend}_${test_name}"]}"
    local reference_output="${OUTPUTS["${reference_backend}_${test_name}"]}"
    local consistent=true

    # 全バックエンドがPASSしているかチェック
    local all_pass=true
    for backend in "${BACKEND_LIST[@]}"; do
        if [ "${RESULTS["${backend}_${test_name}"]}" != "PASS" ]; then
            all_pass=false
            break
        fi
    done

    if [ "$all_pass" = false ]; then
        echo -e "  ${YELLOW}⚠ Not all backends passed${NC}"
        return 1
    fi

    # 出力の一貫性をチェック
    for backend in "${BACKEND_LIST[@]:1}"; do
        local backend_output="${OUTPUTS["${backend}_${test_name}"]}"

        if [ "$STRICT" = true ]; then
            # 厳密モード：完全一致を要求
            if [ "$reference_output" != "$backend_output" ]; then
                consistent=false
                echo -e "  ${RED}✗ Output mismatch: $reference_backend vs $backend${NC}"
                if [ "$VERBOSE" = true ]; then
                    echo "    Expected ($reference_backend):"
                    echo "$reference_output" | head -3 | sed 's/^/      /'
                    echo "    Got ($backend):"
                    echo "$backend_output" | head -3 | sed 's/^/      /'
                fi
            fi
        else
            # 寛容モード：主要な出力のみチェック（終了コードなど）
            local ref_exit=$(echo "$reference_output" | grep "^EXIT:" | tail -1)
            local backend_exit=$(echo "$backend_output" | grep "^EXIT:" | tail -1)

            if [ "$ref_exit" != "$backend_exit" ]; then
                consistent=false
                echo -e "  ${RED}✗ Exit code mismatch: $reference_backend vs $backend${NC}"
            fi
        fi
    done

    if [ "$consistent" = true ]; then
        echo -e "  ${GREEN}✓ Consistent across all backends${NC}"
        return 0
    else
        return 1
    fi
}

# テストスイートを実行
run_integration_suite() {
    local suite_dir="$SCRIPT_DIR/../test_programs/$SUITE"

    if [ ! -d "$suite_dir" ]; then
        echo "Error: Suite not found: $SUITE"
        exit 1
    fi

    echo -e "${BLUE}=== Running Suite: $SUITE ===${NC}"

    for test_file in "$suite_dir"/*.cm; do
        [ -f "$test_file" ] || continue

        local test_name=$(basename "$test_file" .cm)

        # 出力を収集
        collect_outputs "$test_file"

        # 一貫性を検証
        ((TOTAL_TESTS++))
        if verify_consistency "$test_name"; then
            ((CONSISTENT_TESTS++))
        else
            ((INCONSISTENT_TESTS++))
        fi
    done
}

# メイン実行
run_integration_suite

# サマリー表示
echo ""
echo -e "${BLUE}=== Integration Test Summary ===${NC}"
echo "Total tests: $TOTAL_TESTS"
echo -e "Consistent:  ${GREEN}$CONSISTENT_TESTS${NC}"
echo -e "Inconsistent: ${RED}$INCONSISTENT_TESTS${NC}"

# バックエンドごとの結果サマリー
echo ""
echo -e "${BLUE}=== Backend Results ===${NC}"
for backend in "${BACKEND_LIST[@]}"; do
    local passed=0
    local failed=0
    local skipped=0

    for test_file in "$SCRIPT_DIR/../test_programs/$SUITE"/*.cm; do
        [ -f "$test_file" ] || continue
        local test_name=$(basename "$test_file" .cm)
        case "${RESULTS["${backend}_${test_name}"]}" in
            PASS) ((passed++)) ;;
            FAIL) ((failed++)) ;;
            SKIP) ((skipped++)) ;;
        esac
    done

    echo "$backend: Passed=$passed Failed=$failed Skipped=$skipped"
done

# 終了コード
if [ "$INCONSISTENT_TESTS" -eq 0 ]; then
    echo ""
    echo -e "${GREEN}✓ All tests are consistent across backends${NC}"
    exit 0
else
    echo ""
    echo -e "${RED}✗ Some tests showed inconsistent behavior${NC}"
    exit 1
fi