# 007: 配列フラット化による最適化設計（ベアメタル対応）

## 1. 問題の本質と解決方針

### 1.1 スタック配列の問題
```assembly
; 現在の多次元配列（スタック上）
; int[500][500] matrix として
sub rsp, 1000000    ; スタックを1MB拡張
; アクセス時：
mov eax, [rsp + 999996]  ; スタックの深い位置への参照
; → スタックポインタからの大きなオフセットがペナルティ
```

### 1.2 フラット化による解決
```assembly
; フラット化された配列（スタック上だが連続アクセス）
; int matrix[250000] として扱う
lea rax, [rsp + offset]   ; ベースアドレス取得
mov ebx, [rax + rcx*4]     ; 連続メモリアクセス
; → キャッシュ効率が大幅に向上
```

## 2. 配列フラット化の設計

### 2.1 多次元配列の内部表現変換
```cpp
// Cm言語での宣言
int[500][500] matrix;

// 現在の内部表現（非効率）
// LLVMでは [500 x [500 x i32]] 型
// → ネストした配列型

// 最適化後の内部表現（効率的）
// LLVMでは [250000 x i32] 型
// → フラットな1次元配列

// アクセス変換
// matrix[i][j] → matrix_flat[i * 500 + j]
```

### 2.2 型システムの拡張
```cpp
// src/frontend/ast/types.hpp
struct Type {
    // 既存フィールド
    TypeKind kind;
    TypePtr element_type;
    std::optional<uint32_t> array_size;

    // 多次元配列用の新規フィールド
    std::vector<uint32_t> dimensions;      // 各次元のサイズ
    bool is_flattened = false;             // フラット化済みフラグ
    uint32_t total_elements = 0;           // 全要素数（事前計算）

    // フラット化が有効かの判定
    bool should_flatten() const {
        // 2次元以上の配列をフラット化
        return dimensions.size() >= 2;
    }

    // フラットインデックスの計算
    uint32_t getFlattenedSize() const {
        if (dimensions.empty()) return array_size.value_or(0);

        uint32_t size = 1;
        for (auto dim : dimensions) {
            size *= dim;
        }
        return size;
    }
};
```

## 3. MIR生成時の最適化

### 3.1 配列宣言の変換
```cpp
// src/mir/lowering/lowering.cpp

MirLocal lowerArrayDeclaration(const HIRArrayDecl* decl) {
    auto type = decl->type;

    if (type->should_flatten()) {
        // 多次元配列をフラット化
        auto flatSize = type->getFlattenedSize();
        auto flatType = make_array(type->element_type, flatSize);
        flatType->is_flattened = true;

        MirLocal local;
        local.type = flatType;
        local.name = decl->name + "_flat";
        local.metadata = ArrayMetadata{
            .original_dims = type->dimensions,
            .is_flattened = true
        };

        return local;
    }

    // 1次元配列はそのまま
    return defaultLowerDecl(decl);
}
```

### 3.2 配列アクセスの変換
```cpp
// src/mir/lowering/lowering.cpp

MirPlace lowerArrayAccess(const HIRArrayAccess* access) {
    auto base = access->array;
    auto indices = access->indices;

    if (isFlattened(base)) {
        // 多次元インデックスをフラットインデックスに変換
        // A[i][j][k] → A_flat[((i * DIM_J) + j) * DIM_K + k]

        auto dims = getArrayDimensions(base);
        MirOperand flatIndex = indices[0];

        for (size_t i = 1; i < indices.size(); i++) {
            flatIndex = MirBinaryOp{
                .op = BinOp::Mul,
                .lhs = flatIndex,
                .rhs = MirConstant{dims[i]}
            };
            flatIndex = MirBinaryOp{
                .op = BinOp::Add,
                .lhs = flatIndex,
                .rhs = indices[i]
            };
        }

        return MirPlace{
            .local = base,
            .projections = {
                Projection::Index(flatIndex)
            }
        };
    }

    return defaultLowerAccess(access);
}
```

## 4. LLVM IR生成の最適化

### 4.1 配列アロケーション
```cpp
// src/codegen/llvm/core/mir_to_llvm.cpp

llvm::AllocaInst* allocateFlattenedArray(const MirLocal& local) {
    auto type = local.type;
    llvm::Type* llvmType;

    if (type->is_flattened) {
        // フラット配列として確保
        auto elemType = convertType(type->element_type);
        auto arraySize = type->getFlattenedSize();
        llvmType = llvm::ArrayType::get(elemType, arraySize);

        // アライメントを設定（キャッシュライン境界）
        auto alloca = builder->CreateAlloca(llvmType, nullptr, local.name);
        alloca->setAlignment(llvm::Align(64));  // 64バイトアライメント

        // メタデータ付与（デバッグ情報用）
        llvm::MDNode* metadata = llvm::MDNode::get(ctx, {
            llvm::MDString::get(ctx, "flattened_array"),
            llvm::ConstantAsMetadata::get(
                llvm::ConstantInt::get(ctx.getI32Type(), type->dimensions.size())
            )
        });
        alloca->setMetadata("cm.array.flattened", metadata);

        return alloca;
    }

    // 通常の配列
    return builder->CreateAlloca(convertType(type), nullptr, local.name);
}
```

### 4.2 最適化されたメモリアクセス
```cpp
// src/codegen/llvm/core/mir_to_llvm.cpp

llvm::Value* generateFlattenedAccess(llvm::Value* base,
                                     const std::vector<llvm::Value*>& indices,
                                     const ArrayMetadata& metadata) {
    // ベースアドレスの取得
    auto basePtr = builder->CreateBitCast(base,
        llvm::PointerType::get(ctx.getI32Type(), 0));

    // フラットインデックスは既にMIRで計算済み
    auto flatIndex = indices[0];

    // GEP命令でアクセス
    auto gep = builder->CreateGEP(ctx.getI32Type(), basePtr, flatIndex);

    // プリフェッチヒント（次の行をプリフェッチ）
    if (metadata.original_dims.size() >= 2) {
        auto nextRow = builder->CreateAdd(flatIndex,
            llvm::ConstantInt::get(flatIndex->getType(), metadata.original_dims[1]));
        auto prefetchPtr = builder->CreateGEP(ctx.getI32Type(), basePtr, nextRow);

        // LLVM組み込み関数でプリフェッチ
        auto prefetchFunc = llvm::Intrinsic::getDeclaration(
            module, llvm::Intrinsic::prefetch, {prefetchPtr->getType()});
        builder->CreateCall(prefetchFunc, {
            prefetchPtr,
            llvm::ConstantInt::get(ctx.getI32Type(), 0),  // 読み込み
            llvm::ConstantInt::get(ctx.getI32Type(), 3),  // 時間的局所性高
            llvm::ConstantInt::get(ctx.getI32Type(), 1)   // データ
        });
    }

    return gep;
}
```

## 5. ループ最適化

### 5.1 ループ交換（Loop Interchange）
```cpp
// MIRパスでループ順序を最適化
class LoopInterchangePass : public MirPass {
    void optimizeNestedLoops(MirFunction& func) {
        // 行列アクセスパターンの検出
        // A[i][j] のアクセスがある場合、外側ループをiにする

        for (auto& block : func.basic_blocks) {
            if (auto loop = detectNestedLoop(block)) {
                if (shouldInterchange(loop)) {
                    // ループ順序の入れ替え
                    interchangeLoops(loop);
                }
            }
        }
    }
};
```

### 5.2 ループアンローリング
```cpp
// LLVM最適化パスでのアンローリング設定
void setLoopUnrollHints(llvm::Loop* loop) {
    // メタデータでアンローリング係数を指定
    llvm::MDNode* unrollMD = llvm::MDNode::get(ctx, {
        llvm::MDString::get(ctx, "llvm.loop.unroll.count"),
        llvm::ConstantAsMetadata::get(
            llvm::ConstantInt::get(ctx.getI32Type(), 4))  // 4回アンローリング
    });

    loop->setLoopID(unrollMD);
}
```

## 6. キャッシュ最適化テクニック

### 6.1 ブロッキング（タイリング）
```cm
// Cm言語での最適化されたコード生成
void matrix_multiply_optimized(int[N][N] A, int[N][N] B, int[N][N] C) {
    const int BLOCK = 64;  // キャッシュブロックサイズ

    // コンパイラが自動的に以下のようなコードに変換
    for (int ii = 0; ii < N; ii += BLOCK) {
        for (int jj = 0; jj < N; jj += BLOCK) {
            for (int kk = 0; kk < N; kk += BLOCK) {
                // ブロック内の計算
                for (int i = ii; i < min(ii + BLOCK, N); i++) {
                    for (int k = kk; k < min(kk + BLOCK, N); k++) {
                        int a_ik = A[i][k];  // レジスタに保持
                        for (int j = jj; j < min(jj + BLOCK, N); j++) {
                            C[i][j] += a_ik * B[k][j];
                        }
                    }
                }
            }
        }
    }
}
```

### 6.2 SIMD最適化のためのアライメント
```llvm
; フラット化された配列のSIMD最適化
define void @vector_add([1000 x float]* %a, [1000 x float]* %b, [1000 x float]* %c) {
entry:
    ; ベクトル化ループ
    br label %vector.body

vector.body:
    %index = phi i64 [0, %entry], [%index.next, %vector.body]

    ; 4要素同時ロード（AVX）
    %a.vec = load <4 x float>, <4 x float>* %a.ptr, align 16
    %b.vec = load <4 x float>, <4 x float>* %b.ptr, align 16

    ; ベクトル演算
    %c.vec = fadd <4 x float> %a.vec, %b.vec

    ; 4要素同時ストア
    store <4 x float> %c.vec, <4 x float>* %c.ptr, align 16

    %index.next = add i64 %index, 4
    %cond = icmp ult i64 %index.next, 1000
    br i1 %cond, label %vector.body, label %exit
}
```

## 7. ベンチマーク目標

| 操作 | 現在（ネスト配列） | フラット化後 | 改善率 |
|-----|-----------------|------------|--------|
| 500×500配列宣言 | 1000μs | 100μs | 10x |
| 要素アクセス | 10ns | 2ns | 5x |
| 行列乗算（IKJ） | 20,530ms | 100ms | 200x |
| キャッシュミス率 | 40% | 5% | 8x |

## 8. 実装優先順位

### Phase 1: フラット化基盤（最優先）
- [ ] 多次元配列の検出
- [ ] フラット化変換
- [ ] インデックス計算

### Phase 2: メモリアクセス最適化
- [ ] アライメント設定
- [ ] プリフェッチング
- [ ] 連続アクセス最適化

### Phase 3: ループ最適化
- [ ] ループ交換
- [ ] アンローリング
- [ ] ブロッキング

## 9. ベアメタル環境での利点

1. **mallocフリー**: スタック上で完結
2. **決定的なメモリレイアウト**: 実行時のばらつきなし
3. **キャッシュ予測可能**: 静的に最適化可能
4. **メモリフットプリント削減**: メタデータ不要

この設計により、ベアメタル環境でも高速な配列処理を実現できます。