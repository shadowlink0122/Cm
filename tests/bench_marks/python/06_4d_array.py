# 4D Array Benchmark
import time

def main():
    # 4D array: 10x10x10x10 = 10000 elements
    arr4d = [[[[0 for _ in range(10)] for _ in range(10)] for _ in range(10)] for _ in range(10)]
    
    # Initialize
    for i in range(10):
        for j in range(10):
            for k in range(10):
                for l in range(10):
                    arr4d[i][j][k][l] = i * 1000 + j * 100 + k * 10 + l
    
    # Sum 10 times
    total = 0
    for _ in range(10):
        s = 0
        for i in range(10):
            for j in range(10):
                for k in range(10):
                    for l in range(10):
                        s += arr4d[i][j][k][l]
        total += s
    
    print(f"4D array benchmark completed")
    print(f"Total: {total}")

if __name__ == "__main__":
    main()
