#!/usr/bin/env python3
# ベンチマーク4: 配列ソート（バブルソート）
# 1,000要素の配列をソート

import time

def bubble_sort(arr):
    n = len(arr)
    for i in range(n - 1):
        for j in range(n - i - 1):
            if arr[j] > arr[j + 1]:
                arr[j], arr[j + 1] = arr[j + 1], arr[j]

def main():
    n = 1000

    # 逆順で初期化（最悪ケース）
    arr = [n - i for i in range(n)]

    start_time = time.time()
    bubble_sort(arr)
    elapsed_time = time.time() - start_time

    # 検証：ソートされているか確認
    sorted_correctly = all(arr[i] <= arr[i+1] for i in range(len(arr)-1))

    if sorted_correctly:
        print(f"Successfully sorted {n} elements")
    else:
        print("Sort failed!")

    print(f"Time: {elapsed_time:.6f} seconds")

if __name__ == "__main__":
    main()