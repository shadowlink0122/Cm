# 多次元配列ベクトル化最適化 - 詳細設計書 v2

## 概要

本設計書は、Cm言語の多次元配列（構造体配列を含む）に対して、C++/Rustと同等のベクトル化パフォーマンスを達成するための設計を示す。

## 背景調査: Clang/GCC/LLVM のアプローチ

### 1. LLVM GEP (getelementptr) の設計原則

**型認識（Type-Aware）**:
```llvm
; 3次元配列 int a[3][4][5] のアクセス a[i][j][k]
%ptr = getelementptr [3 x [4 x [5 x i32]]], ptr %a, i64 0, i64 %i, i64 %j, i64 %k
```

- GEPは**バイト数ではなく要素数**でオフセットを計算
- 各インデックスは対応する次元のサイズを考慮
- `inbounds`キーワードで境界内保証を示す

### 2. Clangの多次元配列IR生成

**配列型の表現**:
```llvm
; C: int matrix[300][300]
@matrix = alloca [300 x [300 x i32]]  ; 配列の配列として表現
```

**アクセスパターン**:
```llvm
; C: matrix[i][j]
%0 = getelementptr inbounds [300 x [300 x i32]], ptr %matrix, i64 0, i64 %i, i64 %j
%val = load i32, ptr %0
```

### 3. 構造体配列のアクセス

**構造体の配列**:
```c
struct Point { int x, y, z; };
Point points[100];
int val = points[i].y;
```

**生成されるLLVM IR**:
```llvm
%struct.Point = type { i32, i32, i32 }
%0 = getelementptr inbounds [100 x %struct.Point], ptr %points, i64 0, i64 %i, i32 1
%val = load i32, ptr %0
```

### 4. LLVM最適化パスの動作

| パス | 役割 |
|------|------|
| SROA | スカラー置換、配列構造の分析 |
| LICM | ループ不変コード移動 |
| SCEV | ループインデックス解析 |
| LoopVectorize | ベクトル化判定・変換 |

**ベクトル化の条件**:
1. ループ内メモリアクセスが連続（unit stride）
2. エイリアスなし（TBAA/noaliasで証明）
3. ループ trip count が十分

## 現在の問題分析

### 症状
```
1D配列 (手動base計算): 0.31s - ベクトル化あり
2D配列 (a[i][j]構文):   3.29s - ベクトル化なし (10倍遅い)
```

### 原因

**1. 現在のCmコード生成**:
```llvm
; 毎回計算される
%row_stride = mul i64 %i, 300
%linear = add i64 %row_stride, %j
%ptr = gep i32, ptr %base, i64 %linear
```

**2. 1Dベンチマークの手動最適化**:
```cm
int base_c = i * n;         // ← ループ外で1回計算
c[base_c + j] = ...         // ← 変数再利用
```
```llvm
; base_cがレジスタに保持される
%base_c = mul i32 %i, %n    ; ループ外
%idx = add i32 %base_c, %j  ; ループ内
```

**3. なぜCSEが効かないか**:
- SSA形式では、同じ計算でも異なるValue
- 各load操作が新しいValueを生成
- LLVMはMIR localの同一性を認識できない

## 設計方針

### 原則: Clangと同じGEP構造を生成

**Before (現状)**:
```llvm
%flat_base = gep [90000 x i32], ptr %arr, i64 0, i64 0
%linear = add i64 (mul %i, 300), %j
%ptr = gep i32, ptr %flat_base, i64 %linear
```

**After (目標)**:
```llvm
; Clangと同じ: 多次元GEP
%ptr = gep inbounds [300 x [300 x i32]], ptr %arr, i64 0, i64 %i, i64 %j
```

### 構造体配列対応

**Cmコード**:
```cm
struct Point { int x; int y; int z; }
Point[100] points;
int val = points[i].y;
```

**生成すべきLLVM IR**:
```llvm
%struct.Point = type { i32, i32, i32 }
%0 = gep inbounds [100 x %struct.Point], ptr %points, i64 0, i64 %i, i32 1
```

## 実装設計

### 変更箇所

**1. mir_to_llvm.cpp: convertPlaceToAddress()**

現在の実装を以下のように変更:

```cpp
// 現在: フラット化してlinearIndex計算
llvm::Value* linearIndex = ...;
addr = builder->CreateGEP(elemType, basePtr, linearIndex, "flat_elem_ptr");

// 変更後: 多次元GEPを直接生成
std::vector<llvm::Value*> gepIndices;
gepIndices.push_back(llvm::ConstantInt::get(ctx.getI64Type(), 0)); // 配列ポインタ

for (const auto& index : indexValues) {
    gepIndices.push_back(index);
}

// フィールドアクセスの場合
for (const auto& field : fieldProjections) {
    gepIndices.push_back(llvm::ConstantInt::get(ctx.getI32Type(), field.index));
}

addr = builder->CreateInBoundsGEP(arrayType, basePtr, gepIndices, "elem_ptr");
```

**2. 配列型の保持**

現在allocaで`[90000 x i32]`（フラット）を生成しているが、これを`[300 x [300 x i32]]`（多次元）に変更:

```cpp
// 現在
llvm::Type* flatType = llvm::ArrayType::get(elemType, totalSize);
auto alloca = builder->CreateAlloca(flatType, ...);

// 変更後
llvm::Type* multiDimType = elemType;
for (auto dimIt = dimensions.rbegin(); dimIt != dimensions.rend(); ++dimIt) {
    multiDimType = llvm::ArrayType::get(multiDimType, *dimIt);
}
auto alloca = builder->CreateAlloca(multiDimType, ...);
```

**3. TBAAメタデータ（現状維持）**

現在実装済みのTBAAメタデータ付与は有効な最適化として維持:

```cpp
if (tbaaManager && targetType) {
    llvm::MDNode* tbaaTag = tbaaManager->getTypeAccessTag(targetType);
    if (tbaaTag) {
        loadInst->setMetadata(llvm::LLVMContext::MD_tbaa, tbaaTag);
    }
}
```

### 型推論の改善

**convertType()の修正**:

```cpp
llvm::Type* convertType(const hir::TypePtr& type) {
    if (type->kind == hir::TypeKind::Array) {
        auto elemType = convertType(type->element_type);
        if (type->array_size.has_value()) {
            // 固定サイズ配列: そのまま保持
            return llvm::ArrayType::get(elemType, *type->array_size);
        } else {
            // 動的スライス: ポインタ
            return llvm::PointerType::getUnqual(elemType);
        }
    }
    // ... 他の型
}
```

## 削除すべき実験コード

以下は効果がなかった実験的コードで、削除対象:

1. `baseOffsetCache` (mir_to_llvm.hpp)
2. `indexLocals` vector (mir_to_llvm.cpp)
3. `array_base_extraction.hpp` (MIRパス全体)
4. `baseOffset/finalIndex` 分離ロジック

## 検証計画

### テストケース

1. **基本2D配列**: `int[300][300]` matmul
2. **3D配列**: `int[10][20][30]` アクセス
3. **構造体配列**: `Point[100]` フィールドアクセス
4. **構造体内配列**: `struct { int[10] arr; }` 
5. **ネスト構造体**: `struct { Point[10] points; }`

### パフォーマンス目標

| テスト | 現状 | 目標 |
|--------|------|------|
| 2D matmul | 3.29s | <0.5s |
| 構造体配列 | TBD | 1D相当 |

## 次のステップ

1. ✅ 設計書作成
2. [ ] 実験コード削除（git revert）
3. [ ] convertType()で多次元型保持
4. [ ] convertPlaceToAddress()で多次元GEP生成
5. [ ] 構造体配列テスト追加
6. [ ] ベンチマーク検証
