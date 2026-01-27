// Struct Array Benchmark - Rust
struct Point {
    x: i32,
    y: i32,
    z: i32,
}

fn main() {
    // Struct array: 1000 elements
    let mut points: Vec<Point> = Vec::with_capacity(1000);
    for i in 0..1000 {
        points.push(Point {
            x: i as i32,
            y: (i * 2) as i32,
            z: (i * 3) as i32,
        });
    }
    
    // Distance squared sum 10 times
    let mut total: i64 = 0;
    for _ in 0..10 {
        let mut sum: i64 = 0;
        for p in &points {
            sum += (p.x * p.x + p.y * p.y + p.z * p.z) as i64;
        }
        total += sum;
    }
    
    println!("Struct array benchmark completed");
    println!("Total: {}", total);
}
