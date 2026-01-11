#!/usr/bin/env python3
# ベンチマーク7: フィボナッチ数列（動的計画法版）
# 45番目のフィボナッチ数を効率的に計算

def main():
    n = 45  # int範囲内に収まる値

    # 動的計画法でフィボナッチを計算
    if n <= 0:
        print(f"Fibonacci({n}) = 0")
        return

    if n == 1:
        print(f"Fibonacci({n}) = 1")
        return

    prev2 = 0
    prev1 = 1
    result = 0

    for i in range(2, n + 1):
        result = prev1 + prev2
        prev2 = prev1
        prev1 = result

    print(f"Fibonacci({n}) with DP = {result}")

if __name__ == "__main__":
    main()