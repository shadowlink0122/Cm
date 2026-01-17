// 4D Array Benchmark - C++
#include <iostream>

int main() {
    // 4D array: 10x10x10x10 = 10000 elements
    int arr4d[10][10][10][10];
    
    // Initialize
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            for (int k = 0; k < 10; k++) {
                for (int l = 0; l < 10; l++) {
                    arr4d[i][j][k][l] = i * 1000 + j * 100 + k * 10 + l;
                }
            }
        }
    }
    
    // Sum 10 times
    long long total = 0;
    for (int iter = 0; iter < 10; iter++) {
        long long sum = 0;
        for (int i = 0; i < 10; i++) {
            for (int j = 0; j < 10; j++) {
                for (int k = 0; k < 10; k++) {
                    for (int l = 0; l < 10; l++) {
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
