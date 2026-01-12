# 004: 配列フラット化実装計画

## 1. 実装ロードマップ

### Week 1: 基盤実装
- Day 1-2: 型システムの拡張
- Day 3-4: HIR→MIR変換の修正
- Day 5: LLVM IR生成の基本実装
- Day 6-7: テストとデバッグ

### Week 2: 最適化実装
- Day 1-2: メモリアライメント最適化
- Day 3-4: ループ最適化パス
- Day 5-6: SIMD対応
- Day 7: ベンチマーク測定

## 2. 具体的な実装変更

### 2.1 AST/HIR型の拡張
```cpp
// src/frontend/ast/types.hpp
struct Type {
    // 既存のコードに追加
    struct ArrayDimensions {
        std::vector<uint32_t> sizes;     // 各次元のサイズ
        uint32_t total_elements = 0;      // 全要素数
        bool row_major = true;            // 行優先（C言語スタイル）

        uint32_t calculate_offset(const std::vector<uint32_t>& indices) const {
            uint32_t offset = 0;
            uint32_t stride = 1;

            // 行優先順序でオフセット計算
            for (int i = sizes.size() - 1; i >= 0; i--) {
                offset += indices[i] * stride;
                stride *= sizes[i];
            }
            return offset;
        }
    };

    std::optional<ArrayDimensions> array_dims;

    // 多次元配列の作成ヘルパー
    static TypePtr make_multidim_array(TypePtr elem_type,
                                       std::vector<uint32_t> dimensions) {
        auto t = std::make_shared<Type>(TypeKind::Array);
        t->element_type = elem_type;

        ArrayDimensions dims;
        dims.sizes = dimensions;
        dims.total_elements = 1;
        for (auto d : dimensions) {
            dims.total_elements *= d;
        }

        t->array_dims = dims;
        t->array_size = dims.total_elements;  // フラット化されたサイズ
        return t;
    }
};
```

### 2.2 HIR Lowering の修正
```cpp
// src/hir/lowering/array_lowering.cpp

class ArrayLowering {
    mir::MirLocal lowerArrayVariable(const hir::VarDecl& decl) {
        if (!decl.type->array_dims.has_value()) {
            // 1次元配列はそのまま
            return defaultLower(decl);
        }

        // 多次元配列をフラット化
        mir::MirLocal local;
        local.name = decl.name;
        local.type = flattenArrayType(decl.type);
        local.is_flattened = true;

        // メタデータを保存（デバッグ用）
        local.debug_info = ArrayDebugInfo{
            .original_dims = decl.type->array_dims->sizes,
            .flattened_size = decl.type->array_dims->total_elements
        };

        return local;
    }

    mir::MirPlace lowerArrayAccess(const hir::ArrayAccess& access) {
        auto base = access.base;
        auto indices = access.indices;

        if (isFlattenedArray(base)) {
            // 多次元インデックスを1次元に変換
            auto flatIndex = calculateFlatIndex(base, indices);

            return mir::MirPlace{
                .local = getLocalId(base),
                .projections = {
                    mir::Projection{
                        .kind = mir::ProjectionKind::Index,
                        .index_operand = flatIndex
                    }
                }
            };
        }

        return defaultLowerAccess(access);
    }

private:
    mir::MirOperand calculateFlatIndex(const hir::Expr& base,
                                       const std::vector<hir::Expr>& indices) {
        auto dims = getArrayDimensions(base);

        // 例: A[i][j][k] -> ((i * DIM_J) + j) * DIM_K + k
        mir::MirOperand result = lowerExpr(indices[0]);

        for (size_t i = 1; i < indices.size(); i++) {
            // result = result * dims[i]
            result = mir::MirBinaryOp{
                .op = mir::BinOp::Mul,
                .lhs = result,
                .rhs = mir::MirConstant{
                    .value = dims.sizes[i],
                    .type = make_int()
                }
            };

            // result = result + indices[i]
            result = mir::MirBinaryOp{
                .op = mir::BinOp::Add,
                .lhs = result,
                .rhs = lowerExpr(indices[i])
            };
        }

        return result;
    }
};
```

### 2.3 LLVM コード生成の修正
```cpp
// src/codegen/llvm/core/array_codegen.cpp

class ArrayCodeGen {
    llvm::Value* generateArrayAllocation(const mir::MirLocal& local) {
        if (!local.is_flattened) {
            return generateNormalArray(local);
        }

        // フラット化された配列の生成
        auto elemType = convertType(local.type->element_type);
        auto totalSize = local.type->array_size.value();

        // スタック上にフラット配列を確保
        auto arrayType = llvm::ArrayType::get(elemType, totalSize);
        auto alloca = builder->CreateAlloca(arrayType, nullptr, local.name);

        // キャッシュライン境界にアライメント（64バイト）
        alloca->setAlignment(llvm::Align(64));

        // デバッグ情報の付与
        attachArrayMetadata(alloca, local.debug_info);

        return alloca;
    }

    llvm::Value* generateArrayAccess(llvm::Value* base,
                                     const mir::MirPlace& place) {
        // フラット化された配列へのアクセス
        if (place.projections.size() == 1 &&
            place.projections[0].kind == mir::ProjectionKind::Index) {

            auto index = convertOperand(place.projections[0].index_operand);
            auto elemType = getElementType(base);

            // GEP でアクセス（既にフラットインデックス）
            auto zero = llvm::ConstantInt::get(ctx.getI32Type(), 0);
            std::vector<llvm::Value*> indices = {zero, index};

            auto gep = builder->CreateGEP(base->getType()->getPointerElementType(),
                                         base, indices, "array_elem");

            // 連続アクセスの場合、プリフェッチ挿入
            insertPrefetchHint(base, index);

            return gep;
        }

        return generateNormalAccess(base, place);
    }

private:
    void insertPrefetchHint(llvm::Value* base, llvm::Value* currentIndex) {
        // 次の要素をプリフェッチ
        auto nextIndex = builder->CreateAdd(currentIndex,
                                           llvm::ConstantInt::get(currentIndex->getType(), 1));

        auto zero = llvm::ConstantInt::get(ctx.getI32Type(), 0);
        std::vector<llvm::Value*> indices = {zero, nextIndex};

        auto prefetchAddr = builder->CreateGEP(
            base->getType()->getPointerElementType(),
            base, indices, "prefetch_addr");

        // LLVM組み込みプリフェッチ関数
        auto prefetchFunc = llvm::Intrinsic::getDeclaration(
            module, llvm::Intrinsic::prefetch);

        builder->CreateCall(prefetchFunc, {
            prefetchAddr,
            llvm::ConstantInt::get(ctx.getI32Type(), 0),  // read
            llvm::ConstantInt::get(ctx.getI32Type(), 3),  // high temporal locality
            llvm::ConstantInt::get(ctx.getI32Type(), 1)   // data cache
        });
    }
};
```

### 2.4 最適化パスの追加
```cpp
// src/mir/passes/array_optimization_pass.cpp

class ArrayOptimizationPass : public MirPass {
public:
    void run(mir::MirProgram& program) override {
        for (auto& func : program.functions) {
            optimizeFunction(func);
        }
    }

private:
    void optimizeFunction(mir::MirFunction& func) {
        // 1. ループ内の配列アクセスパターンを分析
        analyzeAccessPatterns(func);

        // 2. ループ順序の最適化（行優先アクセスになるように）
        optimizeLoopOrder(func);

        // 3. ループアンローリングの適用
        applyLoopUnrolling(func);

        // 4. ベクトル化可能なループをマーク
        markVectorizableLoops(func);
    }

    void analyzeAccessPatterns(mir::MirFunction& func) {
        for (auto& block : func.basic_blocks) {
            std::vector<ArrayAccess> accesses;

            for (auto& stmt : block->statements) {
                if (auto access = detectArrayAccess(stmt)) {
                    accesses.push_back(access);
                }
            }

            // 連続アクセスパターンの検出
            if (isSequentialAccess(accesses)) {
                block->metadata.is_vectorizable = true;
            }
        }
    }

    void optimizeLoopOrder(mir::MirFunction& func) {
        // ネストループの検出
        for (auto& loop : detectNestedLoops(func)) {
            // 内側ループが列方向、外側が行方向の場合は交換
            if (shouldInterchange(loop)) {
                interchangeLoops(loop);
            }
        }
    }
};
```

## 3. テストケース

### 3.1 単体テスト
```cm
// tests/flattened_array_test.cm

void test_2d_array_flattening() {
    int[100][100] matrix;

    // フラット化されているか確認
    assert(__builtin_array_is_flattened(&matrix));

    // アクセステスト
    for (int i = 0; i < 100; i++) {
        for (int j = 0; j < 100; j++) {
            matrix[i][j] = i * 100 + j;
        }
    }

    // 値の確認
    for (int i = 0; i < 100; i++) {
        for (int j = 0; j < 100; j++) {
            assert(matrix[i][j] == i * 100 + j);
        }
    }
}

void test_3d_array_flattening() {
    int[10][20][30] cube;

    // 3次元配列のフラット化
    assert(__builtin_array_is_flattened(&cube));

    // パフォーマンステスト
    auto start = __builtin_rdtsc();
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 20; j++) {
            for (int k = 0; k < 30; k++) {
                cube[i][j][k] = i + j + k;
            }
        }
    }
    auto end = __builtin_rdtsc();

    // サイクル数をチェック（期待値以下）
    assert((end - start) < 100000);
}
```

### 3.2 パフォーマンステスト
```cm
// tests/bench_marks/flattened_matrix_bench.cm

void benchmark_matrix_multiply() {
    int[500][500] a, b, c;

    // 初期化
    for (int i = 0; i < 500; i++) {
        for (int j = 0; j < 500; j++) {
            a[i][j] = i + j;
            b[i][j] = i - j;
            c[i][j] = 0;
        }
    }

    // 行列乗算（IKJ順）
    auto start = clock();
    for (int i = 0; i < 500; i++) {
        for (int k = 0; k < 500; k++) {
            int a_ik = a[i][k];
            for (int j = 0; j < 500; j++) {
                c[i][j] += a_ik * b[k][j];
            }
        }
    }
    auto end = clock();

    // 100ms以下であることを確認
    double elapsed = (end - start) / CLOCKS_PER_SEC * 1000;
    assert(elapsed < 100.0);
    println("Matrix multiply time: {elapsed}ms");
}
```

## 4. 期待される成果

### 4.1 パフォーマンス向上
```
現在の実装:
- 500×500行列乗算: 20.53秒
- メモリ使用: スタック深さ1MB
- キャッシュミス率: 高

フラット化後:
- 500×500行列乗算: 0.1秒以下（200倍高速化）
- メモリ使用: 連続領域1MB
- キャッシュミス率: 5%以下
```

### 4.2 コード互換性
```cm
// ユーザーコードは変更不要
int[500][500] matrix;  // 宣言は同じ
matrix[i][j] = value;  // アクセスも同じ

// コンパイラが内部で自動的にフラット化
```

## 5. リスクと対策

### リスク1: デバッグ情報の喪失
**対策**: メタデータで元の次元情報を保持

### リスク2: 既存コードとの非互換性
**対策**: コンパイラフラグで切り替え可能に

### リスク3: LLVM最適化パスとの競合
**対策**: 最適化レベルごとに調整

## 6. 段階的リリース計画

### v0.11.1: 基本実装
- 2次元配列のフラット化
- 基本的なインデックス変換

### v0.11.2: 最適化追加
- ループ最適化
- SIMD対応

### v0.11.3: 完全版
- 任意次元対応
- カスタム最適化パス