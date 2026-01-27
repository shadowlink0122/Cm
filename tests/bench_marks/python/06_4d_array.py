# 4D Array Benchmark (Large Scale)
import time

def main():
    # 4D array: 30x30x30x30 = 810,000 elements
    arr4d = [[[[0 for _ in range(30)] for _ in range(30)] for _ in range(30)] for _ in range(30)]
    
    # Initialize
    for i in range(30):
        for j in range(30):
            for k in range(30):
                for l in range(30):
                    arr4d[i][j][k][l] = i + j + k + l
    
    # Sum 5 times
    total = 0
    for _ in range(5):
        s = 0
        for i in range(30):
            for j in range(30):
                for k in range(30):
                    for l in range(30):
                        s += arr4d[i][j][k][l]
        total += s
    
    print(f"4D array benchmark completed")
    print(f"Total: {total}")

if __name__ == "__main__":
    main()
