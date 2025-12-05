#!/bin/bash
# tests/e2e/run_all.sh
# E2E test runner for Cm language

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CM_BIN="${CM_BIN:-$SCRIPT_DIR/../../build/bin/cm}"
FAILED=0
PASSED=0
SKIPPED=0

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

echo "=========================================="
echo "Cm E2E Test Runner"
echo "=========================================="
echo "Binary: $CM_BIN"
echo ""

# Check if binary exists
if [ ! -f "$CM_BIN" ]; then
    echo -e "${YELLOW}Warning: Cm binary not found at $CM_BIN${NC}"
    echo "Skipping E2E tests"
    exit 0
fi

# Run interpreter tests
if [ -d "$SCRIPT_DIR/interpreter" ]; then
    echo "--- Interpreter Tests ---"
    for test_file in "$SCRIPT_DIR"/interpreter/*.cm; do
        [ -f "$test_file" ] || continue
        
        name=$(basename "$test_file" .cm)
        expected="$SCRIPT_DIR/interpreter/$name.expected"
        
        if [ ! -f "$expected" ]; then
            echo -e "${YELLOW}SKIP${NC}: $name (no .expected file)"
            ((SKIPPED++))
            continue
        fi
        
        actual=$("$CM_BIN" run "$test_file" 2>&1) || true
        expected_content=$(cat "$expected")
        
        if [ "$actual" = "$expected_content" ]; then
            echo -e "${GREEN}PASS${NC}: $name"
            ((PASSED++))
        else
            echo -e "${RED}FAIL${NC}: $name"
            echo "  Expected: $expected_content"
            echo "  Actual:   $actual"
            ((FAILED++))
        fi
    done
fi

# Run codegen tests
if [ -d "$SCRIPT_DIR/codegen" ]; then
    echo ""
    echo "--- Codegen Tests ---"
    for test_file in "$SCRIPT_DIR"/codegen/**/*.cm; do
        [ -f "$test_file" ] || continue
        
        name=$(basename "$test_file" .cm)
        expected="${test_file%.cm}.expected"
        
        if [ ! -f "$expected" ]; then
            echo -e "${YELLOW}SKIP${NC}: $name (no .expected file)"
            ((SKIPPED++))
            continue
        fi
        
        # Generate and compile
        "$CM_BIN" build "$test_file" --target native -o /tmp/cm_test_$name 2>&1 || {
            echo -e "${RED}FAIL${NC}: $name (build failed)"
            ((FAILED++))
            continue
        }
        
        actual=$(/tmp/cm_test_$name 2>&1) || true
        expected_content=$(cat "$expected")
        
        if [ "$actual" = "$expected_content" ]; then
            echo -e "${GREEN}PASS${NC}: $name"
            ((PASSED++))
        else
            echo -e "${RED}FAIL${NC}: $name"
            ((FAILED++))
        fi
        
        rm -f /tmp/cm_test_$name
    done
fi

echo ""
echo "=========================================="
echo "Results: $PASSED passed, $FAILED failed, $SKIPPED skipped"
echo "=========================================="

exit $FAILED
