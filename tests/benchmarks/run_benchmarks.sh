#!/bin/bash

# Cm言語ベンチマーク実行スクリプト
# 全言語のベンチマークを実行し、結果を比較

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
RESULTS_DIR="$SCRIPT_DIR/results"
CM_ROOT="$SCRIPT_DIR/../.."

# 色付き出力用の設定
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 結果ディレクトリの作成
mkdir -p "$RESULTS_DIR"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}   Cm Language Benchmark Suite${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# タイムスタンプ
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
RESULT_FILE="$RESULTS_DIR/benchmark_results_${TIMESTAMP}.txt"

# 結果記録関数
record_result() {
    echo "$1" | tee -a "$RESULT_FILE"
}

# 実行時間測定関数
measure_time() {
    local start=$(date +%s.%N)
    "$@"
    local end=$(date +%s.%N)
    echo "$(echo "$end - $start" | bc)"
}

record_result "Benchmark Results - $(date)"
record_result "========================================"
record_result ""

# 1. Python ベンチマーク (インタプリタ比較用)
echo -e "${YELLOW}Running Python Benchmark...${NC}"
cd "$SCRIPT_DIR/python"
record_result "Python Benchmark:"
record_result "-----------------"
if command -v python3 &> /dev/null; then
    PYTHON_TIME=$(measure_time python3 benchmark.py)
    record_result "Execution time: ${PYTHON_TIME}s"
    if [ -f "python_results.txt" ]; then
        cat python_results.txt >> "$RESULT_FILE"
    fi
else
    record_result "Python3 not found, skipping..."
fi
record_result ""

# 2. Cm インタプリタベンチマーク
echo -e "${YELLOW}Running Cm Interpreter Benchmark...${NC}"
cd "$SCRIPT_DIR/cm"
record_result "Cm Interpreter Benchmark:"
record_result "-------------------------"
if [ -f "$CM_ROOT/build/bin/cm" ]; then
    CM_INTERP_TIME=$(measure_time "$CM_ROOT/build/bin/cm" -i benchmark.cm)
    record_result "Execution time: ${CM_INTERP_TIME}s"
else
    record_result "Cm interpreter not found, skipping..."
fi
record_result ""

# 3. C++ ベンチマーク (ネイティブ比較用)
echo -e "${YELLOW}Building and Running C++ Benchmark...${NC}"
cd "$SCRIPT_DIR/cpp"
record_result "C++ Benchmark:"
record_result "--------------"
if command -v clang++ &> /dev/null || command -v g++ &> /dev/null; then
    make clean > /dev/null 2>&1
    make > /dev/null 2>&1
    CPP_TIME=$(measure_time ./benchmark)
    record_result "Execution time: ${CPP_TIME}s"
    if [ -f "cpp_results.txt" ]; then
        cat cpp_results.txt >> "$RESULT_FILE"
    fi
else
    record_result "C++ compiler not found, skipping..."
fi
record_result ""

# 4. Rust ベンチマーク (ネイティブ比較用)
echo -e "${YELLOW}Building and Running Rust Benchmark...${NC}"
cd "$SCRIPT_DIR/rust"
record_result "Rust Benchmark:"
record_result "---------------"
if command -v cargo &> /dev/null; then
    cargo build --release > /dev/null 2>&1
    RUST_TIME=$(measure_time ./target/release/benchmark)
    record_result "Execution time: ${RUST_TIME}s"
    if [ -f "rust_results.txt" ]; then
        cat rust_results.txt >> "$RESULT_FILE"
    fi
else
    record_result "Rust/Cargo not found, skipping..."
fi
record_result ""

# 5. Cm ネイティブベンチマーク (LLVM)
echo -e "${YELLOW}Building and Running Cm Native Benchmark...${NC}"
cd "$SCRIPT_DIR/cm"
record_result "Cm Native (LLVM) Benchmark:"
record_result "----------------------------"
if [ -f "$CM_ROOT/build/bin/cm" ]; then
    # LLVMバックエンドでコンパイル
    "$CM_ROOT/build/bin/cm" -O3 benchmark.cm -o benchmark_native > /dev/null 2>&1
    if [ -f "benchmark_native" ]; then
        CM_NATIVE_TIME=$(measure_time ./benchmark_native)
        record_result "Execution time: ${CM_NATIVE_TIME}s"
    else
        record_result "Failed to compile Cm to native code"
    fi
else
    record_result "Cm compiler not found, skipping..."
fi
record_result ""

# 結果のサマリー表示
echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}   Benchmark Summary${NC}"
echo -e "${GREEN}========================================${NC}"

record_result ""
record_result "Summary"
record_result "======="

# 結果の比較表を作成
echo -e "\n${BLUE}Interpreter Comparison:${NC}"
echo "Language    | Execution Time"
echo "------------|---------------"
if [ ! -z "$PYTHON_TIME" ]; then
    printf "Python      | %s s\n" "$PYTHON_TIME"
fi
if [ ! -z "$CM_INTERP_TIME" ]; then
    printf "Cm (interp) | %s s\n" "$CM_INTERP_TIME"
fi

echo -e "\n${BLUE}Native Comparison:${NC}"
echo "Language    | Execution Time"
echo "------------|---------------"
if [ ! -z "$CPP_TIME" ]; then
    printf "C++         | %s s\n" "$CPP_TIME"
fi
if [ ! -z "$RUST_TIME" ]; then
    printf "Rust        | %s s\n" "$RUST_TIME"
fi
if [ ! -z "$CM_NATIVE_TIME" ]; then
    printf "Cm (native) | %s s\n" "$CM_NATIVE_TIME"
fi

echo ""
echo -e "${GREEN}Results saved to: $RESULT_FILE${NC}"
echo ""

# 速度比較の計算（Pythonがインストールされている場合）
if command -v python3 &> /dev/null; then
    python3 << EOF
import sys

def print_comparison(name1, time1, name2, time2):
    if time1 and time2:
        try:
            t1 = float(time1)
            t2 = float(time2)
            if t1 > 0:
                ratio = t2 / t1
                if ratio < 1:
                    print(f"  {name2} is {1/ratio:.2f}x faster than {name1}")
                else:
                    print(f"  {name1} is {ratio:.2f}x faster than {name2}")
        except:
            pass

print("\nPerformance Comparisons:")
print("------------------------")

# インタプリタ比較
python_time = "$PYTHON_TIME" if "$PYTHON_TIME" else None
cm_interp_time = "$CM_INTERP_TIME" if "$CM_INTERP_TIME" else None
print_comparison("Python", python_time, "Cm (interpreter)", cm_interp_time)

# ネイティブ比較
cpp_time = "$CPP_TIME" if "$CPP_TIME" else None
rust_time = "$RUST_TIME" if "$RUST_TIME" else None
cm_native_time = "$CM_NATIVE_TIME" if "$CM_NATIVE_TIME" else None

if cpp_time and cm_native_time:
    print_comparison("C++", cpp_time, "Cm (native)", cm_native_time)
if rust_time and cm_native_time:
    print_comparison("Rust", rust_time, "Cm (native)", cm_native_time)
if cm_interp_time and cm_native_time:
    print_comparison("Cm (interpreter)", cm_interp_time, "Cm (native)", cm_native_time)
EOF
fi

echo -e "\n${GREEN}Benchmark completed successfully!${NC}"