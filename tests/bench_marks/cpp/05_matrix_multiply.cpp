// ベンチマーク5: 行列乗算
// 500x500の行列同士を乗算

#include <chrono>
#include <iostream>
#include <vector>

using namespace std;
using namespace std::chrono;

void matrix_multiply(int n) {
    vector<vector<int>> a(n, vector<int>(n));
    vector<vector<int>> b(n, vector<int>(n));
    vector<vector<int>> c(n, vector<int>(n, 0));

    // 行列初期化
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            a[i][j] = (i + j) % 100;
            b[i][j] = (i - j + 100) % 100;
        }
    }

    // 行列乗算
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            int sum = 0;
            for (int k = 0; k < n; k++) {
                sum += a[i][k] * b[k][j];
            }
            c[i][j] = sum;
        }
    }

    // 結果の一部を表示（検証用）
    cout << "Result c[0][0] = " << c[0][0] << endl;
    cout << "Result c[" << n - 1 << "][" << n - 1 << "] = " << c[n - 1][n - 1] << endl;
}

int main() {
    int n = 500;
    cout << "Multiplying " << n << "x" << n << " matrices..." << endl;

    auto start = high_resolution_clock::now();
    matrix_multiply(n);
    auto end = high_resolution_clock::now();

    auto duration = duration_cast<microseconds>(end - start);

    cout << "Matrix multiplication completed" << endl;
    cout << "Time: " << duration.count() / 1000000.0 << " seconds" << endl;

    return 0;
}