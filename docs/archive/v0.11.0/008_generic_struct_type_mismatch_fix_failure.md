# ジェネリック構造体の型不一致エラー - 修正失敗の分析

作成日: 2026-01-10
対象バージョン: v0.11.0
ステータス: 🔴 未解決（複数箇所に影響）

## 概要

ジェネリック構造体のsizeof問題（009）の修正を試みましたが、新たな型不一致エラーが発生しました。これらのエラーはMIR lowering、モノモーフィゼーション、LLVM codegenの複数層に影響しており、根本的な再設計が必要な可能性があります。

## 発生したエラー

### エラー1: Item構造体がi32として扱われる

```llvm
store i32 %1, %Item*   ; ❌ 型不一致
; 期待: store %Item %1, %Item*
```

**症状:**
- 構造体`Item`がプリミティブ型`i32`として扱われる
- Store命令で型チェックエラー

### エラー2: Node構造体とポインタ型の不一致

```llvm
store %Node__int, i8**  ; ❌ 型不一致
; 期待: store %Node__int*, %Node__int**
```

**症状:**
- マングリングされた構造体型`Node__int`が汎用ポインタ`i8**`にストアされる
- ポインタレベルの不一致

## 根本原因の分析

### 1. モノモーフィゼーション時のフィールド型置換の不完全性

**ファイル:** `src/mir/lowering/monomorphization_impl.cpp:1115-1126`

```cpp
// 現在の実装（問題あり）
hir::TypePtr field_type = field.type;
if (field_type) {
    // ❌ 単純な名前マッチングのみ
    if (field_type->kind == hir::TypeKind::Generic ||
        type_subst.count(field_type->name) > 0) {
        auto subst_it = type_subst.find(field_type->name);
        if (subst_it != type_subst.end()) {
            field_type = subst_it->second;
        }
    }
}
// ❌ 構造体型の場合、置換が実行されない！
```

**問題点:**
- `T`が`Item`構造体の場合、`field_type->kind`は`Struct`であり`Generic`ではない
- そのため型置換がスキップされ、`T`のままになる

### 2. LLVM型変換時の構造体情報の欠落

**ファイル:** `src/codegen/llvm/core/types.cpp`

```cpp
llvm::Type* TypeConverter::convertType(const hir::Type& type) {
    switch (type.kind) {
        case hir::TypeKind::Struct: {
            auto it = structTypes.find(type.name);
            if (it != structTypes.end()) {
                return it->second;
            }
            // ❌ 特殊化された構造体が登録されていない
            // Node__int が見つからず、不透明型として処理される
            return llvm::StructType::create(context, type.name);
        }
    }
}
```

**問題点:**
- `Node<int>`が`Node__int`にマングリングされるが、`structTypes`に未登録
- フィールド情報が失われ、不透明型として扱われる

### 3. MIR層での型情報の伝播不足

**Store命令生成時の問題:**

```cpp
// src/mir/lowering/stmt.cpp
void generate_store(Place* place, Rvalue* value) {
    // placeの型とvalueの型が一致しない
    // place: Item* (構造体ポインタ)
    // value: i32 (プリミティブ型として誤認識)
}
```

## 型情報の伝播フロー

```
Cmソース: struct Node<T> { T data; }
    ↓
AST: TypeKind::Generic (T)
    ↓
HIR: 型パラメータとして記録
    ↓
MIR Lowering: ジェネリック型のまま
    ↓
モノモーフィゼーション: Node<int> → Node__int
    ❌ フィールド型の置換が不完全
    ↓
LLVM IR生成:
    ❌ Node__intが未登録で不透明型
    ❌ フィールドアクセスが失敗
```

## 修正案

### 修正案1: モノモーフィゼーションの修正

```cpp
// src/mir/lowering/monomorphization_impl.cpp
void generate_specialized_struct(/*...*/) {
    for (auto& field : struct_def->fields) {
        mir::StructField mir_field;
        mir_field.name = field.name;

        // ✅ 再帰的な型置換を使用
        hir::TypePtr field_type = field.type;
        if (field_type) {
            // substitute_type_in_type()は既存の関数
            field_type = substitute_type_in_type(field_type, type_subst, this);
        }
        mir_field.type = field_type;

        new_struct.fields.push_back(mir_field);
    }
}
```

### 修正案2: LLVM型登録の事前実行

```cpp
// src/codegen/llvm/core/mir_to_llvm.cpp
void MIRToLLVM::registerSpecializedStructs() {
    for (const auto& [name, struct_def] : mir_module.struct_defs) {
        // 特殊化された構造体を事前に登録
        if (name.find("__") != std::string::npos) {
            // マングリングされた名前（例: Node__int）
            registerStructType(name, struct_def);
        }
    }
}
```

### 修正案3: Store命令生成時の型チェック強化

```cpp
// src/codegen/llvm/core/mir_to_llvm.cpp
void MIRToLLVM::generateStore(llvm::Value* value, llvm::Value* ptr) {
    llvm::Type* valueType = value->getType();
    llvm::Type* ptrElemType = ptr->getType()->getPointerElementType();

    if (valueType != ptrElemType) {
        // 型不一致の詳細をログ出力
        debug_log("Type mismatch in store: value={}, ptr_elem={}",
                  getTypeName(valueType), getTypeName(ptrElemType));

        // 適切なキャストを試みる
        if (canBitCast(valueType, ptrElemType)) {
            value = builder.CreateBitCast(value, ptrElemType);
        } else {
            throw std::runtime_error("Cannot cast types in store");
        }
    }

    builder.CreateStore(value, ptr);
}
```

### 修正案4: MIRダンプによるデバッグ強化

```cpp
// デバッグ用のMIRダンプ機能を追加
void dumpMIRAfterMonomorphization(const mir::Module& module) {
    for (const auto& func : module.functions) {
        std::cerr << "Function: " << func.name << "\n";
        for (const auto& local : func.locals) {
            std::cerr << "  Local " << local.id << ": "
                      << typeToString(local.type) << "\n";
        }
    }

    for (const auto& [name, struct_def] : module.struct_defs) {
        std::cerr << "Struct: " << name << "\n";
        for (const auto& field : struct_def.fields) {
            std::cerr << "  Field " << field.name << ": "
                      << typeToString(field.type) << "\n";
        }
    }
}
```

## デバッグ手順

### 1. MIRダンプの有効化

```bash
# デバッグビルド
cmake -B build -DCM_USE_LLVM=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# MIRダンプを有効にして実行
./build/bin/cm --debug --dump-mir test.cm
```

### 2. 型情報のトレース

```cpp
// 各層で型情報をログ出力
#define TYPE_TRACE(msg, type) \
    std::cerr << "[" << __FUNCTION__ << "] " << msg << ": " \
              << typeToString(type) << " at " << __FILE__ \
              << ":" << __LINE__ << "\n"
```

### 3. LLVM IRの検証

```bash
# LLVM IRをファイルに出力
./build/bin/cm --emit-llvm test.cm > test.ll

# LLVM IRを検証
llvm-as test.ll -o test.bc
llvm-dis test.bc -o test_verified.ll

# 型エラーを確認
opt -verify test.bc
```

## 影響範囲

この問題は以下の機能に影響します：

1. **すべてのジェネリックコンテナ**
   - queue<T>
   - stack<T>
   - priority_queue<T>
   - vector<T>（将来実装）

2. **ジェネリック関数**
   - 構造体を引数に取るジェネリック関数
   - 構造体を返すジェネリック関数

3. **ネストしたジェネリクス**
   - Option<Node<T>>
   - Result<Vec<T>, Error>

## 回避策（暫定）

現時点での回避策：

1. **プリミティブ型のみ使用**
   - queue<int>、stack<double>等は動作

2. **手動でのモノモーフィゼーション**
   ```cm
   // ジェネリックを使わず、具体的な型で定義
   struct NodeInt {
       int data;
       NodeInt* next;
   }
   ```

3. **void*を使用した汎用実装**
   ```cm
   struct Node {
       void* data;
       Node* next;
   }
   ```

## 次のステップ

### 優先度1: 根本原因の修正
1. モノモーフィゼーション時の型置換を修正
2. LLVM型登録のタイミング修正
3. 包括的なテストスイート作成

### 優先度2: デバッグ基盤の強化
1. MIRダンプ機能の改善
2. 型情報トレース機能の追加
3. LLVM IR検証の自動化

### 優先度3: ドキュメント化
1. ジェネリクス実装の設計文書
2. 型システムの内部動作説明
3. トラブルシューティングガイド

## まとめ

ジェネリック構造体の型不一致問題は、**モノモーフィゼーション時の型置換不足**と**LLVM型登録の欠落**が主要因です。これらは設計レベルの問題であり、部分的な修正では解決困難です。

根本的な解決には：
1. モノモーフィゼーション処理の全面的な見直し
2. 型情報伝播メカニズムの再設計
3. LLVM IR生成プロセスの改善

が必要です。

---

**調査日:** 2026-01-10
**ステータス:** 未解決
**影響度:** 致命的（STL実装のブロッカー）