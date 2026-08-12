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

# 前回実行の残骸による偽PASSを防ぐため作業ディレクトリは毎回作り直す
rm -rf "$WORK"
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

# check_panic <名前> <コマンド...>（非0終了 + "runtime error:" を含むpanic出力を要求）
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

# check_panic_msg <名前> <panicメッセージに含むべき文字列> <コマンド...>
check_panic_msg() {
    local name="$1" needle="$2"
    shift 2
    local out code
    out=$($TIMEOUT_CMD "$@" 2>&1)
    code=$?
    if [ "$code" -ne 0 ] && echo "$out" | grep -qF "$needle"; then
        pass "$name"
    else
        fail "$name" "exit=$code out=$out"
    fi
}

# run_output <名前> <出力に含むべき文字列> <コマンド...>（正常終了 + 出力内容も検証し、ガード挿入による誤計算を検出する）
run_output() {
    local name="$1" needle="$2"
    shift 2
    local out code
    out=$($TIMEOUT_CMD "$@" 2>&1)
    code=$?
    if [ "$code" -eq 0 ] && echo "$out" | grep -qF "$needle"; then
        pass "$name"
    else
        fail "$name" "exit=$code out=$out"
    fi
}

# check_asan <名前> <ケースファイル> <ASanレポートに含むべき文字列>
# 注意: localの1行複数代入で前の変数を参照するとbash 5.x（set -u）で未束縛エラーになるため行を分ける
check_asan() {
    local name="$1"
    local case_file="$2"
    local needle="$3"
    local bin
    bin="$WORK/asan_$(basename "$case_file" .cm)"
    if ! $CM compile --sanitize=address -O0 "$case_file" -o "$bin" >/dev/null 2>&1; then
        fail "$name" "compile failed"
        return
    fi
    local out code
    out=$($TIMEOUT_CMD "$bin" 2>&1)
    code=$?
    if [ "$code" -ne 0 ] && echo "$out" | grep -qF "$needle"; then
        pass "$name"
    else
        fail "$name" "exit=$code out=$(echo "$out" | head -3)"
    fi
}

echo "=== Sanitizer E2E tests ==="

# ---------- CLI検証 ----------
expect_msg "cli: unknown sanitizer value" "unknown sanitizer 'foo'" \
    "$CM" compile --sanitize=foo "$CASES/valid/ok.cm" -o "$WORK/never"
expect_msg "cli: address is rejected on wasm" "not supported on target 'wasm'" \
    "$CM" compile --target=wasm --sanitize=address "$CASES/valid/ok.cm" -o "$WORK/never"
expect_msg "cli: address is rejected on jit run" "not supported on target 'jit'" \
    "$CM" run --sanitize=address "$CASES/valid/ok.cm"
expect_msg "cli: address is rejected on js" "not supported on target" \
    "$CM" compile --target=js --sanitize=address "$CASES/valid/ok.cm" -o "$WORK/never.js"
expect_msg "cli: ja message" "エラー: 不明なサニタイザ" \
    "$CM" compile --lang=ja --sanitize=foo "$CASES/valid/ok.cm" -o "$WORK/never"
expect_msg "cli: thread is rejected on jit run" "not supported on target 'jit'" \
    "$CM" run --sanitize=thread "$CASES/valid/ok.cm"
expect_msg "cli: thread is rejected on wasm" "not supported on target 'wasm'" \
    "$CM" compile --target=wasm --sanitize=thread "$CASES/valid/ok.cm" -o "$WORK/never"
if [ "$(uname -s)" = "Darwin" ]; then
    expect_msg "cli: memory is rejected on macOS" "only supported on Linux" \
        "$CM" compile --sanitize=memory "$CASES/valid/ok.cm" -o "$WORK/never"
fi

# ---------- bounds: native ----------
if $CM compile --sanitize=bounds -O0 "$CASES/valid/ok.cm" -o "$WORK/ok_bounds" >/dev/null 2>&1; then
    run_expect "bounds/native: in-bounds program runs normally" zero "$WORK/ok_bounds"
else
    fail "bounds/native: compile ok.cm"
fi
if $CM compile --sanitize=bounds -O0 "$CASES/oob/write.cm" -o "$WORK/oob_bounds" >/dev/null 2>&1; then
    run_expect "bounds/native: out-of-bounds write traps" trap "$WORK/oob_bounds"
else
    fail "bounds/native: compile oob/write.cm"
fi

# ---------- bounds: jit ----------
run_expect "bounds/jit: in-bounds program runs normally" zero \
    "$CM" run --sanitize=bounds -O0 "$CASES/valid/ok.cm"
run_expect "bounds/jit: out-of-bounds write traps" trap \
    "$CM" run --sanitize=bounds -O0 "$CASES/oob/write.cm"

# ---------- bounds: wasm ----------
if command -v wasmtime >/dev/null 2>&1; then
    if $CM compile --target=wasm --sanitize=bounds -O0 "$CASES/valid/ok.cm" -o "$WORK/ok_bounds.wasm" >/dev/null 2>&1 &&
        $CM compile --target=wasm --sanitize=bounds -O0 "$CASES/oob/write.cm" -o "$WORK/oob_bounds.wasm" >/dev/null 2>&1; then
        run_expect "bounds/wasm: in-bounds program runs normally" zero wasmtime "$WORK/ok_bounds.wasm"
        run_expect "bounds/wasm: out-of-bounds write traps" trap wasmtime "$WORK/oob_bounds.wasm"
    else
        fail "bounds/wasm: compile"
    fi
else
    skip "bounds/wasm: wasmtime not installed"
fi

# ---------- bounds: スライス境界（M1。全実行系で同一メッセージのトラップ） ----------
if $CM compile --sanitize=bounds -O0 "$CASES/oob/slice_read.cm" -o "$WORK/slice_oob" >/dev/null 2>&1; then
    expect_msg "bounds/native: slice out-of-bounds read traps with message" "index out of bounds" "$WORK/slice_oob"
else
    fail "bounds/native: compile oob/slice_read.cm"
fi
if $CM compile --sanitize=bounds -O0 "$CASES/oob/slice_write.cm" -o "$WORK/slice_oob_w" >/dev/null 2>&1; then
    expect_msg "bounds/native: slice out-of-bounds write traps with message" "index out of bounds" "$WORK/slice_oob_w"
else
    fail "bounds/native: compile oob/slice_write.cm"
fi
expect_msg "bounds/jit: slice out-of-bounds read traps with message" "index out of bounds" \
    "$CM" run --sanitize=bounds -O0 "$CASES/oob/slice_read.cm"
if command -v wasmtime >/dev/null 2>&1; then
    if $CM compile --target=wasm --sanitize=bounds -O0 "$CASES/oob/slice_read.cm" -o "$WORK/slice_oob.wasm" >/dev/null 2>&1; then
        expect_msg "bounds/wasm: slice out-of-bounds read traps with message" "index out of bounds" wasmtime "$WORK/slice_oob.wasm"
    else
        fail "bounds/wasm: compile oob/slice_read.cm"
    fi
else
    skip "bounds/wasm: wasmtime not installed"
fi
if command -v node >/dev/null 2>&1; then
    if $CM compile --target=js --sanitize=bounds -O0 "$CASES/oob/slice_read.cm" -o "$WORK/slice_oob.js" >/dev/null 2>&1; then
        expect_msg "bounds/js: slice out-of-bounds read traps with message" "index out of bounds" node "$WORK/slice_oob.js"
    else
        fail "bounds/js: compile oob/slice_read.cm"
    fi
else
    skip "bounds/js: node not installed"
fi
# サニタイズ無効時はスライス読みが従来どおり通る（既定の挙動は不変）
run_expect "bounds: slice read without sanitize keeps legacy behavior" zero \
    "$CM" run -O0 "$CASES/oob/slice_read.cm"

# ---------- address: 計装検証（シンボルレベル。全環境で実施） ----------
if $CM compile --sanitize=address -O0 "$CASES/oob/write.cm" -o "$WORK/oob_asan" >/dev/null 2>&1; then
    if nm "$WORK/oob_asan" 2>/dev/null | grep -q '__asan_init'; then
        pass "address/native: binary is ASan-instrumented (__asan_init)"
    else
        fail "address/native: __asan_init not found in binary"
    fi
else
    fail "address/native: compile oob/write.cm"
fi
if nm "$WORK/oob_bounds" 2>/dev/null | grep -q '__asan'; then
    fail "bounds/native: bounds binary must not link ASan"
else
    pass "bounds/native: no ASan symbols in bounds binary"
fi

# ---------- address: 実行時検証（ランタイムが動作する環境のみ） ----------
# プローブ: 正常プログラムのASanビルドが正常終了するかでランタイムの健全性を判定する
if $CM compile --sanitize=address -O0 "$CASES/valid/ok.cm" -o "$WORK/ok_asan" >/dev/null 2>&1; then
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

        # ヒープ系（malloc/free）の検出と正常系の無影響
        check_asan "address/native: heap buffer overflow is reported" "$CASES/heap/oob.cm" "heap-buffer-overflow"
        check_asan "address/native: use-after-free is reported" "$CASES/heap/use_after_free.cm" "heap-use-after-free"
        check_asan "address/native: double free is reported" "$CASES/heap/double_free.cm" "double-free"
        if $CM compile --sanitize=address -O0 "$CASES/valid/heap.cm" -o "$WORK/asan_heap_ok" >/dev/null 2>&1; then
            run_output "address/native: valid malloc/free has no false positive" "sum=30" "$WORK/asan_heap_ok"
        else
            fail "address/native: compile heap_ok.cm"
        fi
        if $CM compile --sanitize=address -O0 "$CASES/valid/move.cm" -o "$WORK/asan_move_ok" >/dev/null 2>&1; then
            run_output "address/native: move semantics program runs correctly" "olen=1" "$WORK/asan_move_ok"
        else
            fail "address/native: compile move_ok.cm"
        fi
    else
        skip "address/native runtime checks: ASan runtime unhealthy on this OS (probe exit=$probe_code)"
    fi
else
    fail "address/native: compile ok.cm"
fi

# ---------- undefined: MIRレベル検査（native/jit/wasm） ----------
if $CM compile --sanitize=undefined -O0 "$CASES/valid/ok.cm" -o "$WORK/ok_undef" >/dev/null 2>&1; then
    run_expect "undefined/native: in-bounds program runs normally" zero "$WORK/ok_undef"
else
    fail "undefined/native: compile ok.cm"
fi
if $CM compile --sanitize=undefined -O0 "$CASES/zero/div.cm" -o "$WORK/dz_undef" >/dev/null 2>&1; then
    check_panic "undefined/native: division by zero panics" "$WORK/dz_undef"
else
    fail "undefined/native: compile div_zero.cm"
fi
if $CM compile --sanitize=undefined -O0 "$CASES/null/deref.cm" -o "$WORK/nd_undef" >/dev/null 2>&1; then
    check_panic "undefined/native: null pointer dereference panics" "$WORK/nd_undef"
else
    fail "undefined/native: compile null_deref.cm"
fi
check_panic "undefined/jit: division by zero panics" \
    "$CM" run --sanitize=undefined -O0 "$CASES/zero/div.cm"
check_panic "undefined/jit: null pointer dereference panics" \
    "$CM" run --sanitize=undefined -O0 "$CASES/null/deref.cm"
if command -v wasmtime >/dev/null 2>&1; then
    if $CM compile --target=wasm --sanitize=undefined -O0 "$CASES/zero/div.cm" -o "$WORK/dz_undef.wasm" >/dev/null 2>&1; then
        check_panic "undefined/wasm: division by zero panics" wasmtime "$WORK/dz_undef.wasm"
    else
        fail "undefined/wasm: compile div_zero.cm"
    fi
else
    skip "undefined/wasm: wasmtime not installed"
fi
if command -v node >/dev/null 2>&1; then
    check_panic "undefined/js: division by zero panics" \
        "$CM" run --target=js --sanitize=undefined -O0 "$CASES/zero/div.cm"
    check_panic "undefined/js: null pointer dereference panics" \
        "$CM" run --target=js --sanitize=undefined -O0 "$CASES/null/deref.cm"
    run_expect "undefined/js: in-bounds program runs normally" zero \
        "$CM" run --target=js --sanitize=undefined -O0 "$CASES/valid/ok.cm"
    # boundsはMIRレベル計装になりjsでも動作する（M1で対応。従来は拒否していた）
    run_expect "cli: bounds is accepted on js (in-bounds runs normally)" zero \
        "$CM" run --target=js --sanitize=bounds "$CASES/valid/ok.cm"
else
    skip "undefined/js: node not installed"
fi

# ---------- undefined: ポインタ・剰余の複雑ケース ----------
run_output "undefined: valid stack pointer has no false positive" "v=10 x=10" \
    "$CM" run --sanitize=undefined -O0 "$CASES/valid/pointer.cm"
run_output "undefined: valid heap (malloc/free) has no false positive" "sum=30" \
    "$CM" run --sanitize=undefined -O0 "$CASES/valid/heap.cm"
check_panic "undefined/jit: store through null pointer panics" \
    "$CM" run --sanitize=undefined -O0 "$CASES/null/store.cm"
check_panic "undefined/jit: null pointer passed to function panics on deref" \
    "$CM" run --sanitize=undefined -O0 "$CASES/null/arg.cm"
check_panic_msg "undefined/jit: modulo by zero panics with its own message" "modulo by zero" \
    "$CM" run --sanitize=undefined -O0 "$CASES/zero/mod.cm"
if $CM compile --sanitize=undefined -O0 "$CASES/null/store.cm" -o "$WORK/ns_undef" >/dev/null 2>&1; then
    check_panic "undefined/native: store through null pointer panics" "$WORK/ns_undef"
else
    fail "undefined/native: compile null_store.cm"
fi

# ---------- bounds: 読み取りOOBとmoveの無影響 ----------
if $CM compile --sanitize=bounds -O0 "$CASES/oob/read.cm" -o "$WORK/oob_read_b" >/dev/null 2>&1; then
    run_expect "bounds/native: out-of-bounds read traps" trap "$WORK/oob_read_b"
else
    fail "bounds/native: compile oob_read.cm"
fi
run_output "bounds/jit: move semantics program runs correctly" "olen=1" \
    "$CM" run --sanitize=bounds -O0 "$CASES/valid/move.cm"
run_output "undefined/jit: move semantics program runs correctly" "olen=1" \
    "$CM" run --sanitize=undefined -O0 "$CASES/valid/move.cm"

# ---------- thread: 計装検証 + 正常系プローブ ----------
if $CM compile --sanitize=thread -O0 "$CASES/valid/ok.cm" -o "$WORK/ok_tsan" >/dev/null 2>&1; then
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
    if $CM compile --sanitize=memory -O0 "$CASES/valid/ok.cm" -o "$WORK/ok_msan" >/dev/null 2>&1; then
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
cp "$CASES/oob/write.cm" "$CFG_DIR/write.cm"
printf 'compile:\n  sanitize: bounds\n' > "$CFG_DIR/.cmconfig.yml"
if (cd "$CFG_DIR" && "$CM" compile -O0 write.cm -o oob_cfg) >/dev/null 2>&1; then
    run_expect "cmconfig: compile.sanitize=bounds is applied" trap "$CFG_DIR/oob_cfg"
else
    fail "cmconfig: compile with config"
fi
printf 'compile:\n  sanitize: nosuch\n' > "$CFG_DIR/.cmconfig.yml"
cfg_out=$(cd "$CFG_DIR" && "$CM" compile -O0 write.cm -o oob_cfg2 2>&1)
if [ $? -eq 0 ] && echo "$cfg_out" | grep -q 'invalid compile.sanitize'; then
    pass "cmconfig: invalid sanitize value warns and is ignored"
else
    fail "cmconfig: invalid sanitize value warns and is ignored" "$cfg_out"
fi

echo ""
echo "Sanitizer tests: PASS=$PASS FAIL=$FAIL SKIP=$SKIP"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
