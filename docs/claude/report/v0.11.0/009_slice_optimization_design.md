# 005: スライス最適化設計（ベアメタル対応）

## 1. スライスの現状と課題

### 1.1 現在のスライス実装
```c
// runtime/cm_runtime.c
typedef struct CmSlice {
    void* data;
    size_t length;
    size_t capacity;
    size_t elem_size;
} CmSlice;

// 問題点:
// 1. malloc/reallocに依存（ベアメタル非対応）
// 2. メタデータのオーバーヘッド（32バイト）
// 3. 間接参照によるキャッシュミス
```

### 1.2 ベアメタル対応の必要性
- 組み込みシステムでのメモリ管理
- リアルタイムシステムでの決定的動作
- 静的メモリ配置の要求

## 2. 最適化されたスライス設計

### 2.1 静的メモリプールベースのスライス
```cpp
// 新しいスライス実装
template<typename T, size_t MAX_SIZE>
struct StaticSlice {
    T data[MAX_SIZE];      // 静的配列（スタック上）
    uint32_t length;       // 現在の長さ
    uint32_t capacity;     // = MAX_SIZE（コンパイル時定数）

    // インライン展開されるアクセサ
    T& operator[](size_t idx) {
        return data[idx];  // 境界チェックは最適化で除去
    }
};

// Cm言語での構文
int[..1024] slice;  // 最大1024要素の静的スライス
```

### 2.2 Small Buffer Optimization (SBO)
```cpp
struct OptimizedSlice {
    union {
        // 小さいデータはインライン格納
        struct {
            uint8_t inline_data[24];
            uint32_t length : 16;
            uint32_t is_inline : 1;
            uint32_t reserved : 15;
        } small;

        // 大きいデータは外部参照
        struct {
            void* external_data;
            uint32_t length;
            uint32_t capacity;
        } large;
    } storage;

    bool is_small() const {
        return storage.small.is_inline;
    }
};
```

## 3. MIRレベルでのスライス表現

### 3.1 スライス型の拡張
```cpp
// src/mir/nodes.hpp
enum class SliceKind {
    Dynamic,      // 従来の動的スライス
    Static,       // 静的メモリプール
    Inline,       // SBO適用
    View          // 既存配列のビュー（所有権なし）
};

struct MirSliceType {
    TypePtr element_type;
    SliceKind kind;
    std::optional<uint32_t> max_size;  // 静的スライスの最大サイズ

    bool needs_allocation() const {
        return kind == SliceKind::Dynamic;
    }

    uint32_t get_stack_size() const {
        if (kind == SliceKind::Static && max_size) {
            return element_type->size() * max_size.value() + 8;  // data + metadata
        }
        return 32;  // メタデータのみ
    }
};
```

### 3.2 スライス操作の最適化
```cpp
// src/mir/lowering/slice_lowering.cpp

class SliceLowering {
    mir::MirRvalue lowerSliceCreation(const hir::SliceExpr& expr) {
        auto slice_type = analyzeSliceUsage(expr);

        switch (slice_type.kind) {
            case SliceKind::Static:
                return createStaticSlice(expr);
            case SliceKind::Inline:
                return createInlineSlice(expr);
            case SliceKind::View:
                return createSliceView(expr);
            default:
                return createDynamicSlice(expr);
        }
    }

    mir::MirRvalue createStaticSlice(const hir::SliceExpr& expr) {
        // スタック上に静的配列を確保
        mir::MirLocal local;
        local.type = make_static_slice_type(expr.element_type, expr.max_size);
        local.name = generate_temp_name();

        // 初期化コード生成
        std::vector<mir::MirStatement> init_stmts;

        // length = 0
        init_stmts.push_back(mir::Assign{
            .place = mir::MirPlace{local.id, {Projection::Field(1)}},  // length field
            .rvalue = mir::Constant{0}
        });

        return mir::UseLocal{local.id};
    }
};
```

## 4. LLVM最適化

### 4.1 静的スライスのコード生成
```cpp
// src/codegen/llvm/core/slice_codegen.cpp

llvm::Type* SliceCodeGen::getStaticSliceType(const MirSliceType& type) {
    auto elem_type = convertType(type.element_type);
    auto array_type = llvm::ArrayType::get(elem_type, type.max_size.value());

    // 構造体: { [N x T] data, i32 length }
    return llvm::StructType::get(ctx.getContext(), {
        array_type,
        ctx.getI32Type()  // length
    });
}

llvm::Value* SliceCodeGen::generateSliceAccess(llvm::Value* slice,
                                              llvm::Value* index,
                                              const MirSliceType& type) {
    if (type.kind == SliceKind::Static) {
        // 静的スライス: 直接配列アクセス
        auto zero = llvm::ConstantInt::get(ctx.getI32Type(), 0);
        std::vector<llvm::Value*> indices = {zero, zero, index};

        auto gep = builder->CreateGEP(slice->getType()->getPointerElementType(),
                                     slice, indices, "slice_elem");

        // 境界チェック（デバッグビルドのみ）
        if (enable_bounds_check) {
            insertBoundsCheck(slice, index, type);
        }

        return gep;
    }

    // 動的スライス: 間接参照
    return generateDynamicAccess(slice, index);
}

void SliceCodeGen::insertBoundsCheck(llvm::Value* slice,
                                    llvm::Value* index,
                                    const MirSliceType& type) {
    // lengthフィールドを取得
    auto length_ptr = builder->CreateStructGEP(
        slice->getType()->getPointerElementType(),
        slice, 1, "length_ptr");
    auto length = builder->CreateLoad(ctx.getI32Type(), length_ptr, "length");

    // 境界チェック: index < length
    auto cmp = builder->CreateICmpULT(index, length, "bounds_check");

    // パニックブロックと継続ブロック
    auto panic_bb = llvm::BasicBlock::Create(ctx.getContext(), "panic");
    auto cont_bb = llvm::BasicBlock::Create(ctx.getContext(), "continue");

    builder->CreateCondBr(cmp, cont_bb, panic_bb);

    // パニック処理
    builder->SetInsertPoint(panic_bb);
    generatePanic("slice index out of bounds");

    builder->SetInsertPoint(cont_bb);
}
```

### 4.2 スライス操作の最適化
```cpp
// push操作の最適化
llvm::Value* SliceCodeGen::generatePush(llvm::Value* slice,
                                       llvm::Value* value,
                                       const MirSliceType& type) {
    if (type.kind == SliceKind::Static) {
        // 静的スライス: インライン展開可能
        auto length_ptr = builder->CreateStructGEP(
            slice->getType()->getPointerElementType(),
            slice, 1, "length_ptr");
        auto length = builder->CreateLoad(ctx.getI32Type(), length_ptr, "length");

        // data[length] = value
        auto data_ptr = builder->CreateStructGEP(
            slice->getType()->getPointerElementType(),
            slice, 0, "data_ptr");
        auto elem_ptr = builder->CreateGEP(
            type.element_type, data_ptr, {zero, length}, "new_elem");
        builder->CreateStore(value, elem_ptr);

        // length++
        auto new_length = builder->CreateAdd(length, one, "new_length");
        builder->CreateStore(new_length, length_ptr);

        return slice;
    }

    return generateDynamicPush(slice, value);
}
```

## 5. ループ最適化

### 5.1 スライスイテレーションの最適化
```cm
// Cm言語のコード
int[..100] slice;
for (int val : slice) {
    process(val);
}

// 最適化後のLLVM IR（擬似コード）
// ポインタベースのイテレーション
%ptr = getelementptr %slice, 0, 0, 0
%end = getelementptr %slice, 0, 0, %length
loop:
    %current = phi [%ptr, %entry], [%next, %loop]
    %val = load %current
    call @process(%val)
    %next = getelementptr %current, 1
    %cond = icmp ult %next, %end
    br %cond, %loop, %exit
```

### 5.2 ベクトル化
```llvm
; スライスのベクトル演算
; slice1 + slice2 -> result
vector.body:
    %vec1 = load <4 x i32>, %slice1_ptr
    %vec2 = load <4 x i32>, %slice2_ptr
    %sum = add <4 x i32> %vec1, %vec2
    store <4 x i32> %sum, %result_ptr

    ; ポインタを進める
    %slice1_ptr = getelementptr %slice1_ptr, 4
    %slice2_ptr = getelementptr %slice2_ptr, 4
    %result_ptr = getelementptr %result_ptr, 4
```

## 6. ビュースライス（ゼロコピー）

### 6.1 配列のスライスビュー
```cm
// Cm言語
int[1000] array;
int[] view = array[100..200];  // ビュー作成（コピーなし）

// 内部表現
struct SliceView {
    int* base;       // array + 100
    uint32_t length; // 100
    // capacityなし（拡張不可）
};
```

### 6.2 文字列スライス
```cm
string text = "Hello, World!";
string_view hello = text[0..5];  // "Hello"のビュー

// ゼロコピー、O(1)操作
```

## 7. メモリレイアウト最適化

### 7.1 構造体内のスライス
```cpp
struct MyStruct {
    int id;
    int[..64] data;  // 静的スライス（インライン）
    float value;
};

// メモリレイアウト（パディングを考慮）
// [id:4][padding:4][data:256][length:4][value:4][padding:4]
// 合計: 280バイト（キャッシュライン境界）
```

### 7.2 スライスの配列
```cpp
// スライスの配列を効率的に配置
int[..32][10] slice_array;  // 10個の静的スライス

// フラットなメモリレイアウト
// [slice0_data][slice0_len][slice1_data][slice1_len]...
```

## 8. パフォーマンス目標

| 操作 | 現在 | 最適化後 | 改善率 |
|-----|------|---------|--------|
| スライス作成 | 100ns | 5ns | 20x |
| push操作 | 50ns | 2ns | 25x |
| インデックスアクセス | 10ns | 1ns | 10x |
| イテレーション（1000要素） | 10μs | 0.5μs | 20x |
| realloc（動的） | 1000ns | 0ns（静的） | ∞ |

## 9. 実装優先順位

### Phase 1: 静的スライス
- 固定サイズスライスの実装
- スタック割り当て
- 境界チェック

### Phase 2: ビュースライス
- ゼロコピービュー
- 文字列スライス
- イテレータ最適化

### Phase 3: 高度な最適化
- SBO実装
- ベクトル化
- カスタムアロケータ統合

## 10. まとめ

この設計により：
1. **ベアメタル対応**: mallocフリーで動作
2. **高速化**: 静的配置で20倍以上高速
3. **メモリ効率**: SBOでヒープ使用を削減
4. **互換性**: 既存コードは変更不要

配列のフラット化と組み合わせることで、Cm言語は組み込みからHPCまで幅広く対応できる高性能言語となります。