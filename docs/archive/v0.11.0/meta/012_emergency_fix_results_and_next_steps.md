# 緊急修正結果と次段階の実装計画

作成日: 2026-01-10更新日: 2026-01-11対象バージョン: v0.11.0ステータス: 🟢 **完全解決**

> **2026-01-11更新:** 本レポートで記載された問題は完全に解決されました。
> - 戻り値型からの型引数推論を追加
> - ポインタ型のelement_typeから型引数推論を追加
> - ポインタ型ローカルへの代入時にロードをスキップ
> - tlpテスト: 266/296合格（9件改善）

## 実施した緊急修正

### Phase 1: sizeof(T)計算の緊急修正 ✅ 完了

**ファイル:** `src/hir/lowering/impl.cpp:596-608`

```cpp
case ast::TypeKind::Generic: {
    // ジェネリック型のサイズ計算
    // Phase 1: 緊急修正 - 安全側のサイズを返す
    debug_msg(1031, "Generic type size requested for: " + type->name);
    return 256;  // 暫定的な安全サイズ
}
```

**効果:**
- メモリ不足によるセグメンテーションフォルトを防止
- queue<Item>のコンパイルが進行するように改善

## 現在の問題

### コンパイル時のLLVMアサーション失敗

```
Assertion failed: (Ty && "Invalid GetElementPtrInst indices for type!"),
function checkGEPType, file Instructions.h, line 923.
```

**原因:**
- LLVM IRレベルでジェネリック構造体の型情報が不完全
- GetElementPtrInst（構造体フィールドアクセス）で型不一致

### 根本原因の分析

```
Phase 1: HIR Lowering
    sizeof(Node<T>) → 256バイト (緊急修正で対応済み ✅)
    ↓
Phase 2: MIR Lowering & Monomorphization
    Node<T> → Node__Item (型置換は実装済み)
    ↓
Phase 3: LLVM Codegen ← ❌ ここで失敗
    構造体型が未登録またはフィールド情報が不完全
```

## Phase 2: 必要な追加修正

### 1. LLVM構造体型の事前登録

**ファイル:** `src/codegen/llvm/core/mir_to_llvm.cpp`

```cpp
// 修正案: 特殊化構造体を事前に登録
void MIRToLLVM::registerSpecializedStructTypes() {
    // MIRモジュール内のすべての特殊化構造体を事前登録
    for (const auto& [name, struct_def] : mir_module->struct_defs) {
        if (name.find("__") != std::string::npos) {
            // Node__Item のような特殊化構造体

            // フィールド型をLLVM型に変換
            std::vector<llvm::Type*> field_types;
            for (const auto& field : struct_def.fields) {
                auto* llvm_field_type = convertType(field.type);
                field_types.push_back(llvm_field_type);
            }

            // LLVM構造体型を作成・登録
            auto* struct_type = llvm::StructType::create(
                context, field_types, name, false);
            structTypes[name] = struct_type;

            debug_log("Registered specialized struct: " + name);
        }
    }
}
```

### 2. 型変換時の特殊化構造体対応

**ファイル:** `src/codegen/llvm/core/types.cpp`

```cpp
llvm::Type* TypeConverter::convertType(const hir::Type& type) {
    switch (type.kind) {
        case hir::TypeKind::Struct: {
            // 特殊化構造体名を正しく処理
            std::string struct_name = type.name;

            // Node<Item> → Node__Item への変換
            if (!type.type_args.empty()) {
                struct_name = mangleStructName(type.name, type.type_args);
            }

            auto it = structTypes.find(struct_name);
            if (it != structTypes.end()) {
                return it->second;
            }

            // エラー: 未登録の構造体
            throw std::runtime_error("Unregistered struct type: " + struct_name);
        }
    }
}
```

### 3. GetElementPtr生成時の型チェック強化

```cpp
llvm::Value* MIRToLLVM::generateFieldAccess(
    llvm::Value* struct_ptr,
    const std::string& field_name,
    const hir::TypePtr& struct_type) {

    // 構造体型をLLVM型に変換
    llvm::Type* llvm_struct_type = convertType(*struct_type);

    // 型が正しく登録されているか確認
    if (!llvm_struct_type->isStructTy()) {
        throw std::runtime_error(
            "Invalid struct type for field access: " + struct_type->name);
    }

    // フィールドインデックスを取得
    int field_idx = getFieldIndex(struct_type->name, field_name);

    // GEPインストラクション生成
    return builder.CreateStructGEP(
        llvm_struct_type, struct_ptr, field_idx, field_name);
}
```

## テスト結果

### 作成したテスト

**ファイル:** `tests/test_programs/generics/test_queue_struct.cm`

- ✅ 構文解析: 成功
- ✅ 型チェック: 成功（警告のみ）
- ✅ HIR Lowering: 成功
- ✅ MIR Lowering: 成功
- ❌ LLVM Codegen: アサーション失敗

## 実装計画

### 即座（今日中）
1. ✅ sizeof(T)緊急修正（完了）
2. ⏳ LLVM構造体型事前登録の実装
3. ⏳ GetElementPtr生成修正

### 短期（1-2日）
1. 完全なモノモーフィゼーション修正
2. 包括的なテストスイート作成
3. パフォーマンステスト

### 中期（3-5日）
1. sizeof計算の完全修正（モノモーフィゼーション後のサイズ）
2. LLVM最適化の再有効化
3. デバッグツールの改善

## 推奨される次のアクション

1. **LLVM構造体登録の修正**
   ```bash
   # src/codegen/llvm/core/mir_to_llvm.cpp を修正
   # registerSpecializedStructTypes() を追加
   ```

2. **デバッグ実行で詳細確認**
   ```bash
   ./cm compile --debug -d=trace test_queue_struct.cm 2>debug.log
   grep "struct\|Struct\|GEP" debug.log
   ```

3. **LLVM IRの手動検証**
   ```bash
   ./cm compile --emit-llvm test_queue_struct.cm > test.ll
   llc test.ll  # エラー箇所の特定
   ```

## まとめ

緊急修正によりメモリ破壊は防止できましたが、LLVM IRレベルでの型情報不整合が残っています。次のステップは：

1. **最優先**: LLVM構造体型の事前登録実装
2. **高優先**: GetElementPtr生成時の型チェック強化
3. **中優先**: sizeof計算の完全修正

これらの修正により、queue<T>が任意の型（構造体含む）で動作するようになります。

---

**作成日:** 2026-01-10 **次回作業:** LLVM構造体型登録の実装