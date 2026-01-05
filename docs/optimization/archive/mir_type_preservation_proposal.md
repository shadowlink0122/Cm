# MIR型情報保持の改善提案

## 問題の背景
LLVM 14+ のopaque pointerでは、CreateLoad命令に明示的なpointee typeが必要ですが、MIR最適化によって型情報が失われるため、O1以上でアサーション失敗が発生しています。

## 解決策

### 1. MirPlaceへの型情報追加

```cpp
// src/mir/nodes.hpp
struct MirPlace {
    LocalId local;
    std::vector<PlaceProjection> projections;
    hir::TypePtr type;  // ← 追加: このPlaceが示す値の型
    hir::TypePtr pointee_type;  // ← 追加: ポインタの場合のpointee type

    MirPlace(LocalId l, hir::TypePtr t)
        : local(l), type(t) {
        // ポインタ型の場合、pointee_typeを設定
        if (auto ptr_type = std::dynamic_pointer_cast<hir::PointerType>(t)) {
            pointee_type = ptr_type->pointee;
        }
    }
};
```

### 2. MirOperandへの型情報拡張

```cpp
struct MirOperand {
    enum Kind { Move, Copy, Constant, FunctionRef };

    Kind kind;
    std::variant<MirPlace, MirConstant, std::string> data;
    hir::TypePtr type;  // ← 追加: オペランドの型情報

    // ファクトリメソッドも型を受け取るように変更
    static MirOperandPtr move(MirPlace place, hir::TypePtr type) {
        auto op = std::make_unique<MirOperand>();
        op->kind = Move;
        op->data = std::move(place);
        op->type = type;  // 型情報を保持
        return op;
    }
};
```

### 3. PlaceProjectionへの型情報追加

```cpp
struct PlaceProjection {
    ProjectionKind kind;
    union {
        FieldId field_id;
        LocalId index_local;
    };
    hir::TypePtr result_type;  // ← 追加: 投影後の型
    hir::TypePtr pointee_type;  // ← 追加: Derefの場合のpointee type
};
```

### 4. MIR→LLVM変換の改善

```cpp
// src/codegen/llvm/core/mir_to_llvm.cpp
llvm::Value* MirToLLVM::emit_load(const MirPlace& place) {
    auto [ptr, _] = emit_place(place);

    // opaque pointer対応：型情報から正確なpointee typeを取得
    llvm::Type* pointee_type = nullptr;
    if (place.pointee_type) {
        pointee_type = convert_type(place.pointee_type);
    } else if (place.type) {
        // ポインタ型から推論
        if (auto ptr_type = std::dynamic_pointer_cast<hir::PointerType>(place.type)) {
            pointee_type = convert_type(ptr_type->pointee);
        }
    }

    if (!pointee_type) {
        // フォールバック：LLVM型から取得を試みる
        if (auto ptr_type = llvm::dyn_cast<llvm::PointerType>(ptr->getType())) {
            if (!ptr_type->isOpaque()) {
                pointee_type = ptr_type->getElementType();
            }
        }
    }

    assert(pointee_type && "Failed to determine pointee type for load");
    return builder.CreateLoad(pointee_type, ptr);
}
```

### 5. 最適化パスでの型情報保持

各最適化パスで値を変換する際、必ず型情報を伝播：

```cpp
// 例：Constant Folding
MirRvaluePtr fold_binary_op(const MirBinaryOp& op) {
    auto result = compute_folded_value(op);
    // 型情報を保持して新しい定数を作成
    auto constant = std::make_unique<MirConstant>();
    constant->value = result;
    constant->type = op.lhs->type;  // 元の型情報を保持
    return MirRvalue::constant(std::move(constant));
}
```

## 実装計画

### Phase 1: 基本的な型情報の追加（短期）
1. MirPlace、MirOperandに型フィールドを追加
2. HIR→MIR変換時に型情報を設定
3. 既存のテストが通ることを確認

### Phase 2: LLVM IR生成の改善（中期）
1. CreateLoad呼び出しで型情報を使用
2. その他のLoad/Store関連命令も同様に修正
3. O1最適化レベルでのテスト

### Phase 3: 最適化パスの更新（長期）
1. 全ての最適化パスで型情報を適切に伝播
2. 型情報の一貫性チェック機能を追加
3. O2、O3最適化レベルでの完全なテスト

## 期待される効果

1. **即座の問題解決**: O1-O3でのCreateLoadアサーション失敗を回避
2. **将来の保守性**: 型情報が明示的になることでデバッグが容易に
3. **最適化の改善**: より正確な型情報により、より積極的な最適化が可能に

## リスクと考慮事項

- メモリ使用量の若干の増加（型情報の重複保持）
- 既存コードへの広範な変更が必要
- 段階的な実装により、一時的に不整合が生じる可能性

## 代替案

最小限の変更として、LLVM IR生成時にHIRの型情報テーブルを参照する方法もありますが、最適化による変換後の正確な型情報を得るのが困難なため、MIRノード自体に型情報を持たせる方が確実です。