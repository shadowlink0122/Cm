// Rustベンチマークプログラム
// 同じアルゴリズムでCm言語と比較

use std::time::Instant;
use std::fs::File;
use std::io::Write;

// ベンチマーク1: 素数計算
fn is_prime(n: i32) -> bool {
    if n <= 1 {
        return false;
    }
    if n <= 3 {
        return true;
    }
    if n % 2 == 0 || n % 3 == 0 {
        return false;
    }

    let mut i = 5;
    while i * i <= n {
        if n % i == 0 || n % (i + 2) == 0 {
            return false;
        }
        i += 6;
    }
    true
}

fn count_primes(limit: i32) -> i32 {
    let mut count = 0;
    for i in 2..=limit {
        if is_prime(i) {
            count += 1;
        }
    }
    count
}

// ベンチマーク2: フィボナッチ計算（再帰）
fn fibonacci_recursive(n: i32) -> i32 {
    if n <= 1 {
        return n;
    }
    fibonacci_recursive(n - 1) + fibonacci_recursive(n - 2)
}

// ベンチマーク3: フィボナッチ計算（反復）
fn fibonacci_iterative(n: i32) -> i32 {
    if n <= 1 {
        return n;
    }

    let mut prev = 0;
    let mut curr = 1;
    for _ in 2..=n {
        let next = prev + curr;
        prev = curr;
        curr = next;
    }
    curr
}

// ベンチマーク4: 配列操作
fn array_operations(size: usize) {
    let mut arr: Vec<i32> = (0..size as i32).collect();

    // バブルソート
    for i in 0..size - 1 {
        for j in 0..size - i - 1 {
            if arr[j] > arr[j + 1] {
                arr.swap(j, j + 1);
            }
        }
    }

    // 配列の合計
    let _sum: i32 = arr.iter().sum();
}

// ベンチマーク5: 行列乗算
fn matrix_multiply(n: usize) {
    // 行列初期化
    let mut a = vec![vec![0; n]; n];
    let mut b = vec![vec![0; n]; n];
    let mut c = vec![vec![0; n]; n];

    for i in 0..n {
        for j in 0..n {
            a[i][j] = (i + j) as i32;
            b[i][j] = (i as i32) - (j as i32);
        }
    }

    // 行列乗算
    for i in 0..n {
        for j in 0..n {
            for k in 0..n {
                c[i][j] += a[i][k] * b[k][j];
            }
        }
    }
}

fn main() {
    println!("Rust Benchmark Starting...");
    let total_start = Instant::now();

    // ベンチマーク1: 素数計算（10000までの素数をカウント）
    println!("Benchmark 1: Prime Numbers");
    let bench1_start = Instant::now();
    let prime_count = count_primes(10000);
    let bench1_time = bench1_start.elapsed();
    println!("  Primes found: {}", prime_count);
    println!("  Time: {:?}", bench1_time);

    // ベンチマーク2: フィボナッチ（再帰）
    println!("Benchmark 2: Fibonacci (Recursive)");
    let bench2_start = Instant::now();
    let mut fib_result = 0;
    for _ in 0..100 {
        fib_result += fibonacci_recursive(20);
    }
    let bench2_time = bench2_start.elapsed();
    println!("  Result: {}", fib_result);
    println!("  Time: {:?}", bench2_time);

    // ベンチマーク3: フィボナッチ（反復）
    println!("Benchmark 3: Fibonacci (Iterative)");
    let bench3_start = Instant::now();
    let mut fib_iter_sum = 0;
    for _ in 0..10000 {
        fib_iter_sum += fibonacci_iterative(30);
    }
    let bench3_time = bench3_start.elapsed();
    println!("  Result: {}", fib_iter_sum);
    println!("  Time: {:?}", bench3_time);

    // ベンチマーク4: 配列操作（1000要素のバブルソートを10回）
    println!("Benchmark 4: Array Operations");
    let bench4_start = Instant::now();
    for _ in 0..10 {
        array_operations(1000);
    }
    let bench4_time = bench4_start.elapsed();
    println!("  Completed 10 iterations");
    println!("  Time: {:?}", bench4_time);

    // ベンチマーク5: 行列乗算（50x50行列を100回）
    println!("Benchmark 5: Matrix Multiplication");
    let bench5_start = Instant::now();
    for _ in 0..100 {
        matrix_multiply(50);
    }
    let bench5_time = bench5_start.elapsed();
    println!("  Completed 100 iterations");
    println!("  Time: {:?}", bench5_time);

    let total_time = total_start.elapsed();

    println!("\nRust Benchmark Completed!");
    println!("Total time: {:?}", total_time);

    // 結果をファイルに出力
    let mut file = File::create("rust_results.txt").expect("Unable to create file");
    writeln!(file, "Rust Benchmark Results").unwrap();
    writeln!(file, "=====================").unwrap();
    writeln!(file, "Prime Numbers: {:?}", bench1_time).unwrap();
    writeln!(file, "Fibonacci (Recursive): {:?}", bench2_time).unwrap();
    writeln!(file, "Fibonacci (Iterative): {:?}", bench3_time).unwrap();
    writeln!(file, "Array Operations: {:?}", bench4_time).unwrap();
    writeln!(file, "Matrix Multiplication: {:?}", bench5_time).unwrap();
    writeln!(file, "Total: {:?}", total_time).unwrap();
}