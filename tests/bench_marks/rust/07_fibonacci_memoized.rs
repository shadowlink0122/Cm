// ベンチマーク7: フィボナッチ数列（動的計画法版）
// 45番目のフィボナッチ数を効率的に計算

use std::time::Instant;

fn main() {
    let n = 45;  // int範囲内に収まる値

    let start = Instant::now();

    // 動的計画法でフィボナッチを計算
    if n <= 0 {
        println!("Fibonacci({}) = 0", n);
        return;
    }

    if n == 1 {
        println!("Fibonacci({}) = 1", n);
        return;
    }

    let mut prev2 = 0;
    let mut prev1 = 1;
    let mut result = 0;

    for _ in 2..=n {
        result = prev1 + prev2;
        prev2 = prev1;
        prev1 = result;
    }

    let duration = start.elapsed();

    println!("Fibonacci({}) with DP = {}", n, result);
    println!("Time: {:.6} seconds", duration.as_secs_f64());
}