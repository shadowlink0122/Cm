# Primitive Type Impl - Borrowed Self Issue

## 概要

プリミティブ型（int, float等）への`impl`内で借用`self`を使用した場合、LLVMコード生成でICmp型不一致エラーが発生する問題の分析と修正案。

## 問題の症状

```cm
impl int for Abs {
    int abs() {
        if (self < 0) {  // ← ICmp assertion error
            return -self;
        }
        return self;
    }
}
```

エラーメッセージ:
```
Assertion failed: getOperand(0)->getType() == getOperand(1)->getType() 
&& "Both operands to ICmp instruction are not of the same type!"
```

## 根本原因

### MIRとLLVMの型表現の不一致

| 項目 | MIR表現 | LLVM表現 |
|------|---------|----------|
| self の型 | `Int` | `i8*` (ポインタ) |
| self へのアクセス | Deref projection | ポインタから値をロード |

MIRでは`self`は`Int`型として記録されるが、LLVMでは借用selfはポインタとして渡される。この不一致が型エラーを引き起こす。

### 構造体 vs プリミティブの違い

```
構造体メソッド (impl Point for Printable):
  MIR: self: Point, projection: [Deref, Field(x)]
  LLVM: 
    1. self_ptr: i8* (引数)
    2. Deref: ポインタのまま使用（GEP用）
    3. Field: GEP + load → i32

プリミティブメソッド (impl int for Abs):
  MIR: self: Int, projection: [Deref]
  LLVM:
    1. self_ptr: i8* (引数)
    2. Deref: 値をロード → i32 ← これが正しい
    現状は: ポインタのまま返している → ICmp error
```

## 試行した修正

### 修正1: Deref on Argument skip (成功)
```cpp
// convertPlaceToAddress内
if (llvm::isa<llvm::Argument>(addr)) {
    needsLoad = false;  // 構造体の場合は正しい
}
```
**結果**: 12件→2件に減少。構造体の借用selfは修正された。

### 修正2: 構造体/プリミティブ判定 (失敗)
```cpp
if (currentType->kind == hir::TypeKind::Struct) {
    needsLoad = false;  // 構造体は skip
} else {
    needsLoad = true;   // プリミティブは load
}
```
**結果**: 2件→19件に増加。currentType追跡が不正確で回帰発生。

### 修正3: Deref後にポインタ以外なら値として返す (失敗)
```cpp
if (lastProj.kind == mir::ProjectionKind::Deref) {
    if (addr && !addr->getType()->isPointerTy()) {
        return addr;  // 既にロード済み
    }
}
```
**結果**: 効果なし。Deref内で正しい型でロードできていない。

## 正しいアーキテクチャ

### 現在のコード構造の問題

```
convertPlaceToAddress:
  - Deref: ポインタ→ポインタをロード（構造体用）
  - 問題: プリミティブ用の分岐がない

convertOperand:
  - projection有: convertPlaceToAddress → CreateLoad
  - projection無: locals[local] を返す
  - 問題: Argument型チェックがprojection有ケースで効かない
```

### 推奨される修正アーキテクチャ

**案1: Deref後のプロジェクション有無で判定**
```cpp
case mir::ProjectionKind::Deref: {
    // 次のプロジェクションがあるかチェック
    bool hasMoreProjections = (projIdx + 1) < place.projections.size();
    
    if (llvm::isa<llvm::Argument>(addr)) {
        if (hasMoreProjections) {
            // 構造体: Deref→Field の場合、ポインタのまま
            needsLoad = false;
        } else {
            // プリミティブ: Deref が最後、値をロード
            needsLoad = true;
            loadType = convertType(currentType);  // i32等
        }
    }
    // ...
}
```

**案2: convertOperandでの統一処理**
```cpp
// convertOperand内、projection有ケース
if (!place.projections.empty()) {
    auto addr = convertPlaceToAddress(place);
    // Derefが最後で、値が必要な場合のみロード
    if (lastProj.kind == Deref && needsValue) {
        return builder->CreateLoad(valueType, addr);
    }
    // Field等は既存ロジック
}
```

## 実装優先度

1. **Phase 1 (完了)**: 構造体借用selfの修正 - 248/262 テスト通過
2. **Phase 2 (未実装)**: プリミティブ借用selfの修正 - 上記案1を実装
3. **Phase 3 (未実装)**: ジェネリック型の借用selfサポート

## 影響を受けるテスト

| テスト | 問題 | Phase |
|--------|------|-------|
| interface/primitive_impl | ICmp type mismatch | Phase 2 |
| generics/impl_generics | string generics hang | Phase 3 |

## 関連コード

- `src/codegen/llvm/core/mir_to_llvm.cpp`
  - `convertPlaceToAddress()`: 行1620-1880
  - `convertOperand()`: 行1410-1620
- `src/mir/nodes.hpp`: ProjectionKind定義
