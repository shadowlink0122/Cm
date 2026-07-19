#!/bin/bash
# ============================================================
# サニタイザ（--sanitize）のE2Eテスト
# ============================================================
# native/wasm/jitの各実行系で bounds の検出（trap終了）と正常系の無影響を検証し、addressは計装（__asanシンボル）とCLIエラーを検証する。
# ASanの実行時検査はランタイムが動作する環境でのみ実施し、動かない環境（例: macOS 26.x + LLVM17ランタイム）ではプローブで判定して明示的にSKIPする。
# wasmはwasmtimeが無い環境では明示的にSKIPする（サイレントスキップ禁止）。
# ============================================================
set -u
cd "$(dirname "$0")/../.."

CM="${CM_EXECUTABLE:-./cm}"
# cmconfigテストがサブディレクトリへcdして実行するため絶対パスへ正規化する
case "$CM" in
    /*) ;;
    *) CM="$PWD/${CM#./}" ;;
esac
CASES=tests/sanitize/cases
WORK=.tmp/test_sanitize
PASS=0
FAIL=0
SKIP=0

mkdir -p "$WORK"

# タイムアウトコマンド（ASanランタイムがハングする環境向け）
TIMEOUT_CMD=""
if command -v timeout >/dev/null 2>&1; then
    TIMEOUT_CMD="timeout 20"
elif command -v gtimeout >/dev/null 2>&1; then
    TIMEOUT_CMD="gtimeout 20"
fi

pass() { echo "[PASS] $1"; PASS=$((PASS + 1)); }
fail() { echo "[FAIL] $1"; shift; [ $# -gt 0 ] && echo "$@" | sed 's/^/    /'; FAIL=$((FAIL + 1)); }
skip() { echo "[SKIP] $1"; SKIP=$((SKIP + 1)); }

# run_expect <名前> <期待終了コード条件: zero|trap> <コマンド...>
# trap = 非0終了（llvm.trap によるシグナル死 128+N やwasmtimeのtrap終了を含む）
run_expect() {
    local name="$1" cond="$2"
    shift 2
    local out code
    out=$($TIMEOUT_CMD "$@" 2>&1)
    code=$?
    case "$cond" in
        zero)
            if [ "$code" -eq 0 ]; then pass "$name"; else fail "$name" "exit=$code out=$out"; fi
            ;;
        trap)
            if [ "$code" -ne 0 ]; then pass "$name"; else fail "$name" "expected non-zero exit, got 0 out=$out"; fi
            ;;
    esac
}

# expect_msg <名前> <含むべき文字列> <コマンド...>（終了コード非0も要求）
expect_msg() {
    local name="$1" needle="$2"
    shift 2
    local out code
    out=$("$@" 2>&1)
    code=$?
    if [ "$code" -ne 0 ] && echo "$out" | grep -qF "$needle"; then
        pass "$name"
    else
        fail "$name" "exit=$code out=$out"
    fi
}

echo "=== Sanitizer E2E tests ==="

# ---------- CLI検証 ----------
expect_msg "cli: unknown sanitizer value" "unknown sanitizer 'foo'" \
    "$CM" compile --sanitize=foo "$CASES/ok.cm" -o "$WORK/never"
expect_msg "cli: address is rejected on wasm" "not supported on target 'wasm'" \
    "$CM" compile --target=wasm --sanitize=address "$CASES/ok.cm" -o "$WORK/never"
expect_msg "cli: address is rejected on jit run" "not supported on target 'jit'" \
    "$CM" run --sanitize=address "$CASES/ok.cm"
expect_msg "cli: sanitize is rejected on js" "not supported on target" \
    "$CM" compile --target=js --sanitize=bounds "$CASES/ok.cm" -o "$WORK/never.js"
expect_msg "cli: ja message" "エラー: 不明なサニタイザ" \
    "$CM" compile --lang=ja --sanitize=foo "$CASES/ok.cm" -o "$WORK/never"
expect_msg "cli: thread is rejected on jit run" "not supported on target 'jit'" \
    "$CM" run --sanitize=thread "$CASES/ok.cm"
expect_msg "cli: thread is rejected on wasm" "not supported on target 'wasm'" \
    "$CM" compile --target=wasm --sanitize=thread "$CASES/ok.cm" -o "$WORK/never"
if [ "$(uname -s)" = "Darwin" ]; then
    expect_msg "cli: memory is rejected on macOS" "only supported on Linux" \
        "$CM" compile --sanitize=memory "$CASES/ok.cm" -o "$WORK/never"
fi

# ---------- bounds: native ----------
if $CM compile --sanitize=bounds -O0 "$CASES/ok.cm" -o "$WORK/ok_bounds" >/dev/null 2>&1; then
    run_expect "bounds/native: in-bounds program runs normally" zero "$WORK/ok_bounds"
else
    fail "bounds/native: compile ok.cm"
fi
if $CM compile --sanitize=bounds -O0 "$CASES/oob_write.cm" -o "$WORK/oob_bounds" >/dev/null 2>&1; then
    run_expect "bounds/native: out-of-bounds write traps" trap "$WORK/oob_bounds"
else
    fail "bounds/native: compile oob_write.cm"
fi

# ---------- bounds: jit ----------
run_expect "bounds/jit: in-bounds program runs normally" zero \
    "$CM" run --sanitize=bounds -O0 "$CASES/ok.cm"
run_expect "bounds/jit: out-of-bounds write traps" trap \
    "$CM" run --sanitize=bounds -O0 "$CASES/oob_write.cm"

# ---------- bounds: wasm ----------
if command -v wasmtime >/dev/null 2>&1; then
    if $CM compile --target=wasm --sanitize=bounds -O0 "$CASES/ok.cm" -o "$WORK/ok_bounds.wasm" >/dev/null 2>&1 &&
        $CM compile --target=wasm --sanitize=bounds -O0 "$CASES/oob_write.cm" -o "$WORK/oob_bounds.wasm" >/dev/null 2>&1; then
        run_expect "bounds/wasm: in-bounds program runs normally" zero wasmtime "$WORK/ok_bounds.wasm"
        run_expect "bounds/wasm: out-of-bounds write traps" trap wasmtime "$WORK/oob_bounds.wasm"
    else
        fail "bounds/wasm: compile"
    fi
else
    skip "bounds/wasm: wasmtime not installed"
fi

# ---------- address: 計装検証（シンボルレベル。全環境で実施） ----------
if $CM compile --sanitize=address -O0 "$CASES/oob_write.cm" -o "$WORK/oob_asan" >/dev/null 2>&1; then
    if nm "$WORK/oob_asan" 2>/dev/null | grep -q '__asan_init'; then
        pass "address/native: binary is ASan-instrumented (__asan_init)"
    else
        fail "address/native: __asan_init not found in binary"
    fi
else
    fail "address/native: compile oob_write.cm"
fi
if nm "$WORK/oob_bounds" 2>/dev/null | grep -q '__asan'; then
    fail "bounds/native: bounds binary must not link ASan"
else
    pass "bounds/native: no ASan symbols in bounds binary"
fi

# ---------- address: 実行時検証（ランタイムが動作する環境のみ） ----------
# プローブ: 正常プログラムのASanビルドが正常終了するかでランタイムの健全性を判定する
if $CM compile --sanitize=address -O0 "$CASES/ok.cm" -o "$WORK/ok_asan" >/dev/null 2>&1; then
    probe_out=$($TIMEOUT_CMD "$WORK/ok_asan" 2>&1)
    probe_code=$?
    if [ "$probe_code" -eq 0 ]; then
        run_expect "address/native: in-bounds program runs normally" zero "$WORK/ok_asan"
        oob_out=$($TIMEOUT_CMD "$WORK/oob_asan" 2>&1)
        oob_code=$?
        if [ "$oob_code" -ne 0 ] && echo "$oob_out" | grep -q 'AddressSanitizer'; then
            pass "address/native: out-of-bounds write is reported"
        else
            fail "address/native: out-of-bounds write is reported" "exit=$oob_code out=$(echo "$oob_out" | head -3)"
        fi
    else
        skip "address/native runtime checks: ASan runtime unhealthy on this OS (probe exit=$probe_code)"
    fi
else
    fail "address/native: compile ok.cm"
fi

# ---------- undefined: MIRレベル検査（native/jit/wasm） ----------
if $CM compile --sanitize=undefined -O0 "$CASES/ok.cm" -o "$WORK/ok_undef" >/dev/null 2>&1; then
    run_expect "undefined/native: in-bounds program runs normally" zero "$WORK/ok_undef"
else
    fail "undefined/native: compile ok.cm"
fi
check_panic() {
    local name="$1"
    shift
    local out code
    out=$($TIMEOUT_CMD "$@" 2>&1)
    code=$?
    if [ "$code" -ne 0 ] && echo "$out" | grep -q "runtime error:"; then
        pass "$name"
    else
        fail "$name" "exit=$code out=$out"
    fi
}
if $CM compile --sanitize=undefined -O0 "$CASES/div_zero.cm" -o "$WORK/dz_undef" >/dev/null 2>&1; then
    check_panic "undefined/native: division by zero panics" "$WORK/dz_undef"
else
    fail "undefined/native: compile div_zero.cm"
fi
if $CM compile --sanitize=undefined -O0 "$CASES/null_deref.cm" -o "$WORK/nd_undef" >/dev/null 2>&1; then
    check_panic "undefined/native: null pointer dereference panics" "$WORK/nd_undef"
else
    fail "undefined/native: compile null_deref.cm"
fi
check_panic "undefined/jit: division by zero panics" \
    "$CM" run --sanitize=undefined -O0 "$CASES/div_zero.cm"
check_panic "undefined/jit: null pointer dereference panics" \
    "$CM" run --sanitize=undefined -O0 "$CASES/null_deref.cm"
if command -v wasmtime >/dev/null 2>&1; then
    if $CM compile --target=wasm --sanitize=undefined -O0 "$CASES/div_zero.cm" -o "$WORK/dz_undef.wasm" >/dev/null 2>&1; then
        check_panic "undefined/wasm: division by zero panics" wasmtime "$WORK/dz_undef.wasm"
    else
        fail "undefined/wasm: compile div_zero.cm"
    fi
else
    skip "undefined/wasm: wasmtime not installed"
fi

# ---------- thread: 計装検証 + 正常系プローブ ----------
if $CM compile --sanitize=thread -O0 "$CASES/ok.cm" -o "$WORK/ok_tsan" >/dev/null 2>&1; then
    if nm "$WORK/ok_tsan" 2>/dev/null | grep -q '__tsan_init'; then
        pass "thread/native: binary is TSan-instrumented (__tsan_init)"
    else
        fail "thread/native: __tsan_init not found in binary"
    fi
    probe_out=$($TIMEOUT_CMD "$WORK/ok_tsan" 2>&1)
    probe_code=$?
    if [ "$probe_code" -eq 0 ]; then
        pass "thread/native: in-bounds program runs normally under TSan"
    else
        skip "thread/native runtime check: TSan runtime unhealthy on this OS (probe exit=$probe_code)"
    fi
else
    fail "thread/native: compile ok.cm"
fi

# ---------- memory: Linuxのみ実行時検証 ----------
if [ "$(uname -s)" = "Linux" ]; then
    if $CM compile --sanitize=memory -O0 "$CASES/ok.cm" -o "$WORK/ok_msan" >/dev/null 2>&1; then
        if nm "$WORK/ok_msan" 2>/dev/null | grep -q '__msan'; then
            pass "memory/native: binary is MSan-instrumented (__msan)"
        else
            fail "memory/native: __msan not found in binary"
        fi
    else
        fail "memory/native: compile ok.cm"
    fi
fi

# ---------- .cmconfig.yml compile.sanitize ----------
CFG_DIR="$WORK/cmconfig"
mkdir -p "$CFG_DIR"
cp "$CASES/oob_write.cm" "$CFG_DIR/"
printf 'compile:\n  sanitize: bounds\n' > "$CFG_DIR/.cmconfig.yml"
if (cd "$CFG_DIR" && "$CM" compile -O0 oob_write.cm -o oob_cfg) >/dev/null 2>&1; then
    run_expect "cmconfig: compile.sanitize=bounds is applied" trap "$CFG_DIR/oob_cfg"
else
    fail "cmconfig: compile with config"
fi
printf 'compile:\n  sanitize: nosuch\n' > "$CFG_DIR/.cmconfig.yml"
cfg_out=$(cd "$CFG_DIR" && "$CM" compile -O0 oob_write.cm -o oob_cfg2 2>&1)
if [ $? -eq 0 ] && echo "$cfg_out" | grep -q 'invalid compile.sanitize'; then
    pass "cmconfig: invalid sanitize value warns and is ignored"
else
    fail "cmconfig: invalid sanitize value warns and is ignored" "$cfg_out"
fi

echo ""
echo "Sanitizer tests: PASS=$PASS FAIL=$FAIL SKIP=$SKIP"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
