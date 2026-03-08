#!/usr/bin/env python3
# ベンチマーク2: フィボナッチ数列（再帰版）
# 30番目のフィボナッチ数を計算

import time

def fibonacci(n):
    if n <= 1:
        return n
    return fibonacci(n - 1) + fibonacci(n - 2)

def main():
    n = 30

    start_time = time.time()
    result = fibonacci(n)
    elapsed_time = time.time() - start_time

    print(f"Fibonacci({n}) = {result}")
    print(f"Time: {elapsed_time:.6f} seconds")

if __name__ == "__main__":
    main()