#!/bin/bash

# Cm統一テストランナー
# 複数バックエンド（interpreter, rust, typescript, wasm）をサポート

set -e

# カラー出力
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# デフォルト設定
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
CM_EXEC="$PROJECT_ROOT/cm"
TEST_DIR="$PROJECT_ROOT/tests/test_programs"
TEMP_DIR="$SCRIPT_DIR/../.tmp"

# コマンドラインオプションのデフォルト値
BACKEND="interpreter"
SUITE="all"
MODE="quick"
OUTPUT_DIR=""
VERBOSE=false
KEEP_ARTIFACTS=false
TEST_PATTERN=""

# テストカウンター
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0
SKIPPED_TESTS=0

# ヘルプメッセージ
show_help() {
    echo "Cm Unified Test Runner"
    echo ""
    echo "Usage: $0 [OPTIONS] [TEST_PATTERN]"
    echo ""
    echo "Options:"
    echo "  --backend=BACKEND    Test backend (interpreter|rust|typescript|wasm|all)"
    echo "                       Default: interpreter"
    echo "  --suite=SUITE        Test suite (basic|control_flow|functions|types|all)"
    echo "                       Default: all"
    echo "  --mode=MODE          Test mode (quick|full|regression)"
    echo "                       Default: quick"
    echo "  --output=DIR         Output directory for artifacts"
    echo "  --verbose, -v        Enable verbose output"
    echo "  --keep-artifacts     Keep generated files after test"
    echo "  --help, -h           Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0 --backend=interpreter --suite=basic"
    echo "  $0 --backend=rust --verbose"
    echo "  $0 --backend=all basic/hello_world.cm"
    echo ""
    echo "Available backends:"
    echo "  interpreter  - Run with MIR interpreter (implemented)"
    echo "  rust        - Transpile to Rust and compile (planned)"
    echo "  typescript  - Transpile to TypeScript (planned)"
    echo "  wasm        - Compile to WebAssembly (planned)"
    exit 0
}

# オプション解析
while [[ $# -gt 0 ]]; do
    case $1 in
        --backend=*)
            BACKEND="${1#*=}"
            ;;
        --suite=*)
            SUITE="${1#*=}"
            ;;
        --mode=*)
            MODE="${1#*=}"
            ;;
        --output=*)
            OUTPUT_DIR="${1#*=}"
            ;;
        --verbose|-v)
            VERBOSE=true
            ;;
        --keep-artifacts)
            KEEP_ARTIFACTS=true
            ;;
        --help|-h)
            show_help
            ;;
        --*)
            echo "Unknown option: $1"
            show_help
            ;;
        *)
            TEST_PATTERN="$1"
            ;;
    esac
    shift
done

# 出力ディレクトリ設定
if [ -z "$OUTPUT_DIR" ]; then
    OUTPUT_DIR="$TEMP_DIR"
fi
mkdir -p "$OUTPUT_DIR"

# クリーンアップ
cleanup() {
    if [ "$KEEP_ARTIFACTS" = false ]; then
        rm -rf "$TEMP_DIR"
    fi
}
trap cleanup EXIT

# 詳細出力
log_verbose() {
    if [ "$VERBOSE" = true ]; then
        echo -e "${CYAN}[VERBOSE]${NC} $1"
    fi
}

# テスト結果を表示
print_result() {
    local test_name=$1
    local backend=$2
    local status=$3
    local message=$4

    case $status in
        PASS)
            echo -e "${GREEN}✓${NC} [$backend] $test_name"
            ((PASSED_TESTS++))
            ;;
        FAIL)
            echo -e "${RED}✗${NC} [$backend] $test_name"
            if [ -n "$message" ]; then
                echo "  $message"
            fi
            ((FAILED_TESTS++))
            ;;
        SKIP)
            echo -e "${YELLOW}⊘${NC} [$backend] $test_name (skipped: $message)"
            ((SKIPPED_TESTS++))
            ;;
    esac
    ((TOTAL_TESTS++))
}

# インタープリタバックエンド
run_interpreter() {
    local test_file=$1
    local expect_file=${test_file%.cm}.expect
    local test_name=$(basename $test_file .cm)
    local output_file="$OUTPUT_DIR/${test_name}_interpreter.out"

    log_verbose "Running interpreter: $test_file"

    if [ ! -f "$expect_file" ]; then
        print_result "$test_name" "interpreter" "SKIP" "No expect file"
        return
    fi

    # インタープリタ実行
    $CM_EXEC run "$test_file" > "$output_file" 2>&1
    local exit_code=$?

    # 結果比較
    if diff -q "$expect_file" "$output_file" > /dev/null 2>&1; then
        print_result "$test_name" "interpreter" "PASS" ""
    else
        print_result "$test_name" "interpreter" "FAIL" "Output mismatch"
        if [ "$VERBOSE" = true ]; then
            echo "  Expected:"
            cat "$expect_file" | head -3 | sed 's/^/    /'
            echo "  Got:"
            cat "$output_file" | head -3 | sed 's/^/    /'
        fi
    fi
}

# Rustバックエンド
run_rust() {
    local test_file=$1
    local expect_file=${test_file%.cm}.expect
    local test_name=$(basename $test_file .cm)
    local rust_file="$OUTPUT_DIR/${test_name}.rs"
    local exe_file="$OUTPUT_DIR/${test_name}_rust"
    local output_file="$OUTPUT_DIR/${test_name}_rust.out"

    log_verbose "Running Rust backend: $test_file"

    if [ ! -f "$expect_file" ]; then
        print_result "$test_name" "rust" "SKIP" "No expect file"
        return
    fi

    # Rustトランスパイル
    if ! $CM_EXEC --emit-rust "$test_file" -o "$rust_file" 2>/dev/null; then
        print_result "$test_name" "rust" "SKIP" "Not implemented"
        return
    fi

    # Rustコンパイル
    if ! rustc "$rust_file" -o "$exe_file" 2>/dev/null; then
        print_result "$test_name" "rust" "FAIL" "Rust compilation failed"
        return
    fi

    # 実行
    if "$exe_file" > "$output_file" 2>&1; then
        echo "EXIT: 0" >> "$output_file"
    else
        echo "EXIT: $?" >> "$output_file"
    fi

    # 結果比較
    if diff -q "$expect_file" "$output_file" > /dev/null 2>&1; then
        print_result "$test_name" "rust" "PASS" ""
    else
        print_result "$test_name" "rust" "FAIL" "Output mismatch"
    fi
}

# TypeScriptバックエンド
run_typescript() {
    local test_file=$1
    local expect_file=${test_file%.cm}.expect
    local test_name=$(basename $test_file .cm)
    local ts_file="$OUTPUT_DIR/${test_name}.ts"
    local js_file="$OUTPUT_DIR/${test_name}.js"
    local output_file="$OUTPUT_DIR/${test_name}_ts.out"

    log_verbose "Running TypeScript backend: $test_file"

    if [ ! -f "$expect_file" ]; then
        print_result "$test_name" "typescript" "SKIP" "No expect file"
        return
    fi

    # TypeScriptトランスパイル
    if ! $CM_EXEC --emit-typescript "$test_file" -o "$ts_file" 2>/dev/null; then
        print_result "$test_name" "typescript" "SKIP" "Not implemented"
        return
    fi

    # TypeScriptコンパイル
    if ! tsc "$ts_file" --outFile "$js_file" 2>/dev/null; then
        print_result "$test_name" "typescript" "FAIL" "TypeScript compilation failed"
        return
    fi

    # Node.js実行
    if node "$js_file" > "$output_file" 2>&1; then
        echo "EXIT: 0" >> "$output_file"
    else
        echo "EXIT: $?" >> "$output_file"
    fi

    # 結果比較
    if diff -q "$expect_file" "$output_file" > /dev/null 2>&1; then
        print_result "$test_name" "typescript" "PASS" ""
    else
        print_result "$test_name" "typescript" "FAIL" "Output mismatch"
    fi
}

# WASMバックエンド
run_wasm() {
    local test_file=$1
    local expect_file=${test_file%.cm}.expect
    local test_name=$(basename $test_file .cm)
    local wasm_file="$OUTPUT_DIR/${test_name}.wasm"
    local output_file="$OUTPUT_DIR/${test_name}_wasm.out"

    log_verbose "Running WASM backend: $test_file"

    if [ ! -f "$expect_file" ]; then
        print_result "$test_name" "wasm" "SKIP" "No expect file"
        return
    fi

    # WASMコンパイル
    if ! $CM_EXEC --emit-wasm "$test_file" -o "$wasm_file" 2>/dev/null; then
        print_result "$test_name" "wasm" "SKIP" "Not implemented"
        return
    fi

    # WASM実行
    if command -v wasmtime &> /dev/null; then
        if wasmtime "$wasm_file" > "$output_file" 2>&1; then
            echo "EXIT: 0" >> "$output_file"
        else
            echo "EXIT: $?" >> "$output_file"
        fi
    else
        print_result "$test_name" "wasm" "SKIP" "wasmtime not installed"
        return
    fi

    # 結果比較
    if diff -q "$expect_file" "$output_file" > /dev/null 2>&1; then
        print_result "$test_name" "wasm" "PASS" ""
    else
        print_result "$test_name" "wasm" "FAIL" "Output mismatch"
    fi
}

# バックエンドごとにテストを実行
run_with_backend() {
    local test_file=$1
    local backend=$2

    case $backend in
        interpreter)
            run_interpreter "$test_file"
            ;;
        rust)
            run_rust "$test_file"
            ;;
        typescript)
            run_typescript "$test_file"
            ;;
        wasm)
            run_wasm "$test_file"
            ;;
        all)
            run_interpreter "$test_file"
            run_rust "$test_file"
            run_typescript "$test_file"
            run_wasm "$test_file"
            ;;
        *)
            echo "Unknown backend: $backend"
            exit 1
            ;;
    esac
}

# スイートを実行
run_suite() {
    local suite_dir=$1
    local suite_name=$(basename $suite_dir)

    echo -e "\n${BLUE}=== Suite: $suite_name ===${NC}"

    for test_file in "$suite_dir"/*.cm; do
        [ -f "$test_file" ] || continue
        run_with_backend "$test_file" "$BACKEND"
    done
}

# メイン処理
echo "Cm Unified Test Runner"
echo "Backend: $BACKEND"
echo "Suite: $SUITE"
echo "Mode: $MODE"
echo ""

# cmコンパイラをビルド（必要な場合）
if [ ! -f "$CM_EXEC" ]; then
    echo "Building cm compiler..."
    cd "$PROJECT_ROOT"
    cmake -B build
    cmake --build build
    cd -
fi

# テストパターンが指定された場合
if [ -n "$TEST_PATTERN" ]; then
    if [ -f "$TEST_DIR/$TEST_PATTERN" ]; then
        echo -e "${BLUE}=== Running: $TEST_PATTERN ===${NC}"
        run_with_backend "$TEST_DIR/$TEST_PATTERN" "$BACKEND"
    else
        echo "Error: Test file not found: $TEST_PATTERN"
        exit 1
    fi
# スイート実行
elif [ "$SUITE" == "all" ]; then
    for suite_dir in "$TEST_DIR"/*; do
        [ -d "$suite_dir" ] || continue
        run_suite "$suite_dir"
    done
else
    if [ -d "$TEST_DIR/$SUITE" ]; then
        run_suite "$TEST_DIR/$SUITE"
    else
        echo "Error: Suite not found: $SUITE"
        exit 1
    fi
fi

# サマリー表示
echo ""
echo -e "${BLUE}=== Test Summary ===${NC}"
echo "Total:   $TOTAL_TESTS"
echo -e "Passed:  ${GREEN}$PASSED_TESTS${NC}"
echo -e "Failed:  ${RED}$FAILED_TESTS${NC}"
echo -e "Skipped: ${YELLOW}$SKIPPED_TESTS${NC}"

# 実装状況
echo ""
echo -e "${BLUE}=== Backend Status ===${NC}"
echo -e "Interpreter: ${GREEN}✓ Implemented${NC}"
echo -e "Rust:        ${YELLOW}⊘ Planned${NC}"
echo -e "TypeScript:  ${YELLOW}⊘ Planned${NC}"
echo -e "WASM:        ${YELLOW}⊘ Planned${NC}"

if [ $FAILED_TESTS -eq 0 ]; then
    echo -e "\n${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "\n${RED}Some tests failed.${NC}"
    exit 1
fi