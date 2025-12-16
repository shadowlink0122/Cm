#!/bin/bash
# Cm言語テストランナー
# test_programs/配下のすべてのテストを実行

set -e

# カラー出力設定
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# デフォルト設定
BACKEND="interpreter"
VERBOSE=0
CM_BINARY="./cm"
TEST_DIR="tests/test_programs"
CATEGORIES=""
FILTER=""

# ヘルプメッセージ
usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -b, --backend BACKEND   実行バックエンド [interpreter|rust|ts|wasm] (default: interpreter)"
    echo "  -c, --category CAT      特定カテゴリのみ実行 (basic, control_flow, etc.)"
    echo "  -f, --filter PATTERN    ファイル名パターンでフィルタ"
    echo "  -v, --verbose           詳細出力を表示"
    echo "  -h, --help              このヘルプを表示"
    echo ""
    echo "Examples:"
    echo "  $0                      # インタープリタですべてのテスト実行"
    echo "  $0 -b rust              # Rustバックエンドでテスト"
    echo "  $0 -c basic             # basicカテゴリのみ実行"
    echo "  $0 -f \"hello*\"          # hello で始まるテストのみ"
    exit 0
}

# 引数パース
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
        -f|--filter)
            FILTER="$2"
            shift 2
            ;;
        -v|--verbose)
            VERBOSE=1
            shift
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

# cmバイナリの存在確認
if [ ! -x "$CM_BINARY" ]; then
    echo -e "${RED}Error: cm binary not found at $CM_BINARY${NC}"
    echo "Please build the project first with: make build"
    exit 1
fi

# テストディレクトリの存在確認
if [ ! -d "$TEST_DIR" ]; then
    echo -e "${RED}Error: Test directory not found at $TEST_DIR${NC}"
    exit 1
fi

# カテゴリリストの取得
if [ -z "$CATEGORIES" ]; then
    CATEGORIES=$(find "$TEST_DIR" -mindepth 1 -maxdepth 1 -type d ! -name ".*" -exec basename {} \; | sort)
fi

# 結果カウンタ
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0
SKIPPED_TESTS=0
FAILED_FILES=()

# テスト実行関数
run_test() {
    local test_file="$1"
    local expect_file="${test_file%.cm}.expect"
    local test_name=$(basename "$test_file" .cm)
    local category=$(basename $(dirname "$test_file"))

    # フィルタチェック
    if [ -n "$FILTER" ] && [[ ! "$test_name" =~ $FILTER ]]; then
        return
    fi

    TOTAL_TESTS=$((TOTAL_TESTS + 1))

    # .expectファイルの存在チェック
    if [ ! -f "$expect_file" ]; then
        echo -e "${YELLOW}[SKIP]${NC} $category/$test_name - No .expect file"
        SKIPPED_TESTS=$((SKIPPED_TESTS + 1))
        return
    fi

    # テスト実行
    local output_file="/tmp/cm_test_$$.out"
    local error_code=0

    # timeout コマンドの検出 (macOSではgtimeout)
    local TIMEOUT_CMD=""
    if command -v timeout >/dev/null 2>&1; then
        TIMEOUT_CMD="timeout 5"
    elif command -v gtimeout >/dev/null 2>&1; then
        TIMEOUT_CMD="gtimeout 5"
    fi

    case "$BACKEND" in
        interpreter)
            if [ -n "$TIMEOUT_CMD" ]; then
                $TIMEOUT_CMD "$CM_BINARY" run "$test_file" > "$output_file" 2>&1 || error_code=$?
            else
                "$CM_BINARY" run "$test_file" > "$output_file" 2>&1 || error_code=$?
            fi
            ;;
        rust)
            local rust_file="/tmp/cm_test_$$.rs"
            local rust_bin="/tmp/cm_test_$$"
            local compile_error="/tmp/cm_test_$$_compile.err"

            # Cmコンパイル
            if ! "$CM_BINARY" compile "$test_file" --emit-rust-v2 -o "$rust_file" > "$compile_error" 2>&1; then
                cat "$compile_error" > "$output_file"
                rm -f "$compile_error"
                error_code=1
            elif [ -f "$rust_file" ]; then
                # Rustコンパイル
                if ! rustc "$rust_file" -o "$rust_bin" > "$compile_error" 2>&1; then
                    cat "$compile_error" > "$output_file"
                    rm -f "$rust_file" "$compile_error"
                    error_code=1
                else
                    # 実行
                    if [ -n "$TIMEOUT_CMD" ]; then
                        $TIMEOUT_CMD "$rust_bin" > "$output_file" 2>&1 || error_code=$?
                    else
                        "$rust_bin" > "$output_file" 2>&1 || error_code=$?
                    fi
                    rm -f "$rust_file" "$rust_bin"
                fi
                rm -f "$compile_error"
            else
                echo "Failed to generate Rust code" > "$output_file"
                error_code=1
            fi
            ;;
        ts)
            local ts_file="/tmp/cm_test_$$.ts"
            local js_file="/tmp/cm_test_$$.js"
            local compile_error="/tmp/cm_test_$$_compile.err"

            # Cmコンパイル
            if ! "$CM_BINARY" compile "$test_file" --emit-ts-v2 -o "$ts_file" > "$compile_error" 2>&1; then
                cat "$compile_error" > "$output_file"
                rm -f "$compile_error"
                error_code=1
            elif [ -f "$ts_file" ]; then
                # Node.jsで直接実行（TypeScriptはJavaScriptとしても実行可能な単純なコードを生成）
                if [ -n "$TIMEOUT_CMD" ]; then
                    $TIMEOUT_CMD node "$ts_file" > "$output_file" 2>&1 || error_code=$?
                else
                    node "$ts_file" > "$output_file" 2>&1 || error_code=$?
                fi
                rm -f "$ts_file"
                rm -f "$compile_error"
            else
                echo "Failed to generate TypeScript code" > "$output_file"
                error_code=1
            fi
            ;;
        wasm)
            echo "WASM backend not yet implemented" > "$output_file"
            error_code=1
            ;;
        *)
            echo "Unknown backend: $BACKEND" > "$output_file"
            error_code=1
            ;;
    esac

    # 結果比較
    # return.cmのような非ゼロ終了コードが意図的な場合を考慮
    if [ $error_code -ne 0 ] && ! grep -q "エラー" "$expect_file" && [ -s "$expect_file" ]; then
        # 実行エラーかつ期待値にエラーが含まれていない、かつexpectファイルが空でない
        echo -e "${RED}[FAIL]${NC} $category/$test_name - Runtime error (exit code: $error_code)"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        FAILED_FILES+=("$test_file")
        if [ $VERBOSE -eq 1 ]; then
            echo "  Output:"
            cat "$output_file" | sed 's/^/    /'
        fi
    elif diff -q "$output_file" "$expect_file" > /dev/null 2>&1; then
        echo -e "${GREEN}[PASS]${NC} $category/$test_name"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "${RED}[FAIL]${NC} $category/$test_name - Output mismatch"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        FAILED_FILES+=("$test_file")
        if [ $VERBOSE -eq 1 ]; then
            echo "  Expected:"
            head -n 3 "$expect_file" | sed 's/^/    /'
            echo "  Got:"
            head -n 3 "$output_file" | sed 's/^/    /'
            echo "  Diff:"
            diff -u "$expect_file" "$output_file" | head -n 10 | sed 's/^/    /'
        fi
    fi

    rm -f "$output_file"
}

# メイン処理
echo "=========================================="
echo "Cm Language Test Runner"
echo "Backend: $BACKEND"
echo "Categories: $CATEGORIES"
if [ -n "$FILTER" ]; then
    echo "Filter: $FILTER"
fi
echo "=========================================="
echo ""

# カテゴリごとにテスト実行
for category in $CATEGORIES; do
    category_dir="$TEST_DIR/$category"
    if [ ! -d "$category_dir" ]; then
        continue
    fi

    # カテゴリ内の.cmファイルを検索
    test_files=$(find "$category_dir" -name "*.cm" | sort)
    if [ -z "$test_files" ]; then
        continue
    fi

    echo "Testing category: $category"
    echo "----------------------------------------"

    for test_file in $test_files; do
        run_test "$test_file"
    done

    echo ""
done

# 結果サマリ
echo "=========================================="
echo "Test Results Summary"
echo "=========================================="
echo -e "Total:   $TOTAL_TESTS"
echo -e "Passed:  ${GREEN}$PASSED_TESTS${NC}"
echo -e "Failed:  ${RED}$FAILED_TESTS${NC}"
echo -e "Skipped: ${YELLOW}$SKIPPED_TESTS${NC}"

if [ $FAILED_TESTS -gt 0 ]; then
    echo ""
    echo "Failed tests:"
    for failed in "${FAILED_FILES[@]}"; do
        echo "  - $failed"
    done
    exit 1
fi

if [ $PASSED_TESTS -eq $TOTAL_TESTS ]; then
    echo -e "\n${GREEN}All tests passed!${NC}"
fi

exit 0