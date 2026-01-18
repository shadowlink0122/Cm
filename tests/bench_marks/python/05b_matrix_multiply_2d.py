#!/usr/bin/env python3
# ベンチマーク5b: 行列乗算（2D配列版）
# 300x300の行列同士を乗算
# 純粋Python版（Cmの2D配列と比較用）

import time

def matrix_multiply_2d(n):
    """純粋Python版2D配列行列乗算"""
    # 行列初期化（ネストされたリスト = 2D配列）
    a = [[(i + j) % 100 for j in range(n)] for i in range(n)]
    b = [[(i - j + 100) % 100 for j in range(n)] for i in range(n)]
    c = [[0 for _ in range(n)] for _ in range(n)]

    # 行列乗算（ikj順）
    for i in range(n):
        for k in range(n):
            a_ik = a[i][k]
            for j in range(n):
                c[i][j] += a_ik * b[k][j]

    return c

def main():
    n = 300
    print(f"Multiplying {n}x{n} matrices (2D array)...")

    start_time = time.time()
    result = matrix_multiply_2d(n)
    elapsed_time = time.time() - start_time

    print(f"Result c[0][0] = {result[0][0]}")
    print(f"Result c[{n-1}][{n-1}] = {result[n-1][n-1]}")
    print(f"Time: {elapsed_time:.6f} seconds")

if __name__ == "__main__":
    main()
