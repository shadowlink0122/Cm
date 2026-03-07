// Matrix Multiply 2D Array Benchmark - C++ (matching Cm version)
#include <iostream>

int main() {
    const int N = 300;

    // 2D static arrays (to avoid stack overflow)
    static int a[N][N];
    static int b[N][N];
    static int c[N][N];

    // Initialize a and b (same as Cm version)
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            a[i][j] = (i + j) % 100;
            b[i][j] = (i - j + 100) % 100;  // Match Cm version
            c[i][j] = 0;
        }
    }

    std::cout << "Multiplying " << N << "x" << N << " matrices (2D array)..." << std::endl;

    // Matrix multiplication (IKJ order for cache efficiency)
    for (int i = 0; i < N; i++) {
        for (int k = 0; k < N; k++) {
            int a_ik = a[i][k];
            for (int j = 0; j < N; j++) {
                c[i][j] += a_ik * b[k][j];
            }
        }
    }

    std::cout << "Result c[0][0] = " << c[0][0] << std::endl;
    std::cout << "Result c[n-1][n-1] = " << c[N - 1][N - 1] << std::endl;
    std::cout << "Matrix multiplication completed" << std::endl;

    return 0;
}
