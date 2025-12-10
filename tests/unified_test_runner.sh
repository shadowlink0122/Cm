#!/bin/bash

# Unified Test Runner for Cm Language
# Supports: interpreter, llvm backends with the same test items

# set -e を削除（エラーが発生してもスクリプトを継続）

# 設定
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Windows対応: 実行ファイルの拡張子
if [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "cygwin" ]] || [[ "$OSTYPE" == "win32" ]]; then
    CM_EXECUTABLE="$PROJECT_ROOT/cm.exe"
    IS_WINDOWS=true
else
    CM_EXECUTABLE="$PROJECT_ROOT/cm"
    IS_WINDOWS=false
fi

TEST_DIR="$PROJECT_ROOT/tests/test_programs"
TEMP_DIR="$PROJECT_ROOT/.tmp/test_runner"

# カラー出力
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# デフォルト値
BACKEND="interpreter"
CATEGORIES=""
VERBOSE=false
PARALLEL=false
TIMEOUT=5

# タイムアウトコマンドの検出
TIMEOUT_CMD=""
if command -v timeout >/dev/null 2>&1; then
    TIMEOUT_CMD="timeout"
elif command -v gtimeout >/dev/null 2>&1; then
    TIMEOUT_CMD="gtimeout"
fi

# ヘルプメッセージ
usage() {
    echo "Usage: $0 [OPTIONS]"
    echo "Options:"
    echo "  -b, --backend <backend>    Test backend: interpreter|typescript|rust|cpp|llvm (default: interpreter)"
    echo "  -c, --category <category>  Test categories (comma-separated, default: all)"
    echo "  -v, --verbose              Show detailed output"
    echo "  -p, --parallel             Run tests in parallel (experimental)"
    echo "  -t, --timeout <seconds>    Test timeout in seconds (default: 5)"
    echo "  -h, --help                 Show this help message"
    echo ""
    echo "Categories: basic, control_flow, errors, formatting, functions, macros, modules, overload, types"
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
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -p|--parallel)
            PARALLEL=true
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

# バックエンド検証
if [[ ! "$BACKEND" =~ ^(interpreter|typescript|rust|cpp|llvm)$ ]]; then
    echo "Error: Invalid backend '$BACKEND'"
    echo "Valid backends: interpreter, typescript, rust, cpp, llvm"
    exit 1
fi

# カテゴリー設定
if [ -z "$CATEGORIES" ]; then
    CATEGORIES="basic control_flow errors formatting types functions"
    # マクロとモジュールは未実装なので、インタプリタ以外ではスキップ
    if [ "$BACKEND" = "interpreter" ]; then
        CATEGORIES="$CATEGORIES macros modules"
    fi
fi

# 一時ディレクトリ作成
mkdir -p "$TEMP_DIR"

# テスト結果カウンタ
TOTAL=0
PASSED=0
FAILED=0
SKIPPED=0

# ログファイル
LOG_FILE="$TEMP_DIR/test_${BACKEND}_$(date +%Y%m%d_%H%M%S).log"

# ログ出力関数
log() {
    echo "$1" | tee -a "$LOG_FILE"
}

# テスト実行関数
run_single_test() {
    local test_file="$1"
    local test_name="$(basename "${test_file%.cm}")"
    local category="$(basename "$(dirname "$test_file")")"
    local expect_file="${test_file%.cm}.expect"
    local backend_expect_file="${test_file%.cm}.expect.${BACKEND}"
    local output_file="$TEMP_DIR/${category}_${test_name}.out"
    local error_file="$TEMP_DIR/${category}_${test_name}.err"

    # バックエンド固有のexpectファイルがあれば優先して使用
    if [ -f "$backend_expect_file" ]; then
        expect_file="$backend_expect_file"
    fi

    # expectファイルがない場合はスキップ
    if [ ! -f "$expect_file" ]; then
        echo -e "${YELLOW}[SKIP]${NC} $category/$test_name - No expect file"
        ((SKIPPED++))
        return
    fi

    # マクロ/モジュールテストは未実装のため特別処理
    if [[ "$category" == "macros" || "$category" == "modules" ]]; then
        # expectファイルに「未実装」が含まれているかチェック
        if grep -q "未実装\|not implemented" "$expect_file" 2>/dev/null; then
            if [ "$BACKEND" != "interpreter" ]; then
                echo -e "${YELLOW}[SKIP]${NC} $category/$test_name - Not implemented"
                ((SKIPPED++))
                return
            fi
        fi
    fi

    local exit_code=0

    # タイムアウト付きコマンド実行のヘルパー関数
    run_with_timeout() {
        local cmd="$@"
        if [ -n "$TIMEOUT_CMD" ]; then
            $TIMEOUT_CMD "$TIMEOUT" $cmd
        else
            # タイムアウトコマンドがない場合は直接実行
            $cmd
        fi
    }

    case "$BACKEND" in
        interpreter)
            # インタプリタで実行
            run_with_timeout "$CM_EXECUTABLE" run "$test_file" > "$output_file" 2>&1 || exit_code=$?
            ;;

        typescript)
            # TypeScriptにコンパイルして実行
            local ts_dir="$TEMP_DIR/ts_${test_name}"
            rm -rf "$ts_dir"

            # コンパイル（エラー時は出力ファイルにエラーメッセージを書き込む）
            run_with_timeout "$CM_EXECUTABLE" compile --emit-ts "$test_file" -o "$ts_dir" > "$output_file" 2>&1 || exit_code=$?

            if [ $exit_code -eq 0 ]; then
                # TypeScriptプロジェクトのビルドと実行
                pushd "$ts_dir" > /dev/null 2>&1
                npm install > /dev/null 2>&1 || exit_code=$?
                if [ $exit_code -eq 0 ]; then
                    npm run build > /dev/null 2>&1 || exit_code=$?
                    if [ $exit_code -eq 0 ]; then
                        run_with_timeout node main.js > "$output_file" 2>&1 || exit_code=$?
                    fi
                fi
                popd > /dev/null 2>&1
            fi
            ;;

        rust)
            # Rustにコンパイルして実行
            local rust_dir="$TEMP_DIR/rust_${test_name}"
            rm -rf "$rust_dir"

            # コンパイル（エラー時は出力ファイルにエラーメッセージを書き込む）
            run_with_timeout "$CM_EXECUTABLE" compile --emit-rust "$test_file" -o "$rust_dir" > "$output_file" 2>&1 || exit_code=$?

            if [ $exit_code -eq 0 ]; then
                # Rustコンパイルと実行
                pushd "$rust_dir" > /dev/null 2>&1
                rustc main.rs -o main 2> /dev/null || exit_code=$?
                if [ $exit_code -eq 0 ]; then
                    run_with_timeout ./main > "$output_file" 2>&1 || exit_code=$?
                fi
                popd > /dev/null 2>&1
            fi
            ;;

        cpp)
            # C++にコンパイルして実行
            local cpp_dir="$TEMP_DIR/cpp_${test_name}"
            rm -rf "$cpp_dir"

            # コンパイル
            run_with_timeout "$CM_EXECUTABLE" compile --emit-cpp "$test_file" -o "$cpp_dir" > /dev/null 2>&1 || exit_code=$?

            if [ $exit_code -eq 0 ]; then
                # C++コンパイルと実行
                pushd "$cpp_dir" > /dev/null 2>&1
                g++ -std=c++17 main.cpp -o main 2> /dev/null || exit_code=$?
                if [ $exit_code -eq 0 ]; then
                    run_with_timeout ./main > "$output_file" 2>&1 || exit_code=$?
                fi
                popd > /dev/null 2>&1
            fi
            ;;

        llvm)
            # LLVMバックエンドでネイティブコードにコンパイルして実行
            local llvm_exec="$TEMP_DIR/llvm_${test_name}"
            # Windows対応: .exe拡張子
            if [ "$IS_WINDOWS" = true ]; then
                llvm_exec="${llvm_exec}.exe"
            fi
            rm -f "$llvm_exec"

            # LLVM経由でネイティブ実行ファイル生成（エラー時は出力ファイルにエラーメッセージを書き込む）
            run_with_timeout "$CM_EXECUTABLE" compile --emit-llvm "$test_file" -o "$llvm_exec" > "$output_file" 2>&1 || exit_code=$?

            if [ $exit_code -eq 0 ] && [ -f "$llvm_exec" ]; then
                # 実行
                "$llvm_exec" > "$output_file" 2>&1 || exit_code=$?
            fi
            ;;
    esac

    # タイムアウト処理
    if [ $exit_code -eq 124 ] || [ $exit_code -eq 143 ]; then
        echo -e "${RED}[FAIL]${NC} $category/$test_name - Timeout (>${TIMEOUT}s)"
        ((FAILED++))
        return
    fi

    # エラーファイルに期待される出力がある場合（コンパイルエラーテスト等）
    if grep -q "error\|Error\|エラー" "$expect_file" 2>/dev/null; then
        # エラーが期待される場合
        if [ $exit_code -ne 0 ]; then
            # エラー出力を比較
            if diff -q "$expect_file" "$output_file" > /dev/null 2>&1; then
                echo -e "${GREEN}[PASS]${NC} $category/$test_name"
                ((PASSED++))
            else
                echo -e "${RED}[FAIL]${NC} $category/$test_name - Error output mismatch"
                if [ "$VERBOSE" = true ]; then
                    echo "Expected:"
                    cat "$expect_file"
                    echo "Got:"
                    cat "$output_file"
                fi
                ((FAILED++))
            fi
        else
            echo -e "${RED}[FAIL]${NC} $category/$test_name - Expected error but succeeded"
            ((FAILED++))
        fi
    else
        # 正常終了が期待される場合
        if [ $exit_code -ne 0 ]; then
            echo -e "${RED}[FAIL]${NC} $category/$test_name - Runtime error (exit code: $exit_code)"
            if [ "$VERBOSE" = true ]; then
                cat "$output_file"
            fi
            ((FAILED++))
        else
            # 出力を比較
            if diff -q "$expect_file" "$output_file" > /dev/null 2>&1; then
                echo -e "${GREEN}[PASS]${NC} $category/$test_name"
                ((PASSED++))
            else
                echo -e "${RED}[FAIL]${NC} $category/$test_name - Output mismatch"
                if [ "$VERBOSE" = true ]; then
                    echo "Expected:"
                    cat "$expect_file"
                    echo "Got:"
                    cat "$output_file"
                fi
                ((FAILED++))
            fi
        fi
    fi
}

# テスト実行メイン
main() {
    log "=========================================="
    log "Cm Language Unified Test Runner"
    log "Backend: $BACKEND"
    log "Categories: $CATEGORIES"
    log "=========================================="
    log ""

    # カテゴリーごとにテスト実行
    for category in $CATEGORIES; do
        local category_dir="$TEST_DIR/$category"

        if [ ! -d "$category_dir" ]; then
            log "Warning: Category directory '$category' not found, skipping"
            continue
        fi

        log "Testing category: $category"
        log "----------------------------------------"

        # .cmファイルを検索
        for test_file in "$category_dir"/*.cm; do
            if [ -f "$test_file" ]; then
                ((TOTAL++))
                run_single_test "$test_file"
            fi
        done

        log ""
    done

    # 結果サマリー
    log "=========================================="
    log "Test Results Summary"
    log "=========================================="
    log "Total:   $TOTAL"
    log "Passed:  $PASSED"
    log "Failed:  $FAILED"
    log "Skipped: $SKIPPED"
    log ""

    if [ $FAILED -gt 0 ]; then
        log "Status: FAILED"
        exit 1
    else
        log "Status: SUCCESS"
        exit 0
    fi
}

# メイン実行
main