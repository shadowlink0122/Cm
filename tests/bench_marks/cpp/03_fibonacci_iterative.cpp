// ベンチマーク3: フィボナッチ数列（反復版）
// 1,000,000番目のフィボナッチ数を計算

#include <chrono>
#include <iostream>

using namespace std;
using namespace std::chrono;

int fibonacci_iter(int n) {
    if (n <= 1)
        return n;

    int prev = 0;
    int curr = 1;
    const int MOD = 1000000007;

    for (int i = 2; i <= n; i++) {
        int next = (prev + curr) % MOD;
        prev = curr;
        curr = next;
    }

    return curr;
}

int main() {
    int n = 1000000;

    auto start = high_resolution_clock::now();
    int result = fibonacci_iter(n);
    auto end = high_resolution_clock::now();

    auto duration = duration_cast<microseconds>(end - start);

    cout << "Fibonacci(" << n << ") mod 1000000007 = " << result << endl;
    cout << "Time: " << duration.count() / 1000000.0 << " seconds" << endl;

    return 0;
}