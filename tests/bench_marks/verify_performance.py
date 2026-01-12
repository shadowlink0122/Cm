#!/usr/bin/env python3
"""
パフォーマンス検証スクリプト
Cmの性能がC++/Rustより著しく劣っていないことを確認
"""

import os
import sys
import json
import re
import subprocess
from pathlib import Path
from typing import Dict, List, Tuple, Optional

class BenchmarkVerifier:
    """ベンチマーク結果の検証クラス"""

    def __init__(self, threshold: float = 2.0):
        """
        Args:
            threshold: 許容する性能差の倍率（デフォルト: 2.0倍まで許容）
        """
        self.threshold = threshold
        self.results: Dict[str, Dict[str, float]] = {}

    def parse_time_output(self, output: str) -> Optional[float]:
        """実行時間を出力から抽出"""
        patterns = [
            r'Time:\s*([\d.]+)\s*(?:s|seconds?)',
            r'Elapsed:\s*([\d.]+)\s*(?:s|seconds?)',
            r'実行時間:\s*([\d.]+)\s*(?:秒|s)',
            r'(\d+\.\d+)\s*(?:seconds?|s)\s*$',
            r'real\s+([\d.]+)s',  # time コマンドの出力
        ]

        for pattern in patterns:
            match = re.search(pattern, output, re.IGNORECASE | re.MULTILINE)
            if match:
                return float(match.group(1))
        return None

    def run_benchmark(self, executable: str, timeout: int = 30) -> Optional[float]:
        """ベンチマークを実行して時間を測定"""
        if not os.path.exists(executable):
            print(f"  ⚠️  {executable} not found")
            return None

        try:
            # time コマンドで測定（より正確）
            result = subprocess.run(
                ['time', '-p', executable],
                capture_output=True,
                text=True,
                timeout=timeout
            )

            # 標準エラー出力から時間を抽出（timeコマンドの出力）
            time_val = self.parse_time_output(result.stderr)
            if time_val:
                return time_val

            # プログラム自体の出力から時間を抽出
            time_val = self.parse_time_output(result.stdout)
            if time_val:
                return time_val

            print(f"  ⚠️  Could not parse time from {executable}")
            return None

        except subprocess.TimeoutExpired:
            print(f"  ❌ {executable} timed out after {timeout}s")
            return None
        except Exception as e:
            print(f"  ❌ Error running {executable}: {e}")
            return None

    def run_all_benchmarks(self):
        """すべてのベンチマークを実行"""
        benchmarks = [
            ("Prime Sieve (10000)", {
                "python": "python/01_prime_sieve.py",
                "cpp": "cpp/01_prime_sieve",
                "rust": "rust/01_prime_sieve/target/release/prime_sieve",
                "cm": "cm/01_prime_sieve"
            }),
            ("Fibonacci Memoized (40)", {
                "python": "python/02_fibonacci_memo.py",
                "cpp": "cpp/02_fibonacci_memo",
                "rust": "rust/02_fibonacci_memo/target/release/fibonacci_memo",
                "cm": "cm/02_fibonacci_memo"
            }),
            ("Array Sort (1000)", {
                "python": "python/03_array_sort.py",
                "cpp": "cpp/03_array_sort",
                "rust": "rust/03_array_sort/target/release/array_sort",
                "cm": "cm/03_array_sort"
            }),
            ("Matrix Multiply (500x500)", {
                "python": "python/04_matrix_multiply.py",
                "cpp": "cpp/04_matrix_multiply",
                "rust": "rust/04_matrix_multiply/target/release/matrix_multiply",
                "cm": "cm/04_matrix_multiply"
            }),
        ]

        for bench_name, executables in benchmarks:
            print(f"\n{'='*60}")
            print(f"Running: {bench_name}")
            print('='*60)

            bench_results = {}
            for lang, exe_path in executables.items():
                print(f"\n{lang.upper()}:")

                # Pythonスクリプトの場合
                if exe_path.endswith('.py'):
                    exe_path = f"python3 {exe_path}"
                    time_val = self.run_benchmark_python(exe_path)
                else:
                    time_val = self.run_benchmark(exe_path)

                if time_val is not None:
                    bench_results[lang] = time_val
                    print(f"  ✅ Time: {time_val:.4f}s")

            self.results[bench_name] = bench_results

    def run_benchmark_python(self, script_path: str, timeout: int = 30) -> Optional[float]:
        """Pythonベンチマークを実行"""
        try:
            result = subprocess.run(
                script_path.split(),
                capture_output=True,
                text=True,
                timeout=timeout
            )
            return self.parse_time_output(result.stdout)
        except Exception as e:
            print(f"  ❌ Error running Python script: {e}")
            return None

    def verify_performance(self) -> bool:
        """パフォーマンスを検証"""
        print("\n" + "="*60)
        print("PERFORMANCE VERIFICATION REPORT")
        print("="*60)

        all_passed = True

        for bench_name, times in self.results.items():
            print(f"\n{bench_name}:")

            if 'cm' not in times:
                print("  ⚠️  Cm benchmark not available")
                continue

            cm_time = times['cm']

            # C++/Rustとの比較
            native_times = []
            if 'cpp' in times:
                native_times.append(('C++', times['cpp']))
            if 'rust' in times:
                native_times.append(('Rust', times['rust']))

            if not native_times:
                print("  ⚠️  No C++/Rust results for comparison")
                continue

            # 最速のネイティブ実装と比較
            fastest_name, fastest_time = min(native_times, key=lambda x: x[1])
            ratio = cm_time / fastest_time if fastest_time > 0 else float('inf')

            status = "✅ PASS" if ratio <= self.threshold else "❌ FAIL"
            all_passed = all_passed and (ratio <= self.threshold)

            print(f"  Cm: {cm_time:.4f}s")
            print(f"  {fastest_name}: {fastest_time:.4f}s")
            print(f"  Ratio: {ratio:.2f}x")
            print(f"  Status: {status}")

            if ratio > self.threshold:
                print(f"  ⚠️  Warning: Cm is {ratio:.1f}x slower than {fastest_name}")
                print(f"     (Threshold: {self.threshold}x)")

        return all_passed

    def save_results(self, output_file: str = "results/benchmark_results.json"):
        """結果をJSONファイルに保存"""
        os.makedirs(os.path.dirname(output_file), exist_ok=True)
        with open(output_file, 'w') as f:
            json.dump(self.results, f, indent=2)
        print(f"\nResults saved to {output_file}")

    def generate_markdown_report(self, output_file: str = "results/benchmark_report.md"):
        """Markdownレポートを生成"""
        os.makedirs(os.path.dirname(output_file), exist_ok=True)

        with open(output_file, 'w') as f:
            f.write("# Benchmark Performance Report\n\n")
            f.write("## Summary\n\n")

            # テーブルヘッダ
            f.write("| Benchmark | Cm | C++ | Rust | Python | Cm/Native |\n")
            f.write("|-----------|-----|-----|------|--------|----------|\n")

            for bench_name, times in self.results.items():
                cm_time = times.get('cm', 0)
                cpp_time = times.get('cpp', 0)
                rust_time = times.get('rust', 0)
                py_time = times.get('python', 0)

                # ネイティブ最速との比較
                native_min = min(x for x in [cpp_time, rust_time] if x > 0) if any([cpp_time, rust_time]) else 0
                ratio = f"{cm_time/native_min:.2f}x" if native_min > 0 and cm_time > 0 else "N/A"

                # 時間フォーマット
                cm_str = f"{cm_time:.4f}s" if cm_time > 0 else "N/A"
                cpp_str = f"{cpp_time:.4f}s" if cpp_time > 0 else "N/A"
                rust_str = f"{rust_time:.4f}s" if rust_time > 0 else "N/A"
                py_str = f"{py_time:.4f}s" if py_time > 0 else "N/A"

                f.write(f"| {bench_name} | {cm_str} | {cpp_str} | {rust_str} | {py_str} | {ratio} |\n")

        print(f"Markdown report saved to {output_file}")


def main():
    """メイン処理"""
    print("🚀 Cm Language Performance Verification")
    print("="*60)

    # ベンチマークディレクトリの確認
    if not os.path.exists("cm"):
        print("❌ Error: Run this script from tests/bench_marks/ directory")
        sys.exit(1)

    verifier = BenchmarkVerifier(threshold=2.0)

    # すべてのベンチマークを実行
    verifier.run_all_benchmarks()

    # パフォーマンスを検証
    passed = verifier.verify_performance()

    # 結果を保存
    verifier.save_results()
    verifier.generate_markdown_report()

    # 終了ステータス
    if passed:
        print("\n✅ All performance checks PASSED")
        print("Cm performance is competitive with C++ and Rust!")
        sys.exit(0)
    else:
        print("\n❌ Performance checks FAILED")
        print("Cm is significantly slower than C++/Rust in some benchmarks")
        sys.exit(1)


if __name__ == "__main__":
    main()