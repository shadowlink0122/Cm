// ベンチマーク1: 素数計算
// 100,000までの素数を全てカウント

#include <chrono>
#include <iostream>

using namespace std;
using namespace std::chrono;

bool is_prime(int n) {
    if (n <= 1)
        return false;
    if (n <= 3)
        return true;
    if (n % 2 == 0 || n % 3 == 0)
        return false;

    int i = 5;
    while (i * i <= n) {
        if (n % i == 0 || n % (i + 2) == 0) {
            return false;
        }
        i += 6;
    }
    return true;
}

int main() {
    int limit = 100000;
    int count = 0;

    auto start = high_resolution_clock::now();

    for (int i = 2; i <= limit; i++) {
        if (is_prime(i)) {
            count++;
        }
    }

    auto end = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(end - start);

    cout << "Prime numbers up to " << limit << ": " << count << endl;
    cout << "Time: " << duration.count() / 1000000.0 << " seconds" << endl;

    return 0;
}