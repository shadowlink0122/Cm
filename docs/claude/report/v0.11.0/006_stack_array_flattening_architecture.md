# 006: スタックベース配列フラット化アーキテクチャ

## 1. 設計原則

### 1.1 完全スタックベース
- **ヒープ不要**: malloc/freeを一切使用しない
- **ベアメタル対応**: OSなしで動作可能
- **決定的動作**: メモリ配置がコンパイル時に確定

### 1.2 フラット化による高速化
```assembly
; 従来: 多次元配列 int[500][500]（非効率）
; スタック上にネストした構造
sub rsp, 1000000         ; 巨大スタック確保
mov eax, [rsp + 999996]  ; 深いオフセット → キャッシュミス

; 最適化: フラット配列 int[250000]（効率的）
; 連続メモリとして配置
lea rax, [rsp]           ; ベースアドレス
mov eax, [rax + rcx*4]   ; 連続アクセス → キャッシュヒット
```

## 2. N次元配列の統一表現

### 2.1 内部表現の統一
```cpp
// Cm言語での宣言（任意次元）
int[10] array1d;           // 1次元
int[100][100] array2d;     // 2次元
int[10][20][30] array3d;   // 3次元
int[5][5][5][5] array4d;   // 4次元

// すべて1次元配列として内部表現
// array1d -> int[10]
// array2d -> int[10000]
// array3d -> int[6000]
// array4d -> int[625]
```

### 2.2 インデックス計算の一般化
```cpp
// N次元配列のフラットインデックス計算
// dims = [D0, D1, ..., Dn-1]
// indices = [i0, i1, ..., in-1]

size_t flatIndex(const vector<size_t>& dims,
                 const vector<size_t>& indices) {
    size_t flat = 0;
    size_t stride = 1;

    // 行優先順序（C言語スタイル）
    for (int k = dims.size() - 1; k >= 0; k--) {
        flat += indices[k] * stride;
        stride *= dims[k];
    }
    return flat;
}

// 例: array[2][3][4] のインデックス計算
// flat = i * (3*4) + j * 4 + k
```

## 3. メモリレイアウト最適化

### 3.1 アライメント戦略
```cpp
// サイズ別の最適アライメント
template<size_t SIZE>
constexpr size_t getOptimalAlignment() {
    if (SIZE >= 4096) return 64;  // ページ境界
    if (SIZE >= 256)  return 64;  // キャッシュライン
    if (SIZE >= 64)   return 32;  // AVX
    if (SIZE >= 32)   return 16;  // SSE
    return 8;                      // デフォルト
}

// スタック配置時のアライメント指定
alignas(getOptimalAlignment<SIZE>()) T array[SIZE];
```

### 3.2 キャッシュ最適化配置
```cpp
// 複数配列の配置順序最適化
struct MatrixWorkspace {
    // 頻繁にアクセスする配列を近くに配置
    alignas(64) int A[500*500];  // 入力行列A
    alignas(64) int B[500*500];  // 入力行列B
    alignas(64) int C[500*500];  // 出力行列C

    // 作業用配列（キャッシュブロック用）
    alignas(64) int block[64*64];
};
```

## 4. コンパイル時最適化

### 4.1 定数畳み込み
```cpp
// コンパイル時にインデックス計算を解決
template<size_t... Dims>
struct StaticArray {
    static constexpr size_t total_size = (Dims * ...);
    int data[total_size];

    template<size_t... Indices>
    constexpr int& get() {
        constexpr size_t idx = calculateIndex<Dims...>(Indices...);
        return data[idx];
    }
};

// 使用例: array.get<2,3,4>() はコンパイル時に解決
```

### 4.2 ループ展開とベクトル化
```llvm
; コンパイラが生成する最適化されたLLVM IR
; ループ展開 + SIMD化

vector.body:
    ; 8要素同時処理（AVX2）
    %vec.a = load <8 x i32>, <8 x i32>* %ptr.a, align 32
    %vec.b = load <8 x i32>, <8 x i32>* %ptr.b, align 32
    %vec.c = add <8 x i32> %vec.a, %vec.b
    store <8 x i32> %vec.c, <8 x i32>* %ptr.c, align 32

    %index.next = add i64 %index, 8
    %cmp = icmp ult i64 %index.next, %size
    br i1 %cmp, label %vector.body, label %exit

!llvm.loop.vectorize.width !{i32 8}
!llvm.loop.unroll.count !{i32 4}
```

## 5. アクセスパターン最適化

### 5.1 行優先 vs 列優先の自動判定
```cpp
// MIRパスでアクセスパターンを分析
enum class AccessPattern {
    RowMajor,     // C言語スタイル（デフォルト）
    ColumnMajor,  // Fortranスタイル
    Diagonal,     // 対角優先
    BlockWise     // ブロック単位
};

AccessPattern analyzePattern(const LoopNest& loops) {
    // 最内ループがどの次元を走査しているか分析
    // 必要に応じてループ交換を提案
}
```

### 5.2 プリフェッチング戦略
```cpp
// 次の行/ブロックを先読み
void prefetchNextBlock(void* current, size_t stride) {
    __builtin_prefetch(current + stride, 0, 3);     // L1キャッシュ
    __builtin_prefetch(current + stride * 2, 0, 2); // L2キャッシュ
}
```

## 6. パフォーマンス特性

### 6.1 メモリアクセス特性
| 配列サイズ | アライメント | キャッシュミス率 | 実効帯域幅 |
|----------|------------|--------------|----------|
| ≤32KB    | 16B        | <1%          | 100GB/s  |
| ≤256KB   | 64B        | <5%          | 80GB/s   |
| ≤2MB     | 64B        | <10%         | 40GB/s   |
| >2MB     | 4KB        | 20-30%       | 20GB/s   |

### 6.2 期待される改善
```
現在の実装（ネスト配列）:
- 500×500行列乗算: 20.53秒
- キャッシュミス: 40%
- TLBミス: 多発

フラット化後（スタックのみ）:
- 500×500行列乗算: 0.08秒 (250倍高速化)
- キャッシュミス: <5%
- TLBミス: ほぼゼロ
```

## 7. 実装上の制約と対策

### 7.1 スタックサイズ制限
```bash
# デフォルトスタックサイズ
ulimit -s          # 通常8MB

# 大規模配列用に拡張
ulimit -s unlimited

# または実行時指定
./program --stack-size=100M
```

### 7.2 コンパイラ最適化フラグ
```bash
# フラット化を有効にするフラグ
cm compile --flatten-arrays    # 配列フラット化
cm compile --align-arrays      # 最適アライメント
cm compile --vectorize         # SIMD最適化
```

## 8. ベアメタル環境での利点

1. **予測可能性**: メモリ使用量が静的に決定
2. **効率性**: メタデータ不要、純粋なデータのみ
3. **移植性**: OS依存なし、どこでも動作
4. **リアルタイム性**: 動的割り当てなし、遅延なし

この設計により、組み込みからスーパーコンピュータまで、同じコードで最高性能を実現します。