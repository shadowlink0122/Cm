// ベンチマーク5b: 行列乗算（2D配列版）
// 300x300の行列同士を乗算
// Cm言語の2D配列と比較用

use std::time::Instant;

fn matrix_multiply_2d(n: usize) {
    // 行列初期化（Vec<Vec<i32>> = 2D配列）
    let mut a = vec![vec![0i32; n]; n];
    let mut b = vec![vec![0i32; n]; n];
    let mut c = vec![vec![0i32; n]; n];

    for i in 0..n {
        for j in 0..n {
            a[i][j] = ((i + j) % 100) as i32;
            b[i][j] = ((i as i32 - j as i32 + 100) % 100) as i32;
        }
    }

    // 行列乗算（ikj順）
    for i in 0..n {
        for k in 0..n {
            let a_ik = a[i][k];
            for j in 0..n {
                c[i][j] += a_ik * b[k][j];
            }
        }
    }

    // 結果の一部を表示（検証用）
    println!("Result c[0][0] = {}", c[0][0]);
    println!("Result c[{}][{}] = {}", n-1, n-1, c[n-1][n-1]);
}

fn main() {
    let n = 300;
    println!("Multiplying {}x{} matrices (2D array)...", n, n);

    let start = Instant::now();
    matrix_multiply_2d(n);
    let duration = start.elapsed();

    println!("Matrix multiplication completed");
    println!("Time: {:.6} seconds", duration.as_secs_f64());
}
