#!/usr/bin/env python3
"""
Python benchmark program
同じアルゴリズムでCm言語と比較
"""

import time
import sys

# ベンチマーク1: 素数計算
def is_prime(n):
    if n <= 1:
        return False
    if n <= 3:
        return True
    if n % 2 == 0 or n % 3 == 0:
        return False

    i = 5
    while i * i <= n:
        if n % i == 0 or n % (i + 2) == 0:
            return False
        i += 6
    return True

def count_primes(limit):
    count = 0
    for i in range(2, limit + 1):
        if is_prime(i):
            count += 1
    return count

# ベンチマーク2: フィボナッチ計算（再帰）
def fibonacci_recursive(n):
    if n <= 1:
        return n
    return fibonacci_recursive(n - 1) + fibonacci_recursive(n - 2)

# ベンチマーク3: フィボナッチ計算（反復）
def fibonacci_iterative(n):
    if n <= 1:
        return n

    prev, curr = 0, 1
    for i in range(2, n + 1):
        next_val = prev + curr
        prev = curr
        curr = next_val
    return curr

# ベンチマーク4: 配列操作
def array_operations(size):
    # 配列初期化
    arr = list(range(size))

    # バブルソート
    for i in range(size - 1):
        for j in range(size - i - 1):
            if arr[j] > arr[j + 1]:
                arr[j], arr[j + 1] = arr[j + 1], arr[j]

    # 配列の合計
    total = sum(arr)
    return total

# ベンチマーク5: 行列乗算
def matrix_multiply(n):
    # 行列初期化
    a = [[i + j for j in range(n)] for i in range(n)]
    b = [[i - j for j in range(n)] for i in range(n)]
    c = [[0 for _ in range(n)] for _ in range(n)]

    # 行列乗算
    for i in range(n):
        for j in range(n):
            for k in range(n):
                c[i][j] += a[i][k] * b[k][j]

    return c

def main():
    print("Python Benchmark Starting...")
    start_time = time.time()

    # ベンチマーク1: 素数計算（10000までの素数をカウント）
    print("Benchmark 1: Prime Numbers")
    bench1_start = time.time()
    prime_count = count_primes(10000)
    bench1_time = time.time() - bench1_start
    print(f"  Primes found: {prime_count}")
    print(f"  Time: {bench1_time:.3f} seconds")

    # ベンチマーク2: フィボナッチ（再帰）
    print("Benchmark 2: Fibonacci (Recursive)")
    bench2_start = time.time()
    fib_result = 0
    for i in range(100):
        fib_result += fibonacci_recursive(20)
    bench2_time = time.time() - bench2_start
    print(f"  Result: {fib_result}")
    print(f"  Time: {bench2_time:.3f} seconds")

    # ベンチマーク3: フィボナッチ（反復）
    print("Benchmark 3: Fibonacci (Iterative)")
    bench3_start = time.time()
    fib_iter_sum = 0
    for i in range(10000):
        fib_iter_sum += fibonacci_iterative(30)
    bench3_time = time.time() - bench3_start
    print(f"  Result: {fib_iter_sum}")
    print(f"  Time: {bench3_time:.3f} seconds")

    # ベンチマーク4: 配列操作（1000要素のバブルソートを10回）
    print("Benchmark 4: Array Operations")
    bench4_start = time.time()
    for i in range(10):
        array_operations(1000)
    bench4_time = time.time() - bench4_start
    print(f"  Completed 10 iterations")
    print(f"  Time: {bench4_time:.3f} seconds")

    # ベンチマーク5: 行列乗算（50x50行列を100回）
    print("Benchmark 5: Matrix Multiplication")
    bench5_start = time.time()
    for i in range(100):
        matrix_multiply(50)
    bench5_time = time.time() - bench5_start
    print(f"  Completed 100 iterations")
    print(f"  Time: {bench5_time:.3f} seconds")

    total_time = time.time() - start_time
    print(f"\nPython Benchmark Completed!")
    print(f"Total time: {total_time:.3f} seconds")

    # 結果をファイルに出力
    with open("python_results.txt", "w") as f:
        f.write(f"Python Benchmark Results\n")
        f.write(f"========================\n")
        f.write(f"Prime Numbers: {bench1_time:.3f}s\n")
        f.write(f"Fibonacci (Recursive): {bench2_time:.3f}s\n")
        f.write(f"Fibonacci (Iterative): {bench3_time:.3f}s\n")
        f.write(f"Array Operations: {bench4_time:.3f}s\n")
        f.write(f"Matrix Multiplication: {bench5_time:.3f}s\n")
        f.write(f"Total: {total_time:.3f}s\n")

if __name__ == "__main__":
    main()