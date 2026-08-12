#!/bin/bash

# Unified Test Runner for Cm Language
# Supports: interpreter, llvm backends with the same test items

# set -e を削除（エラーが発生してもスクリプトを継続）

# 設定
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# 分割されたランナーモジュールを依存順に読み込む（実装本体は tests/runner/ 配下）
source "$SCRIPT_DIR/runner/env.sh"
source "$SCRIPT_DIR/runner/platform.sh"
source "$SCRIPT_DIR/runner/suite.sh"
source "$SCRIPT_DIR/runner/wasm.sh"
source "$SCRIPT_DIR/runner/execute.sh"
source "$SCRIPT_DIR/runner/drivers.sh"

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
        -n|--no-cache)
            NO_CACHE=true
            shift
            ;;
        --clean-cache)
            CLEAN_CACHE=true
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

# バックエンド検証
if [[ ! "$BACKEND" =~ ^(interpreter|jit|typescript|rust|cpp|llvm|llvm-wasm|llvm-uefi|llvm-baremetal|js|sv)$ ]]; then
    echo "Error: Invalid backend '$BACKEND'"
    echo "Valid backends: interpreter, jit, typescript, rust, cpp, llvm, llvm-wasm, llvm-uefi, llvm-baremetal, js, sv"
    exit 1
fi

# 実行ランタイムの事前チェック
# （欠落時に全テストが「No WASM runtime」等で静かにスキップされ、
# スイートが緑のまま素通りする事故を防ぐ）
if [ "$BACKEND" = "llvm-wasm" ] && ! command -v wasmtime >/dev/null 2>&1 && \
   ! command -v node >/dev/null 2>&1 && ! command -v wasmer >/dev/null 2>&1; then
    echo "Error: WASMランタイムが見つかりません（wasmtime / node / wasmer のいずれかが必要）"
    exit 1
fi
if [ "$BACKEND" = "llvm-wasm" ] && ! command -v wasmtime >/dev/null 2>&1; then
    echo "警告: wasmtimeが見つからないため node のWASIラッパーで実行します"
    setup_wasm_node_wrapper
fi
if [ "$BACKEND" = "js" ] && ! command -v node >/dev/null 2>&1; then
    echo "Error: Node.js が見つかりません（jsバックエンドのテストに必要）"
    exit 1
fi

# キャッシュオプションの構築
# インクリメンタルビルド廃止に伴い、cmへ渡すキャッシュオプションは無い（-n/--no-cacheは互換のため受理のみ）
CACHE_OPTS=""

# テスト前キャッシュクリア
if [ "$CLEAN_CACHE" = true ]; then
    rm -rf "$PROJECT_ROOT/.cm-cache"
    echo -e "${YELLOW}[INFO]${NC} キャッシュをクリアしました"
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
        # SVバックエンドはtests/sv/に配置（programs/外）
        if [ "$platform_dir" = "sv" ]; then
            base_dir="$PROJECT_ROOT/tests/sv"
        else
            base_dir="$PROGRAMS_DIR/$platform_dir"
        fi
        if [ ! -d "$base_dir" ]; then
            continue
        fi
        for dir in "$base_dir"/*/; do
            if [ -d "$dir" ]; then
                dirname="$(basename "$dir")"
                # .cmファイルが直下またはサブフォルダ（階層問わず）に1つでもあれば追加
                if [ -n "$(find "$dir" -type f -name '*.cm' -print -quit 2>/dev/null)" ]; then
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

# ログファイル
LOG_FILE="$TEMP_DIR/test_${BACKEND}_$(date +%Y%m%d_%H%M%S).log"

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

# メイン実行
main
