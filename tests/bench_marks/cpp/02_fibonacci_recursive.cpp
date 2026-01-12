// ベンチマーク2: フィボナッチ数列（再帰版）
// 30番目のフィボナッチ数を計算

#include <chrono>
#include <iostream>

using namespace std;
using namespace std::chrono;

int fibonacci(int n) {
    if (n <= 1)
        return n;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

int main() {
    int n = 30;

    auto start = high_resolution_clock::now();
    int result = fibonacci(n);
    auto end = high_resolution_clock::now();

    auto duration = duration_cast<microseconds>(end - start);

    cout << "Fibonacci(" << n << ") = " << result << endl;
    cout << "Time: " << duration.count() / 1000000.0 << " seconds" << endl;

    return 0;
}