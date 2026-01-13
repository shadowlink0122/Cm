#!/bin/bash

# Cm言語個別ベンチマーク実行スクリプト
# 各アルゴリズムを個別に測定し、ボトルネックを特定

# エラーがあっても継続（ベンチマークの一部が失敗しても他を実行）
set +e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
RESULTS_DIR="$SCRIPT_DIR/results"
CM_ROOT="$SCRIPT_DIR/../.."

# 色付き出力用の設定
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# 結果ディレクトリの作成
mkdir -p "$RESULTS_DIR"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}   Cm Language Benchmark Suite${NC}"
echo -e "${BLUE}   Individual Algorithm Tests${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# タイムスタンプ
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
RESULT_FILE="$RESULTS_DIR/individual_results_${TIMESTAMP}.csv"

# CSV ヘッダー
echo "Algorithm,Python,Cm(JIT),C++,Rust,Cm(Native)" > "$RESULT_FILE"

# ベンチマーク名
declare -a BENCHMARKS=(
    "01_prime:Prime Numbers (100k) - Trial Division"
    "06_prime_sieve:Prime Numbers (100k) - Sieve"
    "02_fibonacci_recursive:Fibonacci Recursive (30) - Naive"
    "07_fibonacci_memoized:Fibonacci (45) - Dynamic Programming"
    "03_fibonacci_iterative:Fibonacci Iterative (1M)"
    "04_array_sort:Array Sort (1k)"
    "05_matrix_multiply:Matrix Multiply (500x500)"
)

# 実行時間測定関数（タイムアウト付き）
measure_time() {
    local timeout_cmd=""
    local timeout_seconds=60  # デフォルト60秒タイムアウト

    # タイムアウトコマンドを検出
    if command -v gtimeout &> /dev/null; then
        timeout_cmd="gtimeout $timeout_seconds"
    elif command -v timeout &> /dev/null; then
        timeout_cmd="timeout $timeout_seconds"
    fi

    if [ -n "$timeout_cmd" ]; then
        # タイムアウト付き実行
        if command -v gtime &> /dev/null; then
            # GNU time (macOS with coreutils)
            $timeout_cmd gtime -f "%e" "$@" 2>&1 | tail -n 1
        elif command -v /usr/bin/time &> /dev/null; then
            # BSD time
            $timeout_cmd /usr/bin/time -p "$@" 2>&1 | grep real | awk '{print $2}'
        else
            # fallback
            local start=$(date +%s.%N)
            $timeout_cmd "$@" > /dev/null 2>&1
            local status=$?
            if [ $status -eq 124 ] || [ $status -eq 137 ]; then
                echo "-"
                return
            fi
            local end=$(date +%s.%N)
            echo "$(echo "$end - $start" | bc)"
        fi
    else
        # タイムアウトなし（オリジナル）
        if command -v gtime &> /dev/null; then
            gtime -f "%e" "$@" 2>&1 | tail -n 1
        elif command -v /usr/bin/time &> /dev/null; then
            /usr/bin/time -p "$@" 2>&1 | grep real | awk '{print $2}'
        else
            local start=$(date +%s.%N)
            "$@" > /dev/null 2>&1
            local end=$(date +%s.%N)
            echo "$(echo "$end - $start" | bc)"
        fi
    fi
}

# 言語ごとのビルド
build_all() {
    echo -e "${YELLOW}Building all benchmarks...${NC}"

    # C++
    if command -v clang++ &> /dev/null || command -v g++ &> /dev/null; then
        echo "Building C++ benchmarks..."
        cd "$SCRIPT_DIR/cpp"
        make clean > /dev/null 2>&1
        make all > /dev/null 2>&1 || true
    fi

    # Rust
    if command -v cargo &> /dev/null; then
        echo "Building Rust benchmarks..."
        cd "$SCRIPT_DIR/rust"
        cargo build --release > /dev/null 2>&1
    fi

    # Cm Native (必要に応じて)
    if [ -f "$CM_ROOT/cm" ]; then
        echo "Building Cm native benchmarks..."
        cd "$SCRIPT_DIR/cm"
        for file in *.cm; do
            base="${file%.cm}"
            "$CM_ROOT/cm" compile -O3 "$file" -o "${base}_native" > /dev/null 2>&1 || true
        done
    fi
}

# 個別ベンチマーク実行
run_benchmark() {
    local bench_name="$1"
    local display_name="$2"

    echo -e "\n${CYAN}=== $display_name ===${NC}"

    local python_time="-"
    local cm_jit_time="-"
    local cpp_time="-"
    local rust_time="-"
    local cm_native_time="-"

    # Python
    if [ -f "$SCRIPT_DIR/python/${bench_name}.py" ]; then
        if command -v python3 &> /dev/null; then
            echo -n "Python...       "
            python_time=$(measure_time python3 "$SCRIPT_DIR/python/${bench_name}.py" 2>/dev/null)
            printf "%8.4f s\n" "$python_time"
        fi
    fi

    # Cm JIT
    if [ -f "$SCRIPT_DIR/cm/${bench_name}.cm" ] && [ -f "$CM_ROOT/cm" ]; then
        echo -n "Cm (JIT)...     "
        cm_jit_time=$(measure_time "$CM_ROOT/cm" run "$SCRIPT_DIR/cm/${bench_name}.cm" 2>/dev/null)
        if [ "$cm_jit_time" = "-" ]; then
            echo "TIMEOUT"
        else
            printf "%8.4f s\n" "$cm_jit_time"
        fi
    fi

    # C++
    if [ -f "$SCRIPT_DIR/cpp/${bench_name}" ]; then
        echo -n "C++...          "
        cpp_time=$(measure_time "$SCRIPT_DIR/cpp/${bench_name}" 2>/dev/null)
        printf "%8.4f s\n" "$cpp_time"
    fi

    # Rust
    if [ -f "$SCRIPT_DIR/rust/target/release/${bench_name}" ]; then
        echo -n "Rust...         "
        rust_time=$(measure_time "$SCRIPT_DIR/rust/target/release/${bench_name}" 2>/dev/null)
        printf "%8.4f s\n" "$rust_time"
    fi

    # Cm Native
    if [ -f "$SCRIPT_DIR/cm/${bench_name}_native" ]; then
        echo -n "Cm (Native)...  "
        cm_native_time=$(measure_time "$SCRIPT_DIR/cm/${bench_name}_native" 2>/dev/null)
        printf "%8.4f s\n" "$cm_native_time"
    fi

    # CSV に記録
    echo "$display_name,$python_time,$cm_jit_time,$cpp_time,$rust_time,$cm_native_time" >> "$RESULT_FILE"

    # 速度比較
    echo -e "${GREEN}Speed comparison:${NC}"

    # Python vs Cm JIT
    if [ "$python_time" != "-" ] && [ "$cm_jit_time" != "-" ]; then
        ratio=$(echo "scale=2; $python_time / $cm_jit_time" | bc)
        echo "  Cm JIT vs Python: ${ratio}x"
    fi

    # C++ vs Cm Native
    if [ "$cpp_time" != "-" ] && [ "$cm_native_time" != "-" ]; then
        ratio=$(echo "scale=2; $cpp_time / $cm_native_time" | bc)
        echo "  Cm Native vs C++: ${ratio}x"
    fi

    # Rust vs Cm Native
    if [ "$rust_time" != "-" ] && [ "$cm_native_time" != "-" ]; then
        ratio=$(echo "scale=2; $rust_time / $cm_native_time" | bc)
        echo "  Cm Native vs Rust: ${ratio}x"
    fi
}

# メイン処理
main() {
    # ビルド
    build_all

    # 各ベンチマーク実行
    for bench_entry in "${BENCHMARKS[@]}"; do
        IFS=':' read -r bench_name display_name <<< "$bench_entry"
        run_benchmark "$bench_name" "$display_name"
    done

    echo ""
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}   Summary Table${NC}"
    echo -e "${GREEN}========================================${NC}"

    # 結果テーブルの表示
    echo ""
    echo "Algorithm                    | Python  | Cm(JIT) | C++     | Rust    | Cm(Nat)"
    echo "-----------------------------|---------|---------|---------|---------|--------"

    # CSVから読み込んで整形表示
    tail -n +2 "$RESULT_FILE" | while IFS=',' read -r algo py cm_i cpp rust cm_n; do
        printf "%-28s | %7s | %7s | %7s | %7s | %7s\n" \
            "$algo" "$py" "$cm_i" "$cpp" "$rust" "$cm_n"
    done

    echo ""
    echo -e "${GREEN}Results saved to: $RESULT_FILE${NC}"
    echo ""

    # ボトルネック分析
    echo -e "${YELLOW}=== Performance Analysis ===${NC}"
    echo ""

    echo "JIT Performance (vs Python):"
    echo "  - Best case: Look for ratios > 1.0 (Cm faster)"
    echo "  - LLVM ORC JIT for fast startup"
    echo ""

    echo "Native Performance (vs C++/Rust):"
    echo "  - Target: 50-80% of C++/Rust speed"
    echo "  - LLVM optimizations: -O3 enabled"
    echo ""

    echo "Bottleneck Identification:"
    echo "  1. Prime Numbers: Tests basic arithmetic and loops"
    echo "  2. Fibonacci Recursive: Tests function call overhead"
    echo "  3. Fibonacci Iterative: Tests tight loop optimization"
    echo "  4. Array Sort: Tests memory access patterns"
    echo "  5. Matrix Multiply: Tests nested loops and cache usage"
}

# 実行
main "$@"
exit 0