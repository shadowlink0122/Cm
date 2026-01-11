#!/usr/bin/env python3
# ベンチマーク6: 素数計算（エラトステネスの篩）
# 100,000までの素数を効率的に計算

def count_primes_sieve(limit):
    # 配列を初期化
    is_prime = [True] * (limit + 1)
    is_prime[0] = False
    is_prime[1] = False

    # エラトステネスの篩
    i = 2
    while i * i <= limit:
        if is_prime[i]:
            for j in range(i * i, limit + 1, i):
                is_prime[j] = False
        i += 1

    # 素数をカウント
    count = sum(1 for i in range(2, limit + 1) if is_prime[i])
    return count

def main():
    limit = 100000
    count = count_primes_sieve(limit)
    print(f"Prime numbers up to {limit} (sieve): {count}")

if __name__ == "__main__":
    main()