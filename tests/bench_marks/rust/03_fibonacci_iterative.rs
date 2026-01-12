// ベンチマーク3: フィボナッチ数列（反復版）
// 1,000,000番目のフィボナッチ数を計算

use std::time::Instant;

fn fibonacci_iter(n: i32) -> i32 {
    if n <= 1 {
        return n;
    }

    let mut prev = 0;
    let mut curr = 1;
    const MOD: i32 = 1000000007;

    for _ in 2..=n {
        let next = (prev + curr) % MOD;
        prev = curr;
        curr = next;
    }

    curr
}

fn main() {
    let n = 1000000;

    let start = Instant::now();
    let result = fibonacci_iter(n);
    let duration = start.elapsed();

    println!("Fibonacci({}) mod 1000000007 = {}", n, result);
    println!("Time: {:.6} seconds", duration.as_secs_f64());
}