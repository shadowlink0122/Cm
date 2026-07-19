#!/bin/bash
# ============================================================
# TypeScriptバックエンド（--target=ts）のE2Eテスト
# ============================================================
# 各ケースについて:
#   1. --target=ts でコンパイルし、期待する型注釈（interface宣言・: number 等）が出力されることを検証
#   2. tsc が利用可能なら --noEmit で型検査し、生成TSが型エラーなく通ることを検証（無ければ明示SKIP）
#   3. cm run --target=ts で実行し、出力が .expect と一致することを検証（TSは同一コード生成へstripされる）
# さらに広域ゲートとして、jsテストコーパス全体を --target=ts でコンパイルし全プログラムが有効なTSを生成することを確認する。
# ============================================================
set -u
cd "$(dirname "$0")/../.."

CM="${CM_EXECUTABLE:-./cm}"
case "$CM" in
    /*) ;;
    *) CM="$PWD/${CM#./}" ;;
esac
CASES=tests/ts/cases
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

# tscの利用可否を1度だけ判定する（npx経由でtypescriptを取得、キャッシュ済みなら高速）
TSC_AVAILABLE=0
if command -v npx >/dev/null 2>&1; then
    if npx -y -p typescript@5 tsc --version >/dev/null 2>&1; then
        TSC_AVAILABLE=1
    fi
fi

echo "=== TypeScript backend E2E tests ==="

for cm_file in "$CASES"/*.cm; do
    name=$(basename "$cm_file" .cm)
    ts_file="$WORK/$name.ts"

    # 1. TS生成 + 型注釈の存在検証
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

    # 2. tscによる型検査
    if [ "$TSC_AVAILABLE" -eq 1 ]; then
        tsc_out=$($TIMEOUT_CMD npx -y -p typescript@5 tsc --noEmit --skipLibCheck --lib es2018 "$ts_file" 2>&1)
        if [ $? -eq 0 ]; then
            pass "ts/$name: passes tsc type-check"
        else
            fail "ts/$name: tsc type-check" "$(echo "$tsc_out" | head -5)"
        fi
    else
        skip "ts/$name: tsc type-check (npx/typescript not available)"
    fi

    # 3. 実行して出力を検証
    run_out=$($TIMEOUT_CMD "$CM" run --target=ts -O0 "$cm_file" 2>&1)
    expected=$(cat "$CASES/$name.expect")
    if [ "$run_out" = "$expected" ]; then
        pass "ts/$name: run output matches"
    else
        fail "ts/$name: run output" "expected=[$expected] got=[$run_out]"
    fi
done

# 4. 広域ゲート: jsテストコーパス全体が有効なTSを生成できることを確認（tscがある場合のみ型検査、無ければ生成成功のみ）
echo ""
echo "--- Broad gate: js corpus compiles to valid TS ---"
corpus_total=0
corpus_fail=0
corpus_files=$(find tests/js -name '*.cm' -not -path '*/node_modules/*' | sort)
for cm_file in $corpus_files; do
    corpus_total=$((corpus_total + 1))
    ts_out="$WORK/corpus_$(echo "$cm_file" | tr '/.' '__').ts"
    if ! (cd "$(dirname "$cm_file")" && "$CM" compile --target=ts -O0 "$(basename "$cm_file")" -o "$ts_out" >/dev/null 2>&1); then
        corpus_fail=$((corpus_fail + 1))
        echo "  [corpus-fail] $cm_file (compile)"
        continue
    fi
    if [ "$TSC_AVAILABLE" -eq 1 ]; then
        if ! $TIMEOUT_CMD npx -y -p typescript@5 tsc --noEmit --skipLibCheck --lib es2018 "$ts_out" >/dev/null 2>&1; then
            corpus_fail=$((corpus_fail + 1))
            echo "  [corpus-fail] $cm_file (tsc)"
        fi
    fi
done
if [ "$corpus_fail" -eq 0 ]; then
    pass "broad gate: all $corpus_total js-corpus programs produce valid TS"
else
    fail "broad gate: $corpus_fail/$corpus_total js-corpus programs failed TS generation/typecheck"
fi

echo ""
echo "TypeScript tests: PASS=$PASS FAIL=$FAIL SKIP=$SKIP"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
