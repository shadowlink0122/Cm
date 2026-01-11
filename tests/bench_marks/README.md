# Cm言語ベンチマークスイート

## 概要

このディレクトリには、Cm言語の実行速度を他の言語と比較するためのベンチマークプログラムが含まれています。各ベンチマークは個別ファイルに分けられており、ボトルネックの特定が容易です。

### 比較対象

- **インタプリタ性能**: Cm インタプリタ vs Python
- **ネイティブ性能**: Cm (LLVM) vs C++ vs Rust

## ベンチマーク内容

全ての言語で同じアルゴリズムを実装しています：

1. **素数計算** (`01_prime`): 100,000までの素数をカウント
   - テスト内容：基本的な算術演算とループ性能

2. **フィボナッチ（再帰）** (`02_fibonacci_recursive`): 35番目のフィボナッチ数を計算
   - テスト内容：関数呼び出しのオーバーヘッド

3. **フィボナッチ（反復）** (`03_fibonacci_iterative`): 1,000,000番目のフィボナッチ数を計算
   - テスト内容：タイトループの最適化

4. **配列ソート** (`04_array_sort`): 1,000要素のバブルソート
   - テスト内容：メモリアクセスパターンと配列操作

5. **行列乗算** (`05_matrix_multiply`): 500×500行列の乗算
   - テスト内容：ネストループとキャッシュ使用効率

## ディレクトリ構造

```
bench_marks/
├── cm/           # Cm言語のベンチマーク
├── python/       # Pythonのベンチマーク
├── cpp/          # C++のベンチマーク
├── rust/         # Rustのベンチマーク
├── results/      # 実行結果の保存先
└── run_benchmarks.sh  # 自動実行スクリプト
```

## 実行方法

### 個別ベンチマークの自動実行（推奨）

```bash
cd tests/bench_marks
chmod +x run_individual_benchmarks.sh
./run_individual_benchmarks.sh
```

これにより：
- 各アルゴリズムを個別に測定
- ボトルネックの特定が容易
- 結果を表形式で比較
- CSVファイルに結果を保存

### 個別実行

#### Python
```bash
cd python
python3 benchmark.py
```

#### C++
```bash
cd cpp
make
./benchmark
```

#### Rust
```bash
cd rust
cargo build --release
./target/release/benchmark
```

#### Cm (インタプリタ)
```bash
cd cm
../../build/bin/cm -i benchmark.cm
```

#### Cm (ネイティブ/LLVM)
```bash
cd cm
../../build/bin/cm -O3 benchmark.cm -o benchmark_native
./benchmark_native
```

## 必要な環境

- Python 3.x
- C++ コンパイラ (clang++ または g++)
- Rust/Cargo
- Cm コンパイラ（ビルド済み）

## 結果の見方

`run_benchmarks.sh`を実行すると、以下の情報が表示されます：

1. 各言語の実行時間
2. 各ベンチマークの詳細結果
3. 言語間の速度比較（何倍速いか）

結果は`results/benchmark_results_YYYYMMDD_HHMMSS.txt`に保存されます。

## パフォーマンスチューニング

### 最適化オプション

- **C++**: `-O3 -march=native` (最大最適化)
- **Rust**: `--release` with `opt-level = 3, lto = true`
- **Cm**: `-O3` (LLVM最適化レベル3)
- **Python**: 最適化なし（インタプリタのデフォルト）

### 注意事項

1. **公平性**: 全ての言語で同じアルゴリズムを使用していますが、言語固有の最適化は行っていません
2. **現実的な測定**: マイクロベンチマークではなく、実際のアプリケーションに近い処理を測定
3. **再現性**: 同じマシンで複数回実行して平均を取ることを推奨

## 期待される結果

### インタプリタ性能
- Pythonは成熟したインタプリタなので、初期段階のCmインタプリタより速い可能性があります
- これは正常であり、Cmインタプリタの最適化の余地を示しています

### ネイティブ性能
- C++とRustは高度に最適化されたコンパイラを持つため、非常に高速です
- Cm（LLVM）はLLVMの最適化を利用するため、競争力のある性能が期待できます
- 初期実装では、C++/Rustの50-80%程度の性能が妥当な目標です

## トラブルシューティング

### Cmコンパイラが見つからない
```bash
cd ../..
cmake -B build -DCM_USE_LLVM=ON
cmake --build build
```

### Rustがインストールされていない
```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
```

### C++コンパイラがない（macOS）
```bash
xcode-select --install
```

## 今後の拡張

- [ ] メモリ使用量の測定
- [ ] より多様なアルゴリズムの追加
- [ ] グラフによる可視化
- [ ] JITコンパイル性能の測定
- [ ] WebAssembly版の追加

---

**作成日**: 2026-01-11
**作成者**: Claude Code