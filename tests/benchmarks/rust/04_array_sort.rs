// ベンチマーク4: 配列ソート（バブルソート）
// 1,000要素の配列をソート

use std::time::Instant;

fn bubble_sort(arr: &mut Vec<i32>) {
    let n = arr.len();
    for i in 0..n - 1 {
        for j in 0..n - i - 1 {
            if arr[j] > arr[j + 1] {
                arr.swap(j, j + 1);
            }
        }
    }
}

fn main() {
    let n = 1000;

    // 逆順で初期化（最悪ケース）
    let mut arr: Vec<i32> = (1..=n as i32).rev().collect();

    let start = Instant::now();
    bubble_sort(&mut arr);
    let duration = start.elapsed();

    // 検証：ソートされているか確認
    let sorted = arr.windows(2).all(|w| w[0] <= w[1]);

    if sorted {
        println!("Successfully sorted {} elements", n);
    } else {
        println!("Sort failed!");
    }
    println!("Time: {:.6} seconds", duration.as_secs_f64());
}