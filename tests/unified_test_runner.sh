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

PROGRAMS_DIR="$PROJECT_ROOT/tests/programs"
TEMP_DIR="$PROJECT_ROOT/.tmp/test_runner"

# カラー出力
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 子プロセスのPIDを追跡
CHILD_PIDS=()

# クリーンアップ関数
cleanup() {
    echo -e "\n${YELLOW}[INTERRUPTED]${NC} テストを中断しています..."
    # 子プロセスを終了
    for pid in "${CHILD_PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            kill -TERM "$pid" 2>/dev/null
            kill -KILL "$pid" 2>/dev/null
        fi
    done
    # プロセスグループ全体を終了
    kill -TERM 0 2>/dev/null
    exit 130
}

# シグナルトラップ設定
trap cleanup SIGINT SIGTERM

# デフォルト値
# NOTE: interpreterバックエンドは未実装のため、デフォルトはjit
BACKEND="jit"
CATEGORIES=""
SUITE=""
VERBOSE=false
OPT_LEVEL=${OPT_LEVEL:-3}  # デフォルトはO3
PARALLEL=false
TIMEOUT=15

# タイムアウトコマンドの検出
TIMEOUT_CMD=""
TIMEOUT_MODE=""
if command -v timeout >/dev/null 2>&1; then
    TIMEOUT_CMD="timeout"
elif command -v gtimeout >/dev/null 2>&1; then
    TIMEOUT_CMD="gtimeout"
elif command -v python3 >/dev/null 2>&1; then
    TIMEOUT_CMD="python3"
    TIMEOUT_MODE="python"
fi

if [ "${CM_TEST_FORCE_PY_TIMEOUT:-0}" -eq 1 ] && command -v python3 >/dev/null 2>&1; then
    TIMEOUT_CMD="python3"
    TIMEOUT_MODE="python"
fi

# タイムアウトコマンドがない場合の警告
if [ -z "$TIMEOUT_CMD" ]; then
    echo -e "${YELLOW}警告: timeout/gtimeoutコマンドが見つかりません${NC}"
    echo "無限ループが発生した場合、Ctrl+Cで中断できます"
    echo "macOSの場合: brew install coreutils でgtimeoutをインストールできます"
    echo ""
fi

# バックエンドに応じたプラットフォームディレクトリの解決
# common/ は全バックエンドで実行
# llvm/ は llvm, jit で実行
# wasm/ は llvm-wasm で実行
# js/ は js で実行
# baremetal/ は llvm-baremetal で実行
# uefi/ は llvm-uefi で実行
# jit/ は jit で実行
get_platform_dirs() {
    local backend="$1"
    case "$backend" in
        interpreter)
            echo "common"
            ;;
        jit)
            echo "common llvm jit"
            ;;
        llvm)
            echo "common llvm"
            ;;
        llvm-wasm)
            echo "common wasm"
            ;;
        llvm-uefi)
            echo "uefi"
            ;;
        llvm-baremetal)
            echo "baremetal"
            ;;
        js)
            echo "common js"
            ;;
        *)
            echo "common"
            ;;
    esac
}

# テストスイート定義
# 各スイートはカテゴリのグループを定義する（common/ 内のカテゴリ名）
expand_suite() {
    local suite="$1"
    case "$suite" in
        core)
            echo "basic types control_flow functions function_ptr loops literal auto const const_interpolation casting errors type_checking"
            ;;
        syntax)
            echo "array arrays array_higher_order dynamic_array slice string formatting enum match structs impl interface lambda chaining result must defer pointer ownership generics iterator"
            ;;
        stdlib)
            echo "collections io std allocator memory intrinsics preprocessor"
            ;;
        modules)
            echo "modules advanced_modules macro advanced"
            ;;
        platform)
            echo "gpu"
            ;;
        runtime)
            echo "file_io fs net thread sync"
            ;;
        all)
            echo ""
            ;;
        *)
            echo "Error: Unknown suite '$suite'" >&2
            echo "Valid suites: core, syntax, stdlib, modules, platform, runtime, all" >&2
            exit 1
            ;;
    esac
}

# ヘルプメッセージ
usage() {
    echo "Usage: $0 [OPTIONS]"
    echo "Options:"
    echo "  -b, --backend <backend>    Test backend: interpreter|jit|typescript|rust|cpp|llvm|llvm-wasm|llvm-uefi|llvm-baremetal|js (default: jit)"
    echo "  -c, --category <category>  Test categories (comma-separated, default: auto-detect from directories)"
    echo "  -s, --suite <suite>        Test suite: core|syntax|stdlib|modules|platform|runtime|all (default: all)"
    echo "  -v, --verbose              Show detailed output"
    echo "  -p, --parallel             Run tests in parallel (experimental)"
    echo "  -t, --timeout <seconds>    Test timeout in seconds (default: 5)"
    echo "  -h, --help                 Show this help message"
    echo ""
    echo "Suites:"
    echo "  core     - 言語基盤テスト（全ターゲット共通）"
    echo "  syntax   - 構文機能テスト（配列・構造体・ジェネリクス等）"
    echo "  stdlib   - 標準ライブラリテスト"
    echo "  modules  - モジュール・マクロテスト"
    echo "  platform - ターゲット固有テスト（UEFI・ベアメタル・ASM等）"
    echo "  runtime  - OS依存ランタイムテスト（ファイルI/O・ネット・スレッド等）"
    echo "  all      - 全テスト（デフォルト）"
    echo ""
    echo "Categories are auto-detected from directories in tests/programs/"
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
        -s|--suite)
            SUITE="$2"
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
if [[ ! "$BACKEND" =~ ^(interpreter|jit|typescript|rust|cpp|llvm|llvm-wasm|llvm-uefi|llvm-baremetal|js)$ ]]; then
    echo "Error: Invalid backend '$BACKEND'"
    echo "Valid backends: interpreter, jit, typescript, rust, cpp, llvm, llvm-wasm, llvm-uefi, llvm-baremetal, js"
    exit 1
fi

# スイート展開
if [ -n "$SUITE" ] && [ -z "$CATEGORIES" ]; then
    CATEGORIES=$(expand_suite "$SUITE")
fi

# プラットフォームディレクトリの解決
PLATFORM_DIRS=$(get_platform_dirs "$BACKEND")

# カテゴリー設定
if [ -z "$CATEGORIES" ]; then
    # バックエンドに応じたプラットフォームディレクトリからカテゴリを自動検出
    CATEGORIES=""
    for platform_dir in $PLATFORM_DIRS; do
        base_dir="$PROGRAMS_DIR/$platform_dir"
        if [ ! -d "$base_dir" ]; then
            continue
        fi
        for dir in "$base_dir"/*/; do
            if [ -d "$dir" ]; then
                dirname="$(basename "$dir")"
                # .cmファイルがあるディレクトリのみ追加
                if ls "$dir"/*.cm 1> /dev/null 2>&1; then
                    # プラットフォーム:カテゴリ の形式で保持
                    CATEGORIES="$CATEGORIES ${platform_dir}:${dirname}"
                fi
            fi
        done
    done
    # 先頭のスペースを削除
    CATEGORIES="${CATEGORIES# }"
else
    # ユーザー指定のカテゴリ（スイート展開含む）は common/ 内として扱う
    expanded=""
    for cat in $CATEGORIES; do
        if [[ "$cat" != *:* ]]; then
            expanded="$expanded common:$cat"
        else
            expanded="$expanded $cat"
        fi
    done
    CATEGORIES="${expanded# }"
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
    local error_expect_file="${test_file%.cm}.error"
    local backend_error_file="${test_file%.cm}.error.${BACKEND}"
    local output_file="$TEMP_DIR/${category}_${test_name}.out"
    local error_file="$TEMP_DIR/${category}_${test_name}.err"
    local is_error_test=false

    # テスト別タイムアウト: .timeoutファイルがあれば値を上書き
    local test_timeout="$TIMEOUT"
    local timeout_file="${test_file%.cm}.timeout"
    if [ -f "$timeout_file" ]; then
        test_timeout=$(cat "$timeout_file" | tr -d '[:space:]')
    fi

    # .skipファイルのチェック
    local skip_file="${test_file%.cm}.skip"
    local category_skip_file="$(dirname "$test_file")/.skip"
    local current_os=$(uname -s | tr '[:upper:]' '[:lower:]')

    # ファイル固有の.skipファイルがある場合
    if [ -f "$skip_file" ]; then
        # .skipファイルの内容を読んで、現在のバックエンドがスキップ対象か確認
        if [ -s "$skip_file" ]; then
            # バックエンド名の完全一致チェック
            if grep -qx "$BACKEND" "$skip_file" 2>/dev/null; then
                echo -e "${YELLOW}[SKIP]${NC} $category/$test_name - Skipped for $BACKEND"
                ((SKIPPED++))
                return
            fi
            # backend:os 形式のチェック (例: llvm:linux)
            if grep -qx "${BACKEND}:${current_os}" "$skip_file" 2>/dev/null; then
                echo -e "${YELLOW}[SKIP]${NC} $category/$test_name - Skipped for $BACKEND on $current_os"
                ((SKIPPED++))
                return
            fi
        else
            # ファイルが空の場合、すべてのバックエンドでスキップ
            echo -e "${YELLOW}[SKIP]${NC} $category/$test_name - Skip file exists"
            ((SKIPPED++))
            return
        fi
    fi

    # カテゴリ全体の.skipファイルがある場合
    if [ -f "$category_skip_file" ]; then
        if [ -s "$category_skip_file" ]; then
            if grep -qx "$BACKEND" "$category_skip_file" 2>/dev/null; then
                echo -e "${YELLOW}[SKIP]${NC} $category/$test_name - Category skipped for $BACKEND"
                ((SKIPPED++))
                return
            fi
        else
            echo -e "${YELLOW}[SKIP]${NC} $category/$test_name - Category skip file exists"
            ((SKIPPED++))
            return
        fi
    fi

    # バックエンド固有のerrorファイルがあれば優先して使用
    if [ -f "$backend_error_file" ]; then
        error_expect_file="$backend_error_file"
        is_error_test=true
    elif [ -f "$error_expect_file" ]; then
        is_error_test=true
    fi

    # バックエンド固有のexpectファイルがあれば優先して使用
    # バックエンド固有のexpectファイルがある場合、汎用errorファイルよりも優先する
    if [ -f "$backend_expect_file" ]; then
        expect_file="$backend_expect_file"
        is_error_test=false
    fi

    # expectファイルもerrorファイルもない場合はスキップ
    if [ ! -f "$expect_file" ] && [ ! -f "$error_expect_file" ]; then
        echo -e "${YELLOW}[SKIP]${NC} $category/$test_name - No expect/error file"
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
        if [ -n "$TIMEOUT_CMD" ]; then
            if [ "$TIMEOUT_MODE" = "python" ]; then
                "$TIMEOUT_CMD" - "$test_timeout" "$@" <<'PY'
import subprocess
import sys

timeout = int(sys.argv[1])
cmd = sys.argv[2:]
try:
    proc = subprocess.Popen(cmd)
    try:
        proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()
        sys.exit(124)
    sys.exit(proc.returncode if proc.returncode is not None else 1)
except FileNotFoundError:
    sys.exit(127)
PY
            else
                # --kill-after: タイムアウト後さらに2秒待ってもプロセスが終了しなければSIGKILL
                $TIMEOUT_CMD --kill-after=2 "$test_timeout" "$@"
            fi
        else
            # タイムアウトコマンドがない場合は直接実行
            "$@"
        fi
    }

    case "$BACKEND" in
        interpreter)
            # テストファイルのディレクトリに移動して実行（モジュールの相対パス解決のため）
            local test_dir="$(dirname "$test_file")"
            local test_basename="$(basename "$test_file")"

            # インタプリタで実行
            (cd "$test_dir" && run_with_timeout "$CM_EXECUTABLE" run -O$OPT_LEVEL "$test_basename" > "$output_file" 2>&1) || exit_code=$?
            ;;

        jit)
            # JITコンパイラで実行（LLVM ORC JIT）
            local test_dir="$(dirname "$test_file")"
            local test_basename="$(basename "$test_file")"

            # JITで実行
            (cd "$test_dir" && run_with_timeout "$CM_EXECUTABLE" run -O$OPT_LEVEL "$test_basename" > "$output_file" 2>&1) || exit_code=$?
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

            # テストファイルのディレクトリに移動してコンパイル（モジュールの相対パス解決のため）
            local test_dir="$(dirname "$test_file")"
            local test_basename="$(basename "$test_file")"

            # LLVM経由でネイティブ実行ファイル生成（エラー時は出力ファイルにエラーメッセージを書き込む）
            (cd "$test_dir" && run_with_timeout "$CM_EXECUTABLE" compile --emit-llvm -O$OPT_LEVEL "$test_basename" -o "$llvm_exec" > "$output_file" 2>&1) || exit_code=$?

            if [ $exit_code -eq 0 ] && [ -f "$llvm_exec" ]; then
                # テストディレクトリで実行（相対パス解決のため）
                (cd "$test_dir" && run_with_timeout "$llvm_exec" > "$output_file" 2>&1) || exit_code=$?
                
                # セグフォ時にgdbでデバッグ情報を取得（CI環境のみ）
                if [ $exit_code -eq 139 ] && [ -n "$CI" ] && command -v gdb >/dev/null 2>&1; then
                    echo "=== Segmentation fault detected, running gdb ===" >> "$output_file"
                    echo "run" | gdb --batch -ex "set pagination off" -ex "run" -ex "bt" -ex "quit" "$llvm_exec" >> "$output_file" 2>&1 || true
                fi
            fi
            ;;

        llvm-wasm)
            # LLVMバックエンドでWebAssemblyにコンパイルして実行
            local wasm_file="$TEMP_DIR/wasm_${test_name}.wasm"
            rm -f "$wasm_file"

            # LLVM経由でWebAssembly生成（エラー時は出力ファイルにエラーメッセージを書き込む）
            # テストファイルのディレクトリに移動してコンパイル（モジュールの相対パス解決のため）
            local test_dir="$(dirname "$test_file")"
            local test_basename="$(basename "$test_file")"
            (cd "$test_dir" && run_with_timeout "$CM_EXECUTABLE" compile --emit-llvm --target=wasm -O$OPT_LEVEL "$test_basename" -o "$wasm_file" > "$output_file" 2>&1) || exit_code=$?

            if [ $exit_code -eq 0 ] && [ -f "$wasm_file" ]; then
                # WASMランタイムで実行
                # 優先順位: wasmtime > node (with wasm wrapper) > wasmer
                if command -v wasmtime >/dev/null 2>&1; then
                    # wasmtimeを使用
                    run_with_timeout wasmtime "$wasm_file" > "$output_file" 2>&1 || exit_code=$?
                elif command -v node >/dev/null 2>&1; then
                    # nodeを使用（WASMラッパースクリプトを生成）
                    local wrapper_js="$TEMP_DIR/wasm_wrapper_${test_name}.js"
                    cat > "$wrapper_js" << 'EOJS'
const fs = require('fs');
const wasmBuffer = fs.readFileSync(process.argv[2]);

// テキストデコーダ
const decoder = new TextDecoder();
let outputBuffer = '';

WebAssembly.instantiate(wasmBuffer, {
    wasi_snapshot_preview1: {
        proc_exit: (code) => {
            // バッファに残っている出力を吐き出す
            if (outputBuffer) {
                process.stdout.write(outputBuffer);
            }
            process.exit(code);
        },
        fd_write: (fd, iovs_ptr, iovs_len, nwritten_ptr) => {
            const memory = result.instance.exports.memory;
            const dataView = new DataView(memory.buffer);

            // fd=1 は標準出力
            if (fd === 1) {
                let totalWritten = 0;

                // 各IOVを処理
                for (let i = 0; i < iovs_len; i++) {
                    const iov_offset = iovs_ptr + (i * 8);
                    const buf_ptr = dataView.getUint32(iov_offset, true);
                    const buf_len = dataView.getUint32(iov_offset + 4, true);

                    // バッファからデータを読み取る
                    const bytes = new Uint8Array(memory.buffer, buf_ptr, buf_len);
                    const str = decoder.decode(bytes);

                    // 出力バッファに追加
                    outputBuffer += str;
                    totalWritten += buf_len;
                }

                // 改行があったら出力
                const lines = outputBuffer.split('\n');
                if (lines.length > 1) {
                    for (let i = 0; i < lines.length - 1; i++) {
                        console.log(lines[i]);
                    }
                    outputBuffer = lines[lines.length - 1];
                }

                // 書き込んだバイト数を設定
                dataView.setUint32(nwritten_ptr, totalWritten, true);
                return 0; // 成功
            }
            return -1; // エラー
        },
        // その他のWASI関数のスタブ
        fd_close: () => 0,
        fd_seek: () => 0,
        fd_read: () => 0,
        environ_sizes_get: () => 0,
        environ_get: () => 0,
        args_sizes_get: () => 0,
        args_get: () => 0,
        random_get: () => 0,
        clock_time_get: () => 0,
        proc_raise: () => 0
    }
}).then(res => {
    result = res;
    // _startまたはmain関数を呼び出し
    if (result.instance.exports._start) {
        result.instance.exports._start();
    } else if (result.instance.exports.main) {
        const ret = result.instance.exports.main();
        // バッファに残っている出力を吐き出す
        if (outputBuffer) {
            process.stdout.write(outputBuffer);
        }
        process.exit(ret);
    }
}).catch(err => {
    console.error('WASM Error:', err);
    process.exit(1);
});
EOJS
                    run_with_timeout node "$wrapper_js" "$wasm_file" > "$output_file" 2>&1 || exit_code=$?
                    rm -f "$wrapper_js"
                elif command -v wasmer >/dev/null 2>&1; then
                    # wasmerを使用
                    run_with_timeout wasmer run "$wasm_file" > "$output_file" 2>&1 || exit_code=$?
                else
                    echo -e "${YELLOW}[SKIP]${NC} $category/$test_name - No WASM runtime found (install wasmtime, node, or wasmer)"
                    ((SKIPPED++))
                    return
                fi
            fi
            ;;

        js)
            # JavaScriptバックエンドでコンパイルして実行
            local js_file="$TEMP_DIR/js_${test_name}.js"
            rm -f "$js_file"

            # テストファイルのディレクトリに移動してコンパイル
            local test_dir="$(dirname "$test_file")"
            local test_basename="$(basename "$test_file")"

            # JavaScript生成（エラー時は出力ファイルにエラーメッセージを書き込む）
            (cd "$test_dir" && run_with_timeout "$CM_EXECUTABLE" compile --target=js -O$OPT_LEVEL "$test_basename" -o "$js_file" > "$output_file" 2>&1) || exit_code=$?

            if [ $exit_code -eq 0 ] && [ -f "$js_file" ]; then
                # Node.jsで実行
                if command -v node >/dev/null 2>&1; then
                    run_with_timeout node "$js_file" > "$output_file" 2>&1 || exit_code=$?
                else
                    echo -e "${YELLOW}[SKIP]${NC} $category/$test_name - Node.js not found"
                    ((SKIPPED++))
                    return
                fi
            fi
            ;;

        llvm-uefi)
            # UEFI ターゲットへのコンパイルのみ検証（実行不可）
            local uefi_obj="$TEMP_DIR/uefi_${test_name}.efi"
            rm -f "$uefi_obj"

            local test_dir="$(dirname "$test_file")"
            local test_basename="$(basename "$test_file")"

            # UEFI ターゲットでコンパイル（オブジェクト出力のみ）
            (cd "$test_dir" && run_with_timeout "$CM_EXECUTABLE" compile --emit-llvm --target=uefi -O$OPT_LEVEL "$test_basename" -o "$uefi_obj" > "$output_file" 2>&1) || exit_code=$?

            # コンパイル成功 = PASS（実行はしない）
            if [ $exit_code -eq 0 ]; then
                # expectファイルが "COMPILE_OK" なら出力比較をスキップ
                if grep -q "COMPILE_OK" "$expect_file" 2>/dev/null; then
                    echo "COMPILE_OK" > "$output_file"
                fi
            fi
            rm -f "$uefi_obj"
            ;;

        llvm-baremetal)
            # ベアメタルターゲットへのコンパイルのみ検証（実行不可）
            local baremetal_obj="$TEMP_DIR/baremetal_${test_name}.o"
            rm -f "$baremetal_obj"

            local test_dir="$(dirname "$test_file")"
            local test_basename="$(basename "$test_file")"

            # ベアメタルターゲットでコンパイル（オブジェクト出力のみ）
            (cd "$test_dir" && run_with_timeout "$CM_EXECUTABLE" compile --emit-llvm --target=baremetal-x86 -O$OPT_LEVEL "$test_basename" -o "$baremetal_obj" > "$output_file" 2>&1) || exit_code=$?

            # コンパイル成功 = PASS（実行はしない）
            if [ $exit_code -eq 0 ]; then
                if grep -q "COMPILE_OK" "$expect_file" 2>/dev/null; then
                    echo "COMPILE_OK" > "$output_file"
                fi
            fi
            rm -f "$baremetal_obj"
            ;;
    esac

    # タイムアウト処理
    if [ $exit_code -eq 124 ] || [ $exit_code -eq 143 ]; then
        echo -e "${RED}[FAIL]${NC} $category/$test_name - Timeout (>${test_timeout}s)"
        ((FAILED++))
        return
    fi

    # .errorファイルがある場合（エラーテスト）
    if [ "$is_error_test" = true ]; then
        # エラーが期待される（非ゼロexit codeならPASS）
        # macOS CIではPython timeout wrapperによりexit codeが変わる場合がある
        if [ $exit_code -ne 0 ]; then
            echo -e "${GREEN}[PASS]${NC} $category/$test_name"
            ((PASSED++))
        else
            echo -e "${RED}[FAIL]${NC} $category/$test_name - Expected error but succeeded"
            ((FAILED++))
        fi
    # エラーファイルに期待される出力がある場合（コンパイルエラーテスト等）
    elif grep -q "error\|Error\|エラー" "$expect_file" 2>/dev/null; then
        # エラーが期待される場合
        if [ $exit_code -ne 0 ]; then
            # エラー出力を比較
            if diff -q "$expect_file" "$output_file" > /dev/null 2>&1; then
                echo -e "${GREEN}[PASS]${NC} $category/$test_name"
                ((PASSED++))
            else
                echo -e "${RED}[FAIL]${NC} $category/$test_name - Error output mismatch"
                echo "  Expected:"
                head -n 5 "$expect_file" 2>/dev/null | sed 's/^/    /'
                echo "  Got:"
                head -n 5 "$output_file" 2>/dev/null | sed 's/^/    /'
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
            echo "  Output (first 10 lines):"
            head -n 10 "$output_file" 2>/dev/null | sed 's/^/    /'
            ((FAILED++))
        else
            # 出力を比較
            if diff -q "$expect_file" "$output_file" > /dev/null 2>&1; then
                echo -e "${GREEN}[PASS]${NC} $category/$test_name"
                ((PASSED++))
            else
                echo -e "${RED}[FAIL]${NC} $category/$test_name - Output mismatch"
                echo "  Expected:"
                head -n 5 "$expect_file" 2>/dev/null | sed 's/^/    /'
                echo "  Got:"
                head -n 5 "$output_file" 2>/dev/null | sed 's/^/    /'
                echo "  Diff:"
                diff -u "$expect_file" "$output_file" 2>/dev/null | head -n 15 | sed 's/^/    /'
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
    log "Parallel: $PARALLEL"
    if [ -n "$TIMEOUT_CMD" ]; then
        local timeout_mode="native"
        if [ "$TIMEOUT_MODE" = "python" ]; then
            timeout_mode="python"
        fi
        log "Timeout: $TIMEOUT_CMD ($timeout_mode), Seconds: $TIMEOUT"
    else
        log "Timeout: none, Seconds: $TIMEOUT"
    fi
    log "=========================================="
    log ""

    if [ "$PARALLEL" = true ]; then
        # 並列実行モード
        run_tests_parallel
    else
        # 順次実行モード
        run_tests_sequential
    fi

    # 結果サマリー
    log "=========================================="
    log "Test Results Summary"
    log "=========================================="
    log "Total:   $TOTAL"
    log "Passed:  $PASSED"
    log "Failed:  $FAILED"
    log "Skipped: $SKIPPED"
    log ""

    # CI環境でセグフォのデバッグ情報を表示
    if [ -n "$CI" ] && [ $FAILED -gt 0 ]; then
        shopt -s nullglob 2>/dev/null || true
        for debug_file in "$TEMP_DIR"/*.debug; do
            if [ -f "$debug_file" ]; then
                log "=========================================="
                log "Debug info for $(basename "${debug_file%.debug}")"
                log "=========================================="
                cat "$debug_file"
                log ""
            fi
        done
        shopt -u nullglob 2>/dev/null || true
    fi

    if [ $FAILED -gt 0 ]; then
        log "Status: FAILED"
        exit 1
    else
        log "Status: SUCCESS"
        exit 0
    fi
}

# 順次実行モード
run_tests_sequential() {
    for entry in $CATEGORIES; do
        # platform:category フォーマットをパース
        local platform_dir="${entry%%:*}"
        local category="${entry##*:}"
        local category_dir="$PROGRAMS_DIR/$platform_dir/$category"

        if [ ! -d "$category_dir" ]; then
            log "Warning: Category directory '$platform_dir/$category' not found, skipping"
            continue
        fi

        log "Testing category: $platform_dir/$category"
        log "----------------------------------------"

        for test_file in "$category_dir"/*.cm; do
            if [ -f "$test_file" ]; then
                ((TOTAL++))
                run_single_test "$test_file"
            fi
        done

        log ""
    done
}

# 並列実行モード
run_tests_parallel() {
    local test_files=()
    local results_dir="$TEMP_DIR/parallel_results"
    mkdir -p "$results_dir"
    
    # 全テストファイルを収集
    for entry in $CATEGORIES; do
        local platform_dir="${entry%%:*}"
        local category="${entry##*:}"
        local category_dir="$PROGRAMS_DIR/$platform_dir/$category"
        if [ -d "$category_dir" ]; then
            for test_file in "$category_dir"/*.cm; do
                if [ -f "$test_file" ]; then
                    test_files+=("$test_file")
                fi
            done
        fi
    done
    
    TOTAL=${#test_files[@]}
    log "Running $TOTAL tests in parallel..."
    log ""
    
    # 並列ジョブ数（CPU数に基づく）
    local max_jobs=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
    
    # 各テストを並列実行
    local pids=()
    local job_count=0
    
    for test_file in "${test_files[@]}"; do
        local test_name="$(basename "${test_file%.cm}")"
        local category="$(basename "$(dirname "$test_file")")"
        local result_file="$results_dir/${category}_${test_name}.result"
        
        # バックグラウンドでテスト実行
        (
            run_parallel_test "$test_file" "$result_file"
        ) &
        
        pids+=($!)
        CHILD_PIDS+=($!)
        ((job_count++))
        
        # 最大ジョブ数に達したら待機
        if [ $job_count -ge $max_jobs ]; then
            wait "${pids[0]}"
            pids=("${pids[@]:1}")
            ((job_count--))
        fi
    done
    
    # 残りのジョブを待機
    wait
    
    # 結果を集計
    for test_file in "${test_files[@]}"; do
        local test_name="$(basename "${test_file%.cm}")"
        local category="$(basename "$(dirname "$test_file")")"
        local result_file="$results_dir/${category}_${test_name}.result"
        
        if [ -f "$result_file" ]; then
            local result=$(cat "$result_file")
            case "$result" in
                PASS*)
                    echo -e "${GREEN}[PASS]${NC} $category/$test_name"
                    ((PASSED++))
                    ;;
                FAIL*)
                    local reason="${result#FAIL:}"
                    echo -e "${RED}[FAIL]${NC} $category/$test_name - $reason"
                    # エラーファイルがあれば先頭5行を表示
                    local error_file="${result_file}.error"
                    if [ -f "$error_file" ]; then
                        echo "  --- Error output (first 5 lines) ---"
                        head -5 "$error_file" | sed 's/^/  /'
                        echo "  ---"
                    fi
                    ((FAILED++))
                    ;;
                SKIP*)
                    local reason="${result#SKIP:}"
                    echo -e "${YELLOW}[SKIP]${NC} $category/$test_name - $reason"
                    ((SKIPPED++))
                    ;;
            esac
        else
            echo -e "${RED}[FAIL]${NC} $category/$test_name - No result file"
            ((FAILED++))
        fi
    done
}

# 並列テスト実行（子プロセス用）
run_parallel_test() {
    local test_file="$1"
    local result_file="$2"
    local test_name="$(basename "${test_file%.cm}")"
    local category="$(basename "$(dirname "$test_file")")"
    local expect_file="${test_file%.cm}.expect"
    local backend_expect_file="${test_file%.cm}.expect.${BACKEND}"
    local error_expect_file="${test_file%.cm}.error"
    local backend_error_file="${test_file%.cm}.error.${BACKEND}"
    local output_file="$TEMP_DIR/${category}_${test_name}_${BASHPID}.out"
    local is_error_test=false

    # テスト別タイムアウト: .timeoutファイルがあれば値を上書き
    local test_timeout="$TIMEOUT"
    local timeout_file="${test_file%.cm}.timeout"
    if [ -f "$timeout_file" ]; then
        test_timeout=$(cat "$timeout_file" | tr -d '[:space:]')
    fi

    # .skipファイルのチェック
    local skip_file="${test_file%.cm}.skip"
    local category_skip_file="$(dirname "$test_file")/.skip"

    # ファイル固有の.skipファイルがある場合
    if [ -f "$skip_file" ]; then
        if [ -s "$skip_file" ]; then
            local current_os=$(uname -s | tr '[:upper:]' '[:lower:]')
            # バックエンド名の完全一致チェック
            if grep -qx "$BACKEND" "$skip_file" 2>/dev/null; then
                echo "SKIP:Skipped for $BACKEND" > "$result_file"
                return
            fi
            # backend:os 形式のチェック (例: llvm:linux)
            if grep -qx "${BACKEND}:${current_os}" "$skip_file" 2>/dev/null; then
                echo "SKIP:Skipped for $BACKEND on $current_os" > "$result_file"
                return
            fi
        else
            # ファイルが空の場合、すべてのバックエンドでスキップ
            echo "SKIP:Skip file exists" > "$result_file"
            return
        fi
    fi

    # カテゴリ全体の.skipファイルがある場合
    if [ -f "$category_skip_file" ]; then
        if [ -s "$category_skip_file" ]; then
            if grep -qx "$BACKEND" "$category_skip_file" 2>/dev/null; then
                echo "SKIP:Category skipped for $BACKEND" > "$result_file"
                return
            fi
        else
            echo "SKIP:Category skip file exists" > "$result_file"
            return
        fi
    fi

    # バックエンド固有のerrorファイルがあれば優先して使用
    if [ -f "$backend_error_file" ]; then
        error_expect_file="$backend_error_file"
        is_error_test=true
    elif [ -f "$error_expect_file" ]; then
        is_error_test=true
    fi

    # バックエンド固有のexpectファイルがあれば優先
    if [ -f "$backend_expect_file" ]; then
        expect_file="$backend_expect_file"
    fi

    # expectファイルもerrorファイルもない場合はスキップ
    if [ ! -f "$expect_file" ] && [ ! -f "$error_expect_file" ]; then
        echo "SKIP:No expect/error file" > "$result_file"
        return
    fi

    local exit_code=0

    # タイムアウト付き実行
    run_with_timeout_silent() {
        if [ -n "$TIMEOUT_CMD" ]; then
            if [ "$TIMEOUT_MODE" = "python" ]; then
                "$TIMEOUT_CMD" - "$test_timeout" "$@" <<'PY'
import subprocess
import sys

timeout = int(sys.argv[1])
cmd = sys.argv[2:]
try:
    proc = subprocess.Popen(cmd)
    try:
        proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()
        sys.exit(124)
    sys.exit(proc.returncode if proc.returncode is not None else 1)
except FileNotFoundError:
    sys.exit(127)
PY
            else
                $TIMEOUT_CMD --kill-after=2 "$test_timeout" "$@"
            fi
        else
            "$@"
        fi
    }

    case "$BACKEND" in
        interpreter|jit)
            # テストファイルのディレクトリに移動して実行（モジュールの相対パス解決のため）
            local test_dir="$(dirname "$test_file")"
            local test_basename="$(basename "$test_file")"
            (cd "$test_dir" && run_with_timeout_silent "$CM_EXECUTABLE" run -O$OPT_LEVEL "$test_basename" > "$output_file" 2>&1) || exit_code=$?
            ;;
        llvm)
            # テストファイルのディレクトリに移動してコンパイル（モジュールの相対パス解決のため）
            local test_dir="$(dirname "$test_file")"
            local test_basename="$(basename "$test_file")"
            local llvm_exec="$TEMP_DIR/llvm_${category}_${test_name}_${BASHPID}"
            (cd "$test_dir" && run_with_timeout_silent "$CM_EXECUTABLE" compile --emit-llvm -O$OPT_LEVEL "$test_basename" -o "$llvm_exec" > "$output_file" 2>&1) || exit_code=$?
            if [ $exit_code -eq 0 ] && [ -f "$llvm_exec" ]; then
                # テストディレクトリで実行（相対パス解決のため）
                (cd "$test_dir" && "$llvm_exec" > "$output_file" 2>&1) || exit_code=$?
                
                # セグフォ時にgdbでデバッグ情報を取得（CI環境のみ）
                if [ $exit_code -eq 139 ] && [ -n "$CI" ] && command -v gdb >/dev/null 2>&1; then
                    echo "=== Segmentation fault detected, running gdb ===" >> "$output_file"
                    echo "run" | gdb --batch -ex "set pagination off" -ex "run" -ex "bt" -ex "quit" "$llvm_exec" >> "$output_file" 2>&1 || true
                fi
                
                rm -f "$llvm_exec"
            fi
            ;;
        llvm-wasm)
            local wasm_file="$TEMP_DIR/wasm_${category}_${test_name}_${BASHPID}.wasm"
            local test_dir="$(dirname "$test_file")"
            local test_basename="$(basename "$test_file")"
            (cd "$test_dir" && run_with_timeout_silent "$CM_EXECUTABLE" compile --emit-llvm --target=wasm -O$OPT_LEVEL "$test_basename" -o "$wasm_file" > "$output_file" 2>&1) || exit_code=$?
            if [ $exit_code -eq 0 ] && [ -f "$wasm_file" ]; then
                if command -v wasmtime >/dev/null 2>&1; then
                    run_with_timeout_silent wasmtime "$wasm_file" > "$output_file" 2>&1 || exit_code=$?
                else
                    echo "SKIP:No WASM runtime" > "$result_file"
                    rm -f "$wasm_file"
                    return
                fi
                rm -f "$wasm_file"
            fi
            ;;
        js)
            local js_file="$TEMP_DIR/js_${test_name}_$$.js"
            local test_dir="$(dirname "$test_file")"
            local test_basename="$(basename "$test_file")"
            (cd "$test_dir" && run_with_timeout_silent "$CM_EXECUTABLE" compile --target=js -O$OPT_LEVEL "$test_basename" -o "$js_file" > "$output_file" 2>&1) || exit_code=$?
            if [ $exit_code -eq 0 ] && [ -f "$js_file" ]; then
                if command -v node >/dev/null 2>&1; then
                    run_with_timeout_silent node "$js_file" > "$output_file" 2>&1 || exit_code=$?
                else
                    echo "SKIP:Node.js not found" > "$result_file"
                    rm -f "$js_file"
                    return
                fi
                rm -f "$js_file"
            fi
            ;;
        llvm-uefi)
            # UEFI ターゲットへのコンパイルのみ検証
            local uefi_obj="$TEMP_DIR/uefi_${test_name}_$$.efi"
            local test_dir="$(dirname "$test_file")"
            local test_basename="$(basename "$test_file")"
            (cd "$test_dir" && run_with_timeout_silent "$CM_EXECUTABLE" compile --emit-llvm --target=uefi -O$OPT_LEVEL "$test_basename" -o "$uefi_obj" > "$output_file" 2>&1) || exit_code=$?
            if [ $exit_code -eq 0 ]; then
                if grep -q "COMPILE_OK" "$expect_file" 2>/dev/null; then
                    echo "COMPILE_OK" > "$output_file"
                fi
            fi
            rm -f "$uefi_obj"
            ;;
        llvm-baremetal)
            # ベアメタルターゲットへのコンパイルのみ検証
            local baremetal_obj="$TEMP_DIR/baremetal_${test_name}_$$.o"
            local test_dir="$(dirname "$test_file")"
            local test_basename="$(basename "$test_file")"
            (cd "$test_dir" && run_with_timeout_silent "$CM_EXECUTABLE" compile --emit-llvm --target=baremetal-x86 -O$OPT_LEVEL "$test_basename" -o "$baremetal_obj" > "$output_file" 2>&1) || exit_code=$?
            if [ $exit_code -eq 0 ]; then
                if grep -q "COMPILE_OK" "$expect_file" 2>/dev/null; then
                    echo "COMPILE_OK" > "$output_file"
                fi
            fi
            rm -f "$baremetal_obj"
            ;;
        *)
            echo "SKIP:Backend not supported for parallel" > "$result_file"
            return
            ;;
    esac

    # タイムアウト
    if [ $exit_code -eq 124 ] || [ $exit_code -eq 143 ]; then
        echo "FAIL:Timeout (>${test_timeout}s)" > "$result_file"
        rm -f "$output_file"
        return
    fi

    # 結果比較
    # .errorファイルがある場合（エラーテスト）
    if [ "$is_error_test" = true ]; then
        # エラーが期待される（非ゼロexit codeならPASS）
        # macOS CIではPython timeout wrapperによりexit codeが変わる場合がある
        if [ $exit_code -ne 0 ]; then
            echo "PASS" > "$result_file"
        else
            echo "FAIL:Expected error but succeeded" > "$result_file"
        fi
    # エラーファイルに期待される出力がある場合（コンパイルエラーテスト等）
    elif grep -q "error\|Error\|エラー" "$expect_file" 2>/dev/null; then
        if [ $exit_code -ne 0 ]; then
            if diff -q "$expect_file" "$output_file" > /dev/null 2>&1; then
                echo "PASS" > "$result_file"
            else
                echo "FAIL:Error output mismatch" > "$result_file"
            fi
        else
            echo "FAIL:Expected error but succeeded" > "$result_file"
        fi
    else
        if [ $exit_code -ne 0 ]; then
            # ランタイムエラー: エラー出力を保存
            echo "FAIL:Runtime error (exit code: $exit_code)" > "$result_file"
            # エラー出力を別ファイルに保存（デバッグ用）
            if [ -f "$output_file" ]; then
                cat "$output_file" > "${result_file}.error" 2>/dev/null || true
            fi
        else
            if diff -q "$expect_file" "$output_file" > /dev/null 2>&1; then
                echo "PASS" > "$result_file"
            else
                echo "FAIL:Output mismatch" > "$result_file"
            fi
        fi
    fi

    rm -f "$output_file"
}

# メイン実行
main
