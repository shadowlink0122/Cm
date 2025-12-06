#!/bin/bash

# Integration test runner for Cm compiler
# Tests that interpreter, Rust, and TypeScript backends produce the same output

set -e

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Paths
CM_COMPILER="../../cm"
TEST_DIR="$(dirname "$0")"
TEMP_DIR=".tmp_test"

# Counters
PASSED=0
FAILED=0

# Clean up function
cleanup() {
    rm -rf "$TEMP_DIR"
}

# Run test function
run_test() {
    local test_file="$1"
    local test_name="$(basename "$test_file" .cm)"
    local expect_file="${test_file%.cm}.expect"

    echo -n "Testing $test_name... "

    # Check if expect file exists
    if [ ! -f "$expect_file" ]; then
        echo -e "${YELLOW}SKIP${NC} (no .expect file)"
        return
    fi

    # Create temp directory
    mkdir -p "$TEMP_DIR"

    # Get expected output
    local expected_output="$(cat "$expect_file")"

    # Test 1: Interpreter
    local interp_output="$($CM_COMPILER run "$test_file" 2>/dev/null || echo "ERROR")"

    # Test 2: Rust transpiler
    local rust_dir="$TEMP_DIR/rust_$test_name"
    $CM_COMPILER compile --emit-rust "$test_file" -o "$rust_dir" 2>/dev/null

    # Build and run Rust code
    local rust_output=""
    if [ -d "$rust_dir" ]; then
        (cd "$rust_dir" && cargo build --quiet 2>/dev/null && cargo run --quiet 2>/dev/null) > "$TEMP_DIR/rust_output.txt" 2>/dev/null || echo "ERROR" > "$TEMP_DIR/rust_output.txt"
        rust_output="$(cat "$TEMP_DIR/rust_output.txt")"
    else
        rust_output="ERROR"
    fi

    # Test 3: TypeScript transpiler
    local ts_dir="$TEMP_DIR/ts_$test_name"
    $CM_COMPILER compile --emit-ts "$test_file" -o "$ts_dir" 2>/dev/null

    # Build and run TypeScript code
    local ts_output=""
    if [ -d "$ts_dir" ]; then
        (cd "$ts_dir" && pnpm install --silent 2>/dev/null && pnpm run build --silent 2>/dev/null && node main.js 2>/dev/null) > "$TEMP_DIR/ts_output.txt" 2>/dev/null || echo "ERROR" > "$TEMP_DIR/ts_output.txt"
        ts_output="$(cat "$TEMP_DIR/ts_output.txt")"
    else
        ts_output="ERROR"
    fi

    # Compare outputs
    local all_passed=true

    if [ "$interp_output" != "$expected_output" ]; then
        all_passed=false
        echo -e "\n  ${RED}✗${NC} Interpreter output mismatch"
        echo "    Expected: $expected_output"
        echo "    Got:      $interp_output"
    fi

    if [ "$rust_output" != "$expected_output" ]; then
        all_passed=false
        echo -e "\n  ${RED}✗${NC} Rust output mismatch"
        echo "    Expected: $expected_output"
        echo "    Got:      $rust_output"
    fi

    if [ "$ts_output" != "$expected_output" ]; then
        all_passed=false
        echo -e "\n  ${RED}✗${NC} TypeScript output mismatch"
        echo "    Expected: $expected_output"
        echo "    Got:      $ts_output"
    fi

    if [ "$all_passed" = true ]; then
        echo -e "${GREEN}PASS${NC}"
        ((PASSED++))
    else
        echo -e "${RED}FAIL${NC}"
        ((FAILED++))
    fi

    # Clean up test artifacts
    rm -rf "$rust_dir" "$ts_dir"
}

# Main execution
echo "==================================="
echo "Cm Compiler Integration Tests"
echo "==================================="
echo ""

# Check if compiler exists
if [ ! -f "$CM_COMPILER" ]; then
    echo -e "${RED}Error: Cm compiler not found at $CM_COMPILER${NC}"
    exit 1
fi

# Check for required tools
if ! command -v cargo &> /dev/null; then
    echo -e "${YELLOW}Warning: cargo not found, Rust tests will be skipped${NC}"
fi

if ! command -v pnpm &> /dev/null; then
    echo -e "${YELLOW}Warning: pnpm not found, TypeScript tests will be skipped${NC}"
fi

# Run all tests
for test_file in "$TEST_DIR"/*.cm; do
    if [ -f "$test_file" ]; then
        run_test "$test_file"
    fi
done

# Summary
echo ""
echo "==================================="
echo "Test Summary"
echo "==================================="
echo -e "Passed: ${GREEN}$PASSED${NC}"
echo -e "Failed: ${RED}$FAILED${NC}"

# Clean up
cleanup

# Exit with appropriate code
if [ $FAILED -eq 0 ]; then
    echo -e "\n${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "\n${RED}Some tests failed.${NC}"
    exit 1
fi