// ベンチマーク5: 行列乗算（生配列版）
// 500x500の行列同士を乗算 - vectorではなく生の2次元配列を使用

#include <chrono>
#include <iostream>

using namespace std;
using namespace std::chrono;

// 定数サイズの配列（コンパイル時確定）
constexpr int N = 500;

// グローバル配列（スタックオーバーフロー防止）
int a[N][N];
int b[N][N];
int c[N][N];

void matrix_multiply() {
    // 行列初期化
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            a[i][j] = (i + j) % 100;
            b[i][j] = (i - j + 100) % 100;
            c[i][j] = 0;
        }
    }

    // 行列乗算（標準IJK順）
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            int sum = 0;
            for (int k = 0; k < N; k++) {
                sum += a[i][k] * b[k][j];
            }
            c[i][j] = sum;
        }
    }

    // 結果の一部を表示（検証用）
    cout << "Result c[0][0] = " << c[0][0] << endl;
    cout << "Result c[" << N - 1 << "][" << N - 1 << "] = " << c[N - 1][N - 1] << endl;
}

int main() {
    cout << "Multiplying " << N << "x" << N << " matrices (raw array)..." << endl;

    auto start = high_resolution_clock::now();
    matrix_multiply();
    auto end = high_resolution_clock::now();

    auto duration = duration_cast<microseconds>(end - start);

    cout << "Matrix multiplication completed" << endl;
    cout << "Time: " << duration.count() / 1000000.0 << " seconds" << endl;

    return 0;
}
