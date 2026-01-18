# Struct Array Benchmark
import time

class Point:
    def __init__(self, x, y, z):
        self.x = x
        self.y = y
        self.z = z

def main():
    # Struct array: 1000 elements
    points = [Point(i, i * 2, i * 3) for i in range(1000)]
    
    # Distance squared sum 10 times
    total = 0
    for _ in range(10):
        s = 0
        for p in points:
            s += p.x * p.x + p.y * p.y + p.z * p.z
        total += s
    
    print(f"Struct array benchmark completed")
    print(f"Total: {total}")

if __name__ == "__main__":
    main()
