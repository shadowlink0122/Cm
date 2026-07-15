#!/bin/bash
# ============================================================
# cm test コマンドのE2Eテスト
# ============================================================
# JITフロー（native）とSVフロー（//! platform: sv → iverilog+vvp）の
# ディスパッチ・成功・失敗・step()ガードを検証する。
# ============================================================
set -u
cd "$(dirname "$0")/.."

CM="${CM_EXECUTABLE:-./cm}"
DIR=tests/cmtest
PASS=0
FAIL=0

# check <名前> <期待exit(0|nonzero)> <出力に含むべき文字列> <コマンド...>
check() {
    local name="$1" expect="$2" needle="$3"
    shift 3
    local out
    out=$("$@" 2>&1)
    local rc=$?
    local rc_ok=false
    if [ "$expect" = "0" ] && [ $rc -eq 0 ]; then rc_ok=true; fi
    if [ "$expect" = "nonzero" ] && [ $rc -ne 0 ]; then rc_ok=true; fi
    if $rc_ok && { [ -z "$needle" ] || echo "$out" | grep -q "$needle"; }; then
        echo "[PASS] $name"
        PASS=$((PASS + 1))
    else
        echo "[FAIL] $name (exit=$rc)"
        echo "$out" | sed 's/^/    /' | head -10
        FAIL=$((FAIL + 1))
    fi
}

# JITフロー: 全テスト成功（[PASS]行と件数サマリ）
check "native pass" 0 "\[PASS\] doubles_negative" "$CM" test "$DIR/native_pass.cm"

# JITフロー: assert失敗で非0終了
check "native fail" nonzero "assertion failed" "$CM" test "$DIR/native_fail.cm"

# JITフロー: step() はSVプラットフォーム専用エラー
check "native step guard" nonzero "platform: sv" "$CM" test "$DIR/native_step_error.cm"

# 通常ビルドでは #[test] が除去される（テスト関数だけのファイルはmain無しエラーになる
# ため、#[test]除去の検証は tests/common/preprocessor/test_attr_excluded.cm が担う）

# SVフロー: iverilog があればシミュレーションまで実行
if command -v iverilog >/dev/null 2>&1 && command -v vvp >/dev/null 2>&1; then
    check "sv platform dispatch" 0 "PASS: value latched" "$CM" test "$DIR/sv_platform.cm"
else
    echo "[SKIP] sv platform dispatch (iverilog/vvp not found)"
fi

echo ""
echo "cm test E2E: PASS=$PASS FAIL=$FAIL"
[ "$FAIL" -eq 0 ]
