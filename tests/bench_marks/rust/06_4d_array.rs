// 4D Array Benchmark - Rust (Large Scale)
fn main() {
    // 4D array: 30x30x30x30 = 810,000 elements
    let mut arr4d = [[[[0i32; 30]; 30]; 30]; 30];
    
    // Initialize
    for i in 0..30 {
        for j in 0..30 {
            for k in 0..30 {
                for l in 0..30 {
                    arr4d[i][j][k][l] = (i + j + k + l) as i32;
                }
            }
        }
    }
    
    // Sum 5 times
    let mut total: i64 = 0;
    for _ in 0..5 {
        let mut sum: i64 = 0;
        for i in 0..30 {
            for j in 0..30 {
                for k in 0..30 {
                    for l in 0..30 {
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
