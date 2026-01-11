#!/usr/bin/env python3
# ベンチマーク3: フィボナッチ数列（反復版）
# 1,000,000番目のフィボナッチ数を計算

import time

def fibonacci_iter(n):
    if n <= 1:
        return n

    prev, curr = 0, 1
    MOD = 1000000007

    for i in range(2, n + 1):
        next_val = (prev + curr) % MOD
        prev = curr
        curr = next_val

    return curr

def main():
    n = 1000000

    start_time = time.time()
    result = fibonacci_iter(n)
    elapsed_time = time.time() - start_time

    print(f"Fibonacci({n}) mod 1000000007 = {result}")
    print(f"Time: {elapsed_time:.6f} seconds")

if __name__ == "__main__":
    main()