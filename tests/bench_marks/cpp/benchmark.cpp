// C++ベンチマークプログラム
// 同じアルゴリズムでCm言語と比較

#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>

using namespace std;
using namespace std::chrono;

// ベンチマーク1: 素数計算
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

int count_primes(int limit) {
    int count = 0;
    for (int i = 2; i <= limit; i++) {
        if (is_prime(i)) {
            count++;
        }
    }
    return count;
}

// ベンチマーク2: フィボナッチ計算（再帰）
int fibonacci_recursive(int n) {
    if (n <= 1)
        return n;
    return fibonacci_recursive(n - 1) + fibonacci_recursive(n - 2);
}

// ベンチマーク3: フィボナッチ計算（反復）
int fibonacci_iterative(int n) {
    if (n <= 1)
        return n;

    int prev = 0;
    int curr = 1;
    for (int i = 2; i <= n; i++) {
        int next = prev + curr;
        prev = curr;
        curr = next;
    }
    return curr;
}

// ベンチマーク4: 配列操作
void array_operations(int size) {
    vector<int> arr(size);

    // 配列初期化
    for (int i = 0; i < size; i++) {
        arr[i] = i;
    }

    // バブルソート
    for (int i = 0; i < size - 1; i++) {
        for (int j = 0; j < size - i - 1; j++) {
            if (arr[j] > arr[j + 1]) {
                int temp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = temp;
            }
        }
    }

    // 配列の合計
    int sum = 0;
    for (int i = 0; i < size; i++) {
        sum += arr[i];
    }
}

// ベンチマーク5: 行列乗算
void matrix_multiply(int n) {
    vector<vector<int>> a(n, vector<int>(n));
    vector<vector<int>> b(n, vector<int>(n));
    vector<vector<int>> c(n, vector<int>(n, 0));

    // 行列初期化
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            a[i][j] = i + j;
            b[i][j] = i - j;
        }
    }

    // 行列乗算
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            for (int k = 0; k < n; k++) {
                c[i][j] += a[i][k] * b[k][j];
            }
        }
    }
}

int main() {
    cout << "C++ Benchmark Starting..." << endl;
    auto total_start = high_resolution_clock::now();

    // ベンチマーク1: 素数計算（10000までの素数をカウント）
    cout << "Benchmark 1: Prime Numbers" << endl;
    auto bench1_start = high_resolution_clock::now();
    int prime_count = count_primes(10000);
    auto bench1_end = high_resolution_clock::now();
    auto bench1_time = duration_cast<milliseconds>(bench1_end - bench1_start).count();
    cout << "  Primes found: " << prime_count << endl;
    cout << "  Time: " << bench1_time << " ms" << endl;

    // ベンチマーク2: フィボナッチ（再帰）
    cout << "Benchmark 2: Fibonacci (Recursive)" << endl;
    auto bench2_start = high_resolution_clock::now();
    int fib_result = 0;
    for (int i = 0; i < 100; i++) {
        fib_result += fibonacci_recursive(20);
    }
    auto bench2_end = high_resolution_clock::now();
    auto bench2_time = duration_cast<milliseconds>(bench2_end - bench2_start).count();
    cout << "  Result: " << fib_result << endl;
    cout << "  Time: " << bench2_time << " ms" << endl;

    // ベンチマーク3: フィボナッチ（反復）
    cout << "Benchmark 3: Fibonacci (Iterative)" << endl;
    auto bench3_start = high_resolution_clock::now();
    int fib_iter_sum = 0;
    for (int i = 0; i < 10000; i++) {
        fib_iter_sum += fibonacci_iterative(30);
    }
    auto bench3_end = high_resolution_clock::now();
    auto bench3_time = duration_cast<milliseconds>(bench3_end - bench3_start).count();
    cout << "  Result: " << fib_iter_sum << endl;
    cout << "  Time: " << bench3_time << " ms" << endl;

    // ベンチマーク4: 配列操作（1000要素のバブルソートを10回）
    cout << "Benchmark 4: Array Operations" << endl;
    auto bench4_start = high_resolution_clock::now();
    for (int i = 0; i < 10; i++) {
        array_operations(1000);
    }
    auto bench4_end = high_resolution_clock::now();
    auto bench4_time = duration_cast<milliseconds>(bench4_end - bench4_start).count();
    cout << "  Completed 10 iterations" << endl;
    cout << "  Time: " << bench4_time << " ms" << endl;

    // ベンチマーク5: 行列乗算（50x50行列を100回）
    cout << "Benchmark 5: Matrix Multiplication" << endl;
    auto bench5_start = high_resolution_clock::now();
    for (int i = 0; i < 100; i++) {
        matrix_multiply(50);
    }
    auto bench5_end = high_resolution_clock::now();
    auto bench5_time = duration_cast<milliseconds>(bench5_end - bench5_start).count();
    cout << "  Completed 100 iterations" << endl;
    cout << "  Time: " << bench5_time << " ms" << endl;

    auto total_end = high_resolution_clock::now();
    auto total_time = duration_cast<milliseconds>(total_end - total_start).count();

    cout << "\nC++ Benchmark Completed!" << endl;
    cout << "Total time: " << total_time << " ms" << endl;

    // 結果をファイルに出力
    ofstream out("cpp_results.txt");
    out << "C++ Benchmark Results" << endl;
    out << "=====================" << endl;
    out << "Prime Numbers: " << bench1_time << " ms" << endl;
    out << "Fibonacci (Recursive): " << bench2_time << " ms" << endl;
    out << "Fibonacci (Iterative): " << bench3_time << " ms" << endl;
    out << "Array Operations: " << bench4_time << " ms" << endl;
    out << "Matrix Multiplication: " << bench5_time << " ms" << endl;
    out << "Total: " << total_time << " ms" << endl;
    out.close();

    return 0;
}