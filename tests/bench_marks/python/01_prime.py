#!/usr/bin/env python3
# ベンチマーク1: 素数計算
# 100,000までの素数を全てカウント

import time

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

def main():
    limit = 100000
    count = 0

    start_time = time.time()

    for i in range(2, limit + 1):
        if is_prime(i):
            count += 1

    elapsed_time = time.time() - start_time

    print(f"Prime numbers up to {limit}: {count}")
    print(f"Time: {elapsed_time:.6f} seconds")

if __name__ == "__main__":
    main()