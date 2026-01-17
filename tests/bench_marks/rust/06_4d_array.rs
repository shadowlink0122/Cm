// 4D Array Benchmark - Rust
fn main() {
    // 4D array: 10x10x10x10 = 10000 elements
    let mut arr4d = [[[[0i32; 10]; 10]; 10]; 10];
    
    // Initialize
    for i in 0..10 {
        for j in 0..10 {
            for k in 0..10 {
                for l in 0..10 {
                    arr4d[i][j][k][l] = (i * 1000 + j * 100 + k * 10 + l) as i32;
                }
            }
        }
    }
    
    // Sum 10 times
    let mut total: i64 = 0;
    for _ in 0..10 {
        let mut sum: i64 = 0;
        for i in 0..10 {
            for j in 0..10 {
                for k in 0..10 {
                    for l in 0..10 {
                        sum += arr4d[i][j][k][l] as i64;
                    }
                }
            }
        }
        total += sum;
    }
    
    println!("4D array benchmark completed");
    println!("Total: {}", total);
}
