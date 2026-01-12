// ベンチマーク2: フィボナッチ数列（再帰版）
// 30番目のフィボナッチ数を計算

use std::time::Instant;

fn fibonacci(n: i32) -> i32 {
    if n <= 1 {
        return n;
    }
    fibonacci(n - 1) + fibonacci(n - 2)
}

fn main() {
    let n = 30;

    let start = Instant::now();
    let result = fibonacci(n);
    let duration = start.elapsed();

    println!("Fibonacci({}) = {}", n, result);
    println!("Time: {:.6} seconds", duration.as_secs_f64());
}