#!/bin/bash
# ============================================================
# libs 単体テストランナー
# ============================================================
# libs/ 以下の *_test.cm（#[test] 属性付き関数）を discover して `cm test` で実行する。
# これはコンパイラのテストではなくライブラリ自体のテストなので、テストはライブラリと同じ
# ディレクトリ（libs/**/​*_test.cm）に置き、このランナーだけを tests/ 側に置く。
# ============================================================
set -u
cd "$(dirname "$0")/../.."

CM="${CM_EXECUTABLE:-./cm}"
FILES_PASS=0
FILES_FAIL=0

if [ ! -x "$CM" ]; then
    echo "error: compiler not found at $CM (run: make build-compiler)" >&2
    exit 1
fi

echo "=== libs unit tests (cm test) ==="

# libs 配下の *_test.cm を再帰的に探して実行する
while IFS= read -r test_file; do
    rel="${test_file#./}"
    out=$("$CM" test "$test_file" 2>&1)
    rc=$?
    if [ $rc -eq 0 ] && echo "$out" | grep -q "test(s) passed"; then
        count=$(echo "$out" | grep -oE '[0-9]+ test\(s\) passed' | grep -oE '^[0-9]+')
        echo "[PASS] $rel (${count:-?} tests)"
        FILES_PASS=$((FILES_PASS + 1))
    else
        echo "[FAIL] $rel (exit=$rc)"
        echo "$out" | sed 's/^/    /' | head -20
        FILES_FAIL=$((FILES_FAIL + 1))
    fi
done < <(find libs -name '*_test.cm' | sort)

echo ""
echo "libs tests: files PASS=$FILES_PASS FAIL=$FILES_FAIL"
[ "$FILES_FAIL" -eq 0 ]
