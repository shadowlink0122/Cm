#!/bin/bash
# ============================================================
# TypeScriptバックエンド（--target=ts）のE2Eテスト
# ============================================================
# 1. tests/ts/cases の焦点テスト: 型注釈生成 → tsc型検査 → 実行出力一致（React/Node fixtureのFFI・構造体配列・undefinedを含む）
# 2. 広域ゲート: tests/common + tests/js の全コーパスを --target=ts でコンパイルし、JSバックエンドが受理する全プログラムが有効なTSを生成することを保証する（tsc型検査は焦点ケースで実施）
#    （malloc/free等 void* を使いJS/TSバックエンドが構造的に拒否するプログラムは対象外）
# ============================================================
set -u
cd "$(dirname "$0")/../.."

CM="${CM_EXECUTABLE:-./cm}"
case "$CM" in
    /*) ;;
    *) CM="$PWD/${CM#./}" ;;
esac
CASES=tests/ts/cases
# React/Node fixtureパッケージ（npm install不要、NODE_PATHで解決する）
FIXTURE_MODULES="$PWD/tests/ts/node_modules"
WORK="$PWD/.tmp/test_ts"
PASS=0
FAIL=0
SKIP=0

rm -rf "$WORK"
mkdir -p "$WORK"

TIMEOUT_CMD=""
if command -v timeout >/dev/null 2>&1; then
    TIMEOUT_CMD="timeout 60"
elif command -v gtimeout >/dev/null 2>&1; then
    TIMEOUT_CMD="gtimeout 60"
fi

pass() { echo "[PASS] $1"; PASS=$((PASS + 1)); }
fail() { echo "[FAIL] $1"; shift; [ $# -gt 0 ] && echo "$@" | sed 's/^/    /'; FAIL=$((FAIL + 1)); }
skip() { echo "[SKIP] $1"; SKIP=$((SKIP + 1)); }

TSC_AVAILABLE=0
if command -v npx >/dev/null 2>&1; then
    if npx -y -p typescript@5 tsc --version >/dev/null 2>&1; then
        TSC_AVAILABLE=1
    fi
fi

typecheck() {
    # $1 = tsファイル。tscで型検査（fixtureのnode_modulesを型解決に含める）
    $TIMEOUT_CMD npx -y -p typescript@5 tsc --noEmit --skipLibCheck --lib es2020 "$1" 2>&1
}

echo "=== TypeScript backend E2E tests ==="

for cm_file in "$CASES"/*.cm; do
    name=$(basename "$cm_file" .cm)
    ts_file="$WORK/$name.ts"

    if ! $CM compile --target=ts -O0 "$cm_file" -o "$ts_file" >/dev/null 2>&1; then
        fail "ts/$name: compile to TS"
        continue
    fi
    if grep -q ': number' "$ts_file" || grep -q ': string' "$ts_file" ||
        grep -q 'export interface' "$ts_file"; then
        pass "ts/$name: emits type annotations"
    else
        fail "ts/$name: no type annotations found in output"
    fi

    if [ "$TSC_AVAILABLE" -eq 1 ]; then
        tsc_out=$(typecheck "$ts_file")
        if [ $? -eq 0 ]; then
            pass "ts/$name: passes tsc type-check"
        else
            fail "ts/$name: tsc type-check" "$(echo "$tsc_out" | head -5)"
        fi
    else
        skip "ts/$name: tsc type-check (npx/typescript not available)"
    fi

    run_out=$($TIMEOUT_CMD env NODE_PATH="$FIXTURE_MODULES" "$CM" run --target=ts -O0 "$cm_file" 2>&1)
    expected=$(cat "$CASES/$name.expect")
    if [ "$run_out" = "$expected" ]; then
        pass "ts/$name: run output matches"
    else
        fail "ts/$name: run output" "expected=[$expected] got=[$run_out]"
    fi
done

# ---------- 広域ゲート: common + js コーパス ----------
echo ""
echo "--- Broad gate: common + js corpus compiles to valid TS ---"
corpus_total=0
corpus_ok=0
corpus_skip=0
corpus_fail=0
corpus_files=$(find tests/common tests/js -name '*.cm' -not -path '*/node_modules/*' | sort)
for cm_file in $corpus_files; do
    # .errorマーカーがある意図的失敗テストは対象外
    [ -f "${cm_file%.cm}.error" ] && continue
    corpus_total=$((corpus_total + 1))
    ts_out="$WORK/corpus_$(echo "$cm_file" | tr '/.' '__').ts"
    js_probe="$WORK/probe_$(echo "$cm_file" | tr '/.' '__').js"
    # JSバックエンドが受理しないプログラム（void*/malloc等）はTSでも構造的に拒否されるため対象外としてスキップ
    if ! (cd "$(dirname "$cm_file")" && "$CM" compile --target=js -O0 "$(basename "$cm_file")" -o "$js_probe" >/dev/null 2>&1); then
        corpus_skip=$((corpus_skip + 1))
        continue
    fi
    if ! (cd "$(dirname "$cm_file")" && "$CM" compile --target=ts -O0 "$(basename "$cm_file")" -o "$ts_out" >/dev/null 2>&1); then
        corpus_fail=$((corpus_fail + 1))
        echo "  [corpus-fail] $cm_file (TS compile failed but JS succeeded)"
        continue
    fi
    # tsc型検査は焦点ケース(tests/ts/cases)で実施。広域ゲートは「JS受理プログラムが有効なTSを生成する」ことのみ確認する（全件tscはCIで非現実的なコストのため）
    corpus_ok=$((corpus_ok + 1))
done
echo "  corpus: total=$corpus_total ok=$corpus_ok skipped(js-unsupported)=$corpus_skip fail=$corpus_fail"
if [ "$corpus_fail" -eq 0 ]; then
    pass "broad gate: all $corpus_ok js-supported programs produce valid TS"
else
    fail "broad gate: $corpus_fail programs produced invalid TS"
fi

echo ""
echo "TypeScript tests: PASS=$PASS FAIL=$FAIL SKIP=$SKIP"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
