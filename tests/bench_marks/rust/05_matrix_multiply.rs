// ベンチマーク5: 行列乗算
// 500x500の行列同士を乗算

use std::time::Instant;

fn matrix_multiply(n: usize) {
    // 行列初期化
    let mut a = vec![vec![0; n]; n];
    let mut b = vec![vec![0; n]; n];
    let mut c = vec![vec![0; n]; n];

    for i in 0..n {
        for j in 0..n {
            a[i][j] = ((i + j) % 100) as i32;
            b[i][j] = ((i as i32 - j as i32 + 100) % 100) as i32;
        }
    }

    // 行列乗算
    for i in 0..n {
        for j in 0..n {
            let mut sum = 0;
            for k in 0..n {
                sum += a[i][k] * b[k][j];
            }
            c[i][j] = sum;
        }
    }

    // 結果の一部を表示（検証用）
    println!("Result c[0][0] = {}", c[0][0]);
    println!("Result c[{}][{}] = {}", n-1, n-1, c[n-1][n-1]);
}

fn main() {
    let n = 500;
    println!("Multiplying {}x{} matrices...", n, n);

    let start = Instant::now();
    matrix_multiply(n);
    let duration = start.elapsed();

    println!("Matrix multiplication completed");
    println!("Time: {:.6} seconds", duration.as_secs_f64());
}