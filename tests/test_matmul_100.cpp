#include <iostream>
int main() {
    int a[100][100];
    int b[100][100];
    int c[100][100];
    // 初期化
    for (int i = 0; i < 100; i++) {
        for (int j = 0; j < 100; j++) {
            a[i][j] = 1;
            b[i][j] = 1;
            c[i][j] = 0;
        }
    }
    // 行列乗算
    for (int i = 0; i < 100; i++) {
        for (int k = 0; k < 100; k++) {
            int a_ik = a[i][k];
            for (int j = 0; j < 100; j++) {
                c[i][j] = c[i][j] + a_ik * b[k][j];
            }
        }
    }
    std::cout << "done " << c[0][0] << std::endl;
    return 0;
}
