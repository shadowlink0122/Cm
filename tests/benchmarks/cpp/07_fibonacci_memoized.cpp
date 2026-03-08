// ベンチマーク7: フィボナッチ数列（動的計画法版）
// 45番目のフィボナッチ数を効率的に計算

#include <chrono>
#include <iostream>

using namespace std;
using namespace std::chrono;

int main() {
    int n = 45;  // int範囲内に収まる値

    auto start = high_resolution_clock::now();

    // 動的計画法でフィボナッチを計算
    if (n <= 0) {
        cout << "Fibonacci(" << n << ") = 0" << endl;
        return 0;
    }

    if (n == 1) {
        cout << "Fibonacci(" << n << ") = 1" << endl;
        return 0;
    }

    int prev2 = 0;
    int prev1 = 1;
    int result = 0;

    for (int i = 2; i <= n; i++) {
        result = prev1 + prev2;
        prev2 = prev1;
        prev1 = result;
    }

    auto end = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(end - start);

    cout << "Fibonacci(" << n << ") with DP = " << result << endl;
    cout << "Time: " << duration.count() / 1000000.0 << " seconds" << endl;

    return 0;
}