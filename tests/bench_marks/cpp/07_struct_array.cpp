// Struct Array Benchmark - C++
#include <iostream>

struct Point {
    int x;
    int y;
    int z;
};

int main() {
    // Struct array: 1000 elements
    Point points[1000];
    
    // Initialize
    for (int i = 0; i < 1000; i++) {
        points[i].x = i;
        points[i].y = i * 2;
        points[i].z = i * 3;
    }
    
    // Distance squared sum 10 times
    long long total = 0;
    for (int iter = 0; iter < 10; iter++) {
        long long sum = 0;
        for (int i = 0; i < 1000; i++) {
            int x = points[i].x;
            int y = points[i].y;
            int z = points[i].z;
            sum += (long long)(x * x + y * y + z * z);
        }
        total += sum;
    }
    
    std::cout << "Struct array benchmark completed" << std::endl;
    std::cout << "Total: " << total << std::endl;
    
    return 0;
}
