// ベンチマーク5: 行列乗算（生配列版）
// 500x500の行列同士を乗算 - Vecではなく静的配列を使用（スタック/BSS領域）

use std::time::Instant;

// 定数サイズ
const N: usize = 500;

// 静的配列（BSSセグメント - ヒープではない）
static mut A: [[i32; N]; N] = [[0; N]; N];
static mut B: [[i32; N]; N] = [[0; N]; N];
static mut C: [[i32; N]; N] = [[0; N]; N];

fn matrix_multiply() {
    unsafe {
        // 行列初期化
        for i in 0..N {
            for j in 0..N {
                A[i][j] = ((i + j) % 100) as i32;
                B[i][j] = ((i as i32 - j as i32 + 100) % 100) as i32;
                C[i][j] = 0;
            }
        }

        // 行列乗算（標準IJK順）
        for i in 0..N {
            for j in 0..N {
                let mut sum = 0i32;
                for k in 0..N {
                    sum += A[i][k] * B[k][j];
                }
                C[i][j] = sum;
            }
        }

        // 結果の一部を表示（検証用）
        println!("Result c[0][0] = {}", C[0][0]);
        println!("Result c[{}][{}] = {}", N-1, N-1, C[N-1][N-1]);
    }
}

fn main() {
    println!("Multiplying {}x{} matrices (static array)...", N, N);

    let start = Instant::now();
    matrix_multiply();
    let duration = start.elapsed();

    println!("Matrix multiplication completed");
    println!("Time: {:.6} seconds", duration.as_secs_f64());
}
