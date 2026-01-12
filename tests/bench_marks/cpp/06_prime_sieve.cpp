// ベンチマーク6: 素数計算（エラトステネスの篩）
// 100,000までの素数を効率的に計算

#include <chrono>
#include <iostream>
#include <vector>

using namespace std;
using namespace std::chrono;

int count_primes_sieve(int limit) {
    // 配列を初期化
    vector<bool> is_prime(limit + 1, true);
    is_prime[0] = false;
    is_prime[1] = false;

    // エラトステネスの篩
    for (int i = 2; i * i <= limit; i++) {
        if (is_prime[i]) {
            for (int j = i * i; j <= limit; j += i) {
                is_prime[j] = false;
            }
        }
    }

    // 素数をカウント
    int count = 0;
    for (int i = 2; i <= limit; i++) {
        if (is_prime[i]) {
            count++;
        }
    }

    return count;
}

int main() {
    int limit = 100000;

    auto start = high_resolution_clock::now();
    int count = count_primes_sieve(limit);
    auto end = high_resolution_clock::now();

    auto duration = duration_cast<microseconds>(end - start);

    cout << "Prime numbers up to " << limit << " (sieve): " << count << endl;
    cout << "Time: " << duration.count() / 1000000.0 << " seconds" << endl;

    return 0;
}