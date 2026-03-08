#!/usr/bin/env python3
# ベンチマーク5: 行列乗算
# 500x500の行列同士を乗算
# NumPy版（高速）

import time

try:
    import numpy as np
    HAS_NUMPY = True
except ImportError:
    HAS_NUMPY = False

def matrix_multiply_numpy(n):
    """NumPyを使った高速行列乗算"""
    # 行列初期化
    a = np.array([[(i + j) % 100 for j in range(n)] for i in range(n)], dtype=np.int32)
    b = np.array([[(i - j + 100) % 100 for j in range(n)] for i in range(n)], dtype=np.int32)
    
    # 行列乗算（NumPyの最適化されたBLAS使用）
    c = np.matmul(a, b)
    
    return c

def matrix_multiply_pure(n):
    """純粋Python版（比較用）"""
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

    if HAS_NUMPY:
        start_time = time.time()
        result = matrix_multiply_numpy(n)
        elapsed_time = time.time() - start_time
        
        print(f"Result c[0][0] = {result[0][0]}")
        print(f"Result c[{n-1}][{n-1}] = {result[n-1][n-1]}")
    else:
        # NumPyがない場合は純粋Python版を使用
        start_time = time.time()
        result = matrix_multiply_pure(n)
        elapsed_time = time.time() - start_time
        
        print(f"Result c[0][0] = {result[0][0]}")
        print(f"Result c[{n-1}][{n-1}] = {result[n-1][n-1]}")
        print("(Warning: NumPy not available, using slow pure Python)")

    print(f"Time: {elapsed_time:.6f} seconds")

if __name__ == "__main__":
    main()