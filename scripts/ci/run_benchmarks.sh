#!/usr/bin/env bash
# JITベンチマークの実行（タイムアウト付き。失敗があれば非0終了）
set -euo pipefail

echo "=== Running Cm JIT Benchmarks ==="

# timeoutコマンド（macOSはgtimeout）
TIMEOUT_CMD="timeout"
command -v timeout >/dev/null || TIMEOUT_CMD="gtimeout"

PASS=0
FAIL=0

for bench in 06_prime_sieve 07_fibonacci_memoized 04_array_sort 05_matrix_multiply; do
    echo ""
    echo "--- $bench ---"

    if [ -f "tests/benchmarks/cm/${bench}.cm" ]; then
        echo "Running tests/benchmarks/cm/${bench}.cm..."
        if "$TIMEOUT_CMD" 120 ./cm run "tests/benchmarks/cm/${bench}.cm" > /dev/null 2>&1; then
            echo "✅ $bench: PASS"
            PASS=$((PASS + 1))
        else
            echo "❌ $bench: FAIL"
            FAIL=$((FAIL + 1))
        fi
    else
        echo "⚠️ $bench: SKIP (file not found)"
    fi
done

echo ""
echo "=== Benchmark Results ==="
echo "Passed: $PASS"
echo "Failed: $FAIL"

if [ $FAIL -gt 0 ]; then
    echo "❌ Some benchmarks failed"
    exit 1
fi
echo "✅ All benchmarks passed!"
