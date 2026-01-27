// 4D Array Benchmark - C++ (Large Scale)
#include <iostream>

int main() {
    // 4D array: 30x30x30x30 = 810,000 elements
    static int arr4d[30][30][30][30];

    // Initialize
    for (int i = 0; i < 30; i++) {
        for (int j = 0; j < 30; j++) {
            for (int k = 0; k < 30; k++) {
                for (int l = 0; l < 30; l++) {
                    arr4d[i][j][k][l] = i + j + k + l;
                }
            }
        }
    }

    // Sum 5 times
    long long total = 0;
    for (int iter = 0; iter < 5; iter++) {
        long long sum = 0;
        for (int i = 0; i < 30; i++) {
            for (int j = 0; j < 30; j++) {
                for (int k = 0; k < 30; k++) {
                    for (int l = 0; l < 30; l++) {
                        sum += arr4d[i][j][k][l];
                    }
                }
            }
        }
        total += sum;
    }

    std::cout << "4D array benchmark completed" << std::endl;
    std::cout << "Total: " << total << std::endl;

    return 0;
}
