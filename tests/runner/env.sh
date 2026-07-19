#!/bin/bash
# unified_test_runner.sh から source される環境設定モジュール。
# 実行ファイル解決・カラー定数・既定値・タイムアウトコマンド検出・cleanup/シグナルトラップ・結果カウンタ・log 関数を提供する。

# Windows対応: 実行ファイルの拡張子
# 環境変数CM_EXECUTABLEが設定されている場合はそれを使用
if [ -n "${CM_EXECUTABLE:-}" ]; then
    # 環境変数から設定済み
    :
elif [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "cygwin" ]] || [[ "$OSTYPE" == "win32" ]]; then
    CM_EXECUTABLE="$PROJECT_ROOT/cm.exe"
    IS_WINDOWS=true
else
    CM_EXECUTABLE="$PROJECT_ROOT/cm"
    IS_WINDOWS=false
fi

PROGRAMS_DIR="$PROJECT_ROOT/tests"
TEMP_DIR="$PROJECT_ROOT/.tmp/test_runner"

# cmバイナリの存在確認
if [ ! -x "$CM_EXECUTABLE" ]; then
    echo -e "${RED}エラー: cmバイナリが見つかりません: $CM_EXECUTABLE${NC}"
    echo "make build を実行してコンパイラをビルドしてください"
    exit 1
fi

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
NO_CACHE=false
CLEAN_CACHE=false

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

# テスト結果カウンタ
TOTAL=0
PASSED=0
FAILED=0
SKIPPED=0

# ログ出力関数
log() {
    echo "$1" | tee -a "$LOG_FILE"
}
