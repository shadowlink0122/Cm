#include <iostream>
int main() {
    static int a[300][300];
    static int b[300][300];
    static int c[300][300];
    // 初期化
    for (int i = 0; i < 300; i++) {
        for (int j = 0; j < 300; j++) {
            a[i][j] = 1;
            b[i][j] = 1;
            c[i][j] = 0;
        }
    }
    // 行列乗算
    for (int i = 0; i < 300; i++) {
        for (int k = 0; k < 300; k++) {
            int a_ik = a[i][k];
            for (int j = 0; j < 300; j++) {
                c[i][j] = c[i][j] + a_ik * b[k][j];
            }
        }
    }
    std::cout << "done " << c[0][0] << std::endl;
    return 0;
}
