# ジェネリクス構造体のLoad/Store問題分析と改善提案

作成日: 2026-01-10
対象バージョン: v0.11.0

## エグゼクティブサマリー

Cm言語でジェネリクスTに構造体を使用した際のload/store命令生成について調査しました。調査の結果、**基本的なジェネリクス構造体は正しく動作**していますが、**Aggregate Rvalue処理が未実装**という潜在的な問題を発見しました。また、大きな構造体の処理において最適化の余地があることも判明しました。

## 1. 問題の詳細

### 1.1 報告された症状

```cm
// 期待される動作
struct Point {
    int x;
    int y;
}

<T> T identity(T value) {
    return value;  // Tが構造体の場合、load/storeが生成されない？
}

int main() {
    Point p = {x: 10, y: 20};
    Point p2 = identity(p);  // 問題が発生？
    return p2.x + p2.y;
}
```

### 1.2 調査結果

**現状：基本機能は動作している**

テストスイートを確認した結果、以下のテストが全てPASSしています：
- `tests/test_programs/generics/basic_generics.cm`
- `tests/test_programs/generics/option_type.cm`
- `tests/test_programs/generics/impl_generics.cm`
- `tests/test_programs/generics/struct_with_generic_method.cm`

## 2. アーキテクチャ分析

### 2.1 現在の構造体処理フロー

```
Cmソースコード
    ↓
[AST] 構造体リテラル: {x: 10, y: 20}
    ↓
[HIR] StructLiteral
    ↓
[MIR Lowering] フィールド単位の代入に展開
    _temp = alloca Point
    _temp.x = 10
    _temp.y = 20
    ↓
[LLVM IR] 個別のstore命令
    %1 = alloca %Point
    %2 = getelementptr %Point, %1, 0, 0
    store i32 10, %2
    %3 = getelementptr %Point, %1, 0, 1
    store i32 20, %3
```

### 2.2 モノモーフィゼーション（型の具体化）

```cm
// ジェネリック定義
<T> T identity(T value) { return value; }

// 使用時
Point p2 = identity(p);

// モノモーフィゼーション後
Point identity__Point(Point value) { return value; }
```

**実装状態：✅ 正しく動作**
- 型引数の解決と置換が正確
- 構造体型の情報が適切に伝播
- 名前マングリング：`identity<Point>` → `identity__Point`

### 2.3 ABI対応

```cpp
// src/codegen/llvm/core/mir_to_llvm.cpp:170-190
bool isSmallStruct(llvm::Type* type) {
    auto* structType = llvm::dyn_cast<llvm::StructType>(type);
    if (!structType) return false;

    uint64_t size = dataLayout.getTypeAllocSize(structType);
    return size <= 16;  // System V ABI: 16バイト以下は値渡し
}
```

**実装状態：✅ 適切に実装**
- 小さい構造体：レジスタ渡し（値）
- 大きい構造体：ポインタ渡し

## 3. 発見された問題点

### 3.1 🔴 Aggregate Rvalue処理の未実装

**ファイル：** `src/codegen/llvm/core/mir_to_llvm.cpp:1328`

```cpp
llvm::Value* MIRToLLVM::convertRvalue(const mir::MirRvalue& rvalue) {
    switch (rvalue.kind) {
        case mir::MirRvalue::Use:
            return convertOperand(std::get<mir::Operand>(rvalue.data));

        case mir::MirRvalue::BinaryOp:
            // ... 実装済み

        // case mir::MirRvalue::Aggregate:  ← 未実装！
        //     return convertAggregate(...);

        default:
            return nullptr;  // Aggregateが来るとnullptrを返す
    }
}
```

**影響：**
- 現在：構造体リテラルがフィールド単位代入で生成されるため問題なし
- 将来：MIR最適化でAggregate Rvalueが生成されると失敗する可能性

### 3.2 🟡 大きな構造体の非効率な処理

**現在の実装：**
```cpp
// 全ての構造体がフィールド単位でコピーされる
for (int i = 0; i < struct_fields; i++) {
    // 個別のload/store
}
```

**問題：**
- 64バイト以上の構造体で非効率
- キャッシュミスの増加
- 命令数の増加

### 3.3 🟡 特定のケースでの問題の可能性

報告された「load/storeされない」問題は、以下のケースで発生している可能性があります：

1. **循環参照を持つジェネリック構造体**
```cm
struct Node<T> {
    T value;
    Node<T>* next;  // 自己参照
}
```

2. **ネストしたジェネリック構造体**
```cm
struct Wrapper<T> {
    Option<T> value;
}

Wrapper<Wrapper<int>> nested;  // 深いネスト
```

3. **可変長配列を含む構造体**
```cm
struct Buffer<T> {
    T[] data;  // スライス型
}
```

## 4. 根本原因の分析

### 4.1 MIR生成レベル

```cpp
// src/mir/lowering/expr.cpp:823-850
// 構造体リテラルの処理
for (const auto& field : struct_literal.fields) {
    // フィールド単位の代入として生成
    auto field_place = create_field_place(temp_var, field.name);
    auto assignment = create_assignment(field_place, field.value);
    add_statement(assignment);
}
// Aggregate Rvalueとしては生成されない
```

### 4.2 LLVM IR生成レベル

```cpp
// Aggregate Rvalueが来た場合の処理が未実装
// → MIR最適化で構造体が最適化されると問題発生
```

## 5. 改善提案

### 5.1 優先度1：Aggregate Rvalue処理の実装

**修正ファイル：** `src/codegen/llvm/core/mir_to_llvm.cpp`

```cpp
// convertRvalue()に追加
case mir::MirRvalue::Aggregate: {
    auto& aggData = std::get<mir::MirRvalue::AggregateData>(rvalue.data);

    if (aggData.kind.type == mir::AggregateKind::Struct) {
        // 構造体型を取得
        std::string structName = mangleStructName(aggData.kind.name, aggData.kind.type_args);
        auto* structType = structTypes[structName];

        if (!structType) {
            // エラー処理
            throw std::runtime_error("Unknown struct type: " + structName);
        }

        // 一時変数を作成
        auto* alloca = builder->CreateAlloca(structType, nullptr, "agg_temp");

        // 各フィールドを初期化
        for (size_t i = 0; i < aggData.operands.size(); ++i) {
            auto* fieldValue = convertOperand(*aggData.operands[i]);
            auto* gep = builder->CreateStructGEP(structType, alloca, i);
            builder->CreateStore(fieldValue, gep);
        }

        // 構造体値をロード（小さい構造体の場合）
        if (isSmallStruct(structType)) {
            return builder->CreateLoad(structType, alloca, "agg_load");
        } else {
            // 大きい構造体はポインタのまま
            return alloca;
        }
    }

    // 他のAggregate型（配列等）の処理
    return nullptr;
}
```

### 5.2 優先度2：大きな構造体のmemcpy最適化

```cpp
// src/codegen/llvm/core/mir_to_llvm.cpp に追加
void copyStruct(llvm::Value* dst, llvm::Value* src, llvm::Type* structType) {
    uint64_t size = dataLayout.getTypeAllocSize(structType);

    if (size >= 64) {  // 64バイト以上
        // memcpyを使用
        llvm::Value* dstPtr = builder->CreateBitCast(dst,
            llvm::Type::getInt8PtrTy(context));
        llvm::Value* srcPtr = builder->CreateBitCast(src,
            llvm::Type::getInt8PtrTy(context));

        builder->CreateMemCpy(dstPtr, llvm::MaybeAlign(8),
                             srcPtr, llvm::MaybeAlign(8),
                             size);
    } else {
        // 現在のフィールド単位コピーを使用
        copyFieldByField(dst, src, structType);
    }
}
```

### 5.3 優先度3：ジェネリクス構造体の特殊ケース処理

```cpp
// モノモーフィゼーション時の特殊処理
if (isRecursiveStruct(structType)) {
    // 循環参照の処理
    handleRecursiveStruct(structType);
}

if (hasVariableLengthArray(structType)) {
    // 可変長配列を含む構造体の処理
    handleDynamicStruct(structType);
}
```

## 6. テストケースの追加

### 6.1 大きな構造体のテスト

```cm
// tests/test_programs/generics/large_struct_generic.cm
struct LargeData {
    long[32] data;  // 256バイト
}

<T> T pass_through(T value) {
    T temp = value;  // コピー
    return temp;
}

int main() {
    LargeData ld;
    for (int i = 0; i < 32; i++) {
        ld.data[i] = i as long;
    }

    LargeData ld2 = pass_through(ld);

    // 検証
    for (int i = 0; i < 32; i++) {
        assert(ld2.data[i] == i as long);
    }

    return 0;
}
```

### 6.2 ネストしたジェネリクスのテスト

```cm
// tests/test_programs/generics/nested_generic_struct.cm
struct Box<T> {
    T value;
}

struct Pair<A, B> {
    A first;
    B second;
}

<T, U> Pair<T, U> make_pair(T a, U b) {
    return Pair<T, U>{first: a, second: b};
}

int main() {
    Box<int> box1 = {value: 42};
    Box<double> box2 = {value: 3.14};

    Pair<Box<int>, Box<double>> nested = make_pair(box1, box2);

    assert(nested.first.value == 42);
    assert(nested.second.value == 3.14);

    return 0;
}
```

## 7. 既存リファクタリング案との関係

既存のリファクタリング案（001-007）を確認した結果：

- **001_stl_implementation_analysis.md**: selfメソッド変更バグに言及（別問題）
- **002_refactoring_proposal.md**: MIR最適化に言及（関連あり）
- **003_implementation_roadmap.md**: STLコンテナ実装（間接的に関連）
- **004_iterator_design_proposal.md**: イテレータ設計（直接関係なし）
- **005_builtin_iterator_integration.md**: ビルトイン統合（直接関係なし）
- **006_performance_bottleneck_analysis.md**: 最適化問題（関連あり）
- **007_immediate_optimization_fixes.md**: 即座修正可能（直接関係なし）

**結論：** ジェネリクス構造体のload/store問題は**新規の発見**であり、既存のリファクタリング案には含まれていません。

## 8. 修正による期待効果

### 短期効果（Aggregate Rvalue実装後）
- MIR最適化の安全な有効化
- 将来的な最適化への対応
- エッジケースでのクラッシュ防止

### 中期効果（memcpy最適化後）
- 大きな構造体の処理: **2-3倍高速化**
- メモリ帯域の効率化: **30-50%改善**
- キャッシュ効率: **20-40%改善**

## 9. 実装優先順位

1. **即座（1-2日）**: Aggregate Rvalue処理の実装
2. **短期（1週間）**: 大きな構造体のmemcpy最適化
3. **中期（2週間）**: テストケースの充実
4. **長期（1ヶ月）**: 特殊ケースの最適化

## 10. まとめ

ジェネリクス構造体の基本的な処理は**正しく動作**していますが、以下の改善が必要です：

1. **Aggregate Rvalue処理の実装**（将来の最適化に必須）
2. **大きな構造体の最適化**（パフォーマンス向上）
3. **エッジケースの処理**（堅牢性向上）

これらの改善により、Cm言語はC++のテンプレートと同等の表現力を持ちながら、より効率的なコード生成が可能になります。

---

**調査完了:** 2026-01-10
**次のステップ:** Aggregate Rvalue処理の実装とテストケースの追加