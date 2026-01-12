# 行列計算パフォーマンス最適化提案

## 問題分析

### ベンチマーク結果（500×500行列）
- **Python**: 17.03秒
- **Cm(JIT)**: 17.26秒
- **Cm(Native)**: 14.55秒
- **C++**: 0.48秒（約30倍高速）
- **Rust**: 0.51秒

### 根本原因
1. **巨大なスタック使用**
   - 3つの500×500配列（各1MB）= 約3MBのスタック
   - アセンブリ: `subq $0x2de7d8, %rsp` (3MB確保)
   - C++は`vector`でヒープ使用

2. **キャッシュミス**
   - 行優先アクセスパターンの問題
   - メモリ局所性の欠如

3. **最適化の不足**
   - ループ展開なし
   - SIMD命令未使用
   - コンパイラ最適化が効いていない

## 最適化案

### 1. メモリ配置の改善

#### 現在の問題コード
```cm
void matrix_multiply(int n) {
    int[500][500] a;  // スタック上に3MB確保
    int[500][500] b;
    int[500][500] c;
    // ...
}
```

#### 改善案1: ヒープ割り当て
```cm
use libc {
    void* malloc(ulong size);
    void free(void* ptr);
}

void matrix_multiply_heap(int n) {
    // ヒープに確保
    void* mem_a = malloc(n * n * sizeof(int));
    int** a = mem_a as int**;

    void* mem_b = malloc(n * n * sizeof(int));
    int** b = mem_b as int**;

    void* mem_c = malloc(n * n * sizeof(int));
    int** c = mem_c as int**;

    // 処理...

    free(mem_a);
    free(mem_b);
    free(mem_c);
}
```

#### 改善案2: 1次元配列でアクセス
```cm
void matrix_multiply_flat(int n) {
    // 1次元配列として扱う
    int* a = malloc(n * n * sizeof(int)) as int*;
    int* b = malloc(n * n * sizeof(int)) as int*;
    int* c = malloc(n * n * sizeof(int)) as int*;

    // インデックス計算でアクセス
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            int sum = 0;
            for (int k = 0; k < n; k++) {
                sum += a[i * n + k] * b[k * n + j];
            }
            c[i * n + j] = sum;
        }
    }
}
```

### 2. キャッシュ最適化

#### ループの入れ替え（IJK → IKJ）
```cm
// キャッシュ効率の良いアクセスパターン
for (int i = 0; i < n; i++) {
    for (int k = 0; k < n; k++) {
        int a_ik = a[i * n + k];
        for (int j = 0; j < n; j++) {
            c[i * n + j] += a_ik * b[k * n + j];
        }
    }
}
```

### 3. ブロック化（タイリング）
```cm
void matrix_multiply_blocked(int n) {
    const int BLOCK_SIZE = 64;  // キャッシュラインに合わせる

    // ゼロクリア
    for (int i = 0; i < n * n; i++) {
        c[i] = 0;
    }

    // ブロック単位で処理
    for (int ii = 0; ii < n; ii += BLOCK_SIZE) {
        for (int kk = 0; kk < n; kk += BLOCK_SIZE) {
            for (int jj = 0; jj < n; jj += BLOCK_SIZE) {
                // ブロック内の計算
                for (int i = ii; i < min(ii + BLOCK_SIZE, n); i++) {
                    for (int k = kk; k < min(kk + BLOCK_SIZE, n); k++) {
                        int a_ik = a[i * n + k];
                        for (int j = jj; j < min(jj + BLOCK_SIZE, n); j++) {
                            c[i * n + j] += a_ik * b[k * n + j];
                        }
                    }
                }
            }
        }
    }
}
```

### 4. SIMD最適化（将来的な実装）
```cm
// SIMD intrinsics を使用した実装
#pragma clang loop vectorize(enable)
for (int j = 0; j < n; j += 4) {
    // 4要素同時処理
}
```

## 段階的実装計画

### Phase 1: 即座の改善（1週間）
1. **スタック→ヒープ変更**
   - 期待効果: 2-3倍高速化
   - 実装難易度: 低

2. **ループ順序の変更（IKJ）**
   - 期待効果: 2倍高速化
   - 実装難易度: 低

### Phase 2: キャッシュ最適化（2週間）
1. **ブロック化実装**
   - 期待効果: 3-5倍高速化
   - 実装難易度: 中

2. **プリフェッチ指示**
   - 期待効果: 1.5倍高速化
   - 実装難易度: 中

### Phase 3: コンパイラ最適化（1ヶ月）
1. **LLVM最適化パスの追加**
   - ループ展開
   - ベクトル化
   - 期待効果: 2-3倍高速化

2. **SIMD intrinsics サポート**
   - 期待効果: 4-8倍高速化
   - 実装難易度: 高

## 期待される結果

現状から段階的に改善:
1. Phase 1完了: 14.55秒 → 3-5秒
2. Phase 2完了: 3-5秒 → 1-2秒
3. Phase 3完了: 1-2秒 → 0.5秒（C++と同等）

## 実装例（即座の修正版）

```cm
// ベンチマーク5: 行列乗算（最適化版）
import std::io;

use libc {
    void* malloc(ulong size);
    void free(void* ptr);
}

void matrix_multiply(int n) {
    // ヒープ割り当て
    ulong size = n * n * sizeof(int);
    int* a = malloc(size) as int*;
    int* b = malloc(size) as int*;
    int* c = malloc(size) as int*;

    // 初期化
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            int idx = i * n + j;
            a[idx] = (i + j) % 100;
            b[idx] = (i - j + 100) % 100;
            c[idx] = 0;
        }
    }

    // 行列乗算（キャッシュ効率的なIKJ順）
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < n; k++) {
            int a_ik = a[i * n + k];
            for (int j = 0; j < n; j++) {
                c[i * n + j] += a_ik * b[k * n + j];
            }
        }
    }

    // 結果の一部を表示
    println("Result c[0][0] = {c[0]}");
    int last_idx = (n-1) * n + (n-1);
    println("Result c[n-1][n-1] = {c[last_idx]}");

    // メモリ解放
    free(a as void*);
    free(b as void*);
    free(c as void*);
}

int main() {
    int n = 500;
    println("Multiplying {n}x{n} matrices...");
    matrix_multiply(n);
    println("Matrix multiplication completed");
    return 0;
}
```

## コンパイラ側の改善案

### 1. 配列のメモリ配置判定
```cpp
// コンパイラで大きな配列を自動的にヒープへ
if (array_size > STACK_THRESHOLD) {
    allocate_on_heap();
} else {
    allocate_on_stack();
}
```

### 2. LLVM最適化フラグ
```cpp
// 行列計算用の最適化パス追加
pass_manager.add(createLoopUnrollPass());
pass_manager.add(createLoopVectorizePass());
pass_manager.add(createSLPVectorizerPass());
```

## まとめ

Cm言語の行列計算が遅い主な原因は：
1. **スタック上の巨大配列**（3MB）
2. **キャッシュ非効率なメモリアクセス**
3. **LLVM最適化の不足**

即座の対策としてヒープ割り当てとループ順序変更で5-10倍の高速化が期待できます。
長期的にはSIMD最適化とコンパイラ改善でC++と同等の性能を実現できます。