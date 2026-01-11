#!/usr/bin/env python3
# ベンチマーク5: 行列乗算
# 500x500の行列同士を乗算

import time

def matrix_multiply(n):
    # 行列初期化
    a = [[(i + j) % 100 for j in range(n)] for i in range(n)]
    b = [[(i - j + 100) % 100 for j in range(n)] for i in range(n)]
    c = [[0 for _ in range(n)] for _ in range(n)]

    # 行列乗算
    for i in range(n):
        for j in range(n):
            sum_val = 0
            for k in range(n):
                sum_val += a[i][k] * b[k][j]
            c[i][j] = sum_val

    return c

def main():
    n = 500
    print(f"Multiplying {n}x{n} matrices...")

    start_time = time.time()
    result = matrix_multiply(n)
    elapsed_time = time.time() - start_time

    # 結果の一部を表示（検証用）
    print(f"Result c[0][0] = {result[0][0]}")
    print(f"Result c[{n-1}][{n-1}] = {result[n-1][n-1]}")
    print(f"Time: {elapsed_time:.6f} seconds")

if __name__ == "__main__":
    main()