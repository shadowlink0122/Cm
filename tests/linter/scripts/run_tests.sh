#!/bin/bash
# ============================================================
# Cm Linter テストランナー
# ============================================================

# set -e を削除: テスト失敗時も継続

# カラー定義
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

# パス設定
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_DIR="$(dirname "$SCRIPT_DIR")"
PROJECT_ROOT="$(dirname "$(dirname "$TEST_DIR")")"
CM="$PROJECT_ROOT/cm"

FIXTURES_DIR="$TEST_DIR/fixtures"
CONFIGS_DIR="$TEST_DIR/configs"
WORKSPACE_DIR="$TEST_DIR/workspace"

# カウンター
PASSED=0
FAILED=0

# 結果表示関数
pass() {
    echo -e "${GREEN}✓ PASS${NC}: $1"
    ((PASSED++))
}

fail() {
    echo -e "${RED}✗ FAIL${NC}: $1"
    echo "  $2"
    ((FAILED++))
}

# ワークスペース準備
prepare_workspace() {
    rm -rf "$WORKSPACE_DIR"/*
    mkdir -p "$WORKSPACE_DIR"
}

# ============================================================
# テストケース
# ============================================================

# Test 1: W001 未使用変数検出
test_w001_detection() {
    echo ""
    echo "=== Test: W001 Unused Variable Detection ==="
    
    local output
    output=$("$CM" check "$FIXTURES_DIR/invalid/unused_var.cm" 2>&1 || true)
    
    if echo "$output" | grep -q "\[W001\]"; then
        pass "W001 detected"
    else
        fail "W001 not detected" "$output"
    fi
}

# Test 2: L001 関数名チェック（--strict）
test_l001_function_naming() {
    echo ""
    echo "=== Test: L001 Function Naming (--strict) ==="
    
    local output
    output=$("$CM" check --strict "$FIXTURES_DIR/invalid/bad_naming.cm" 2>&1 || true)
    
    if echo "$output" | grep "\[L001\]" | grep -q "関数名"; then
        pass "L001 function naming detected"
    else
        fail "L001 function naming not detected" "$output"
    fi
}

# Test 3: L001 変数名チェック（--strict）
test_l001_variable_naming() {
    echo ""
    echo "=== Test: L001 Variable Naming (--strict) ==="
    
    local output
    output=$("$CM" check --strict "$FIXTURES_DIR/invalid/bad_naming.cm" 2>&1 || true)
    
    if echo "$output" | grep "\[L001\]" | grep -q "変数名"; then
        pass "L001 variable naming detected"
    else
        fail "L001 variable naming not detected" "$output"
    fi
}

# Test 4: L001 定数名チェック（--strict）
test_l001_constant_naming() {
    echo ""
    echo "=== Test: L001 Constant Naming (--strict) ==="
    
    local output
    output=$("$CM" check --strict "$FIXTURES_DIR/invalid/bad_naming.cm" 2>&1 || true)
    
    if echo "$output" | grep "\[L001\]" | grep -q "定数名"; then
        pass "L001 constant naming detected"
    else
        fail "L001 constant naming not detected" "$output"
    fi
}

# Test 5: L001 型名チェック（--strict）
test_l001_type_naming() {
    echo ""
    echo "=== Test: L001 Type Naming (--strict) ==="
    
    local output
    output=$("$CM" check --strict "$FIXTURES_DIR/invalid/bad_naming.cm" 2>&1 || true)
    
    if echo "$output" | grep "\[L001\]" | grep -q "構造体名"; then
        pass "L001 struct naming detected"
    else
        fail "L001 struct naming not detected" "$output"
    fi
}

# Test 5b: --strict なしでは L001 を報告しない
test_l001_requires_strict() {
    echo ""
    echo "=== Test: L001 Requires --strict ==="
    
    local output
    output=$("$CM" check "$FIXTURES_DIR/invalid/bad_naming.cm" 2>&1 || true)
    
    if echo "$output" | grep -q "\[L001\]"; then
        fail "L001 should not be reported without --strict" "$output"
    else
        pass "L001 silent without --strict"
    fi
}

# Test 6: 正しいコードは警告ゼロ
test_valid_code() {
    echo ""
    echo "=== Test: Valid Code (No Warnings) ==="
    
    local output
    output=$("$CM" check --strict "$FIXTURES_DIR/valid/good_code.cm" 2>&1 || true)
    
    # W001, L100-L103 が含まれていないことを確認
    if echo "$output" | grep -qE "\[(W001|L001)\]"; then
        fail "Unexpected lint warnings in valid code" "$output"
    else
        pass "No lint warnings in valid code"
    fi
}

# Test 7: 設定による W001 無効化
test_config_disable_w001() {
    echo ""
    echo "=== Test: Config Disable W001 ==="
    
    prepare_workspace
    
    # 設定ファイルをワークスペースにコピー
    cp "$CONFIGS_DIR/disable_w001.yml" "$WORKSPACE_DIR/.cmconfig.yml"
    cp "$FIXTURES_DIR/invalid/unused_var.cm" "$WORKSPACE_DIR/"
    
    local output
    output=$(cd "$WORKSPACE_DIR" && "$CM" check unused_var.cm 2>&1 || true)
    
    # W001 が表示されないことを確認
    if echo "$output" | grep -q "\[W001\]"; then
        fail "W001 should be disabled by config" "$output"
    else
        pass "W001 disabled by config"
    fi
}

# Test 8: 設定による命名規則無効化
test_config_disable_naming() {
    echo ""
    echo "=== Test: Config Disable Naming Rules ==="
    
    prepare_workspace
    
    # 設定ファイルをワークスペースにコピー
    cp "$CONFIGS_DIR/disable_naming.yml" "$WORKSPACE_DIR/.cmconfig.yml"
    cp "$FIXTURES_DIR/invalid/bad_naming.cm" "$WORKSPACE_DIR/"
    
    local output
    output=$(cd "$WORKSPACE_DIR" && "$CM" check --strict bad_naming.cm 2>&1 || true)
    
    # L100-L103 が表示されないことを確認
    if echo "$output" | grep -q "\[L001\]"; then
        fail "Naming rules should be disabled by config" "$output"
    else
        pass "Naming rules disabled by config"
    fi
}

# Test 9: 再帰チェック (-r)
test_recursive_check() {
    echo ""
    echo "=== Test: Recursive Check (-r) ==="
    
    local output
    output=$("$CM" check -r "$FIXTURES_DIR/invalid/" 2>&1 || true)
    
    # 複数ファイルがチェックされることを確認
    if echo "$output" | grep -q "ファイル数:"; then
        pass "Recursive check works"
    else
        fail "Recursive check failed" "$output"
    fi
}

# ============================================================
# メイン
# ============================================================

echo "=============================================="
echo "   Cm Linter Test Suite"
echo "=============================================="

# cmが存在するか確認
if [ ! -f "$CM" ]; then
    echo -e "${RED}Error: cm not found at $CM${NC}"
    echo "Please build the project first: cmake --build build"
    exit 1
fi

# テスト実行
test_w001_detection
test_l001_function_naming
test_l001_variable_naming
test_l001_constant_naming
test_l001_type_naming
test_l001_requires_strict
test_valid_code
test_config_disable_w001
test_config_disable_naming
test_recursive_check

# 結果サマリー
echo ""
echo "=============================================="
echo "   Test Results"
echo "=============================================="
echo -e "Passed: ${GREEN}$PASSED${NC}"
echo -e "Failed: ${RED}$FAILED${NC}"
echo ""

if [ $FAILED -gt 0 ]; then
    echo -e "${RED}Some tests failed!${NC}"
    exit 1
else
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
fi
