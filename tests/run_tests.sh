#!/bin/bash

# Cmテスト実行スクリプト
# 様々なテストモードを簡単に実行できるラッパー

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUNNERS_DIR="$SCRIPT_DIR/runners"

# カラー出力
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

# デフォルト
MODE="quick"

# ヘルプ
show_help() {
    echo "Cm Test Runner"
    echo ""
    echo "Usage: $0 [MODE]"
    echo ""
    echo "Modes:"
    echo "  quick        Run basic tests with interpreter (default)"
    echo "  full         Run all tests with interpreter"
    echo "  regression   Run regression tests"
    echo "  integration  Run integration tests"
    echo "  all          Run all test types"
    echo ""
    echo "Backend-specific modes:"
    echo "  rust         Test Rust backend (when implemented)"
    echo "  typescript   Test TypeScript backend (when implemented)"
    echo "  wasm         Test WASM backend (when implemented)"
    echo ""
    echo "Examples:"
    echo "  $0              # Run quick tests"
    echo "  $0 full         # Run all tests with interpreter"
    echo "  $0 regression   # Run regression tests"
    echo "  $0 rust         # Test Rust backend"
    exit 0
}

# オプション処理
if [ $# -gt 0 ]; then
    MODE="$1"
fi

if [ "$MODE" == "--help" ] || [ "$MODE" == "-h" ]; then
    show_help
fi

echo -e "${BLUE}=== Cm Test System ===${NC}"
echo "Mode: $MODE"
echo ""

case "$MODE" in
    quick)
        echo -e "${GREEN}Running quick tests (basic suite with interpreter)...${NC}"
        "$RUNNERS_DIR/test_runner.sh" --backend=interpreter --suite=basic
        ;;

    full)
        echo -e "${GREEN}Running full interpreter tests...${NC}"
        "$RUNNERS_DIR/test_runner.sh" --backend=interpreter --suite=all
        ;;

    regression)
        echo -e "${GREEN}Running regression tests...${NC}"
        "$RUNNERS_DIR/regression.sh"
        ;;

    integration)
        echo -e "${GREEN}Running integration tests...${NC}"
        "$RUNNERS_DIR/integration.sh" --backends=interpreter --suite=basic
        ;;

    all)
        echo -e "${GREEN}Running all test types...${NC}"

        echo -e "\n${BLUE}--- Full Tests ---${NC}"
        "$RUNNERS_DIR/test_runner.sh" --backend=interpreter --suite=all

        echo -e "\n${BLUE}--- Regression Tests ---${NC}"
        "$RUNNERS_DIR/regression.sh"

        echo -e "\n${BLUE}--- Integration Tests ---${NC}"
        "$RUNNERS_DIR/integration.sh" --backends=interpreter --suite=basic
        ;;

    rust)
        echo -e "${YELLOW}Testing Rust backend...${NC}"
        "$RUNNERS_DIR/test_runner.sh" --backend=rust --suite=all
        ;;

    typescript)
        echo -e "${YELLOW}Testing TypeScript backend...${NC}"
        "$RUNNERS_DIR/test_runner.sh" --backend=typescript --suite=all
        ;;

    wasm)
        echo -e "${YELLOW}Testing WASM backend...${NC}"
        "$RUNNERS_DIR/test_runner.sh" --backend=wasm --suite=all
        ;;

    *)
        echo "Unknown mode: $MODE"
        echo "Use '$0 --help' for available modes"
        exit 1
        ;;
esac

echo ""
echo -e "${GREEN}✓ Test execution completed${NC}"