// ベンチマーク6: 素数計算（エラトステネスの篩）
// 100,000までの素数を効率的に計算

use std::time::Instant;

fn count_primes_sieve(limit: usize) -> usize {
    // 配列を初期化
    let mut is_prime = vec![true; limit + 1];
    is_prime[0] = false;
    is_prime[1] = false;

    // エラトステネスの篩
    let mut i = 2;
    while i * i <= limit {
        if is_prime[i] {
            let mut j = i * i;
            while j <= limit {
                is_prime[j] = false;
                j += i;
            }
        }
        i += 1;
    }

    // 素数をカウント
    (2..=limit).filter(|&i| is_prime[i]).count()
}

fn main() {
    let limit = 100000;

    let start = Instant::now();
    let count = count_primes_sieve(limit);
    let duration = start.elapsed();

    println!("Prime numbers up to {} (sieve): {}", limit, count);
    println!("Time: {:.6} seconds", duration.as_secs_f64());
}