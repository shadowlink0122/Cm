# ジェネリック構造体LLVM型登録問題の修正案

作成日: 2026-01-11対象バージョン: v0.11.0修正ステータス: 🔧 実装待ち

## 問題の詳細

### エラーメッセージ
```
Assertion failed: Invalid GetElementPtrInst indices for type!
```

### 根本原因

ジェネリック構造体の特殊化版（例: `Node__Item`）がLLVM型として正しく登録されていない：

1. **MIRレベル**: モノモーフィゼーションで `Node__Item` が正しく生成される
2. **LLVMレベル**: `structTypes` マップに `Node__Item` が登録されない
3. **結果**: 不透明型（opaque type）として扱われ、フィールドアクセスでアサーションエラー

## 問題のコードフロー

### 1. MIR生成（正常）
`src/mir/lowering/monomorphization_impl.cpp:1258`
```cpp
// 特殊化構造体を正しくMIRプログラムに追加
program.structs.push_back(std::move(mir_struct));  // Node__Item が追加される
```

### 2. LLVM型登録（問題箇所）
`src/codegen/llvm/core/mir_to_llvm.cpp:494-496`
```cpp
// 現在のコード（問題あり）
for (const auto& structPtr : program.structs) {
    const std::string& name = structPtr->name;
    auto structType = llvm::StructType::create(ctx.getContext(), name);
    structTypes[name] = structType;  // Node__Item も登録される
}
```

### 3. 型変換時の検索失敗
`src/codegen/llvm/core/types.cpp:133-147`
```cpp
// Node<Item> の変換時
std::string lookupName = "Node__Item";  // マングリングされた名前
auto it = structTypes.find(lookupName);
if (it != structTypes.end()) {
    return it->second;
}
// 見つからない場合は不透明型として扱う（問題！）
return llvm::StructType::create(ctx.getContext(), lookupName);
```

## 修正方法

### 方法1: 型変換の改善（推奨）

**ファイル:** `src/codegen/llvm/core/types.cpp`

```cpp
case hir::TypeKind::Struct: {
    // インターフェース型チェック（既存）
    if (isInterfaceType(type->name)) {
        // ... 既存のコード ...
    }

    std::string lookupName = type->name;

    // ジェネリック構造体のマングリング
    if (!type->type_args.empty() && lookupName.find("__") == std::string::npos) {
        // ... 既存のマングリングコード ...
    }

    // 構造体型を検索
    auto it = structTypes.find(lookupName);
    if (it != structTypes.end()) {
        return it->second;
    }

    // フォールバック: 元の名前でも検索
    if (lookupName != type->name) {
        auto it2 = structTypes.find(type->name);
        if (it2 != structTypes.end()) {
            return it2->second;
        }
    }

    // ✅ 修正: 特殊化構造体が見つからない場合、structDefsも確認
    auto defIt = structDefs.find(lookupName);
    if (defIt != structDefs.end()) {
        // 構造体定義が存在する場合、LLVM型を作成して登録
        auto structType = llvm::StructType::create(ctx.getContext(), lookupName);
        structTypes[lookupName] = structType;

        // フィールド型を設定
        std::vector<llvm::Type*> fieldTypes;
        for (const auto& field : defIt->second->fields) {
            fieldTypes.push_back(convertType(field.type));
        }
        structType->setBody(fieldTypes);

        return structType;
    }

    // エラーログを追加
    std::cerr << "[LLVM] WARNING: Struct type not found: " << lookupName << "\n";
    std::cerr << "       Available types: ";
    for (const auto& [name, _] : structTypes) {
        std::cerr << name << " ";
    }
    std::cerr << "\n";

    // 不透明型として扱う（最終手段）
    return llvm::StructType::create(ctx.getContext(), lookupName);
}
```

### 方法2: 構造体登録の改善

**ファイル:** `src/codegen/llvm/core/mir_to_llvm.cpp`

```cpp
void MIRToLLVM::convert(const mir::MirProgram& program) {
    // ... 既存のコード ...

    // パス1: 構造体型を作成（opaque型として）
    for (const auto& structPtr : program.structs) {
        const std::string& name = structPtr->name;
        structDefs[name] = structPtr.get();

        // LLVM構造体型を作成
        auto structType = llvm::StructType::create(ctx.getContext(), name);
        structTypes[name] = structType;

        // ✅ 追加: ジェネリック特殊化のエイリアスも登録
        // Node__Item -> Node<Item> のようなマッピング
        if (name.find("__") != std::string::npos) {
            // デバッグログ
            std::cerr << "[LLVM] Registering specialized struct: " << name << "\n";
        }
    }

    // パス2: フィールド型を設定（既存のまま）
    // ...
}
```

## テストケース

```cm
// ジェネリック構造体定義
<T> struct Node {
    T value;
    Node<T>* next;
}

struct Item {
    int data;
}

int main() {
    Node<Item> node;  // Node__Item が生成される
    node.value.data = 42;  // ここでGetElementPtrエラー
    return 0;
}
```

## 修正の効果

1. **エラー解消**: GetElementPtrアサーションが発生しない
2. **型安全性**: 特殊化構造体のフィールドに正しくアクセス可能
3. **デバッグ性**: 型が見つからない場合に詳細なログ出力

## 実装優先度

🔴 **緊急** - ジェネリック構造体を使用するプログラムがコンパイルできない

## 関連する問題

- sizeof計算の問題（`src/hir/lowering/impl.cpp:596-608`）
- モノモーフィゼーション後の型情報伝搬

## 次のステップ

1. 上記修正を実装
2. テストケースで動作確認
3. 既存のテストスイートで回帰確認
4. sizeof計算の修正に着手

---

**作成者:** Claude Code **レビュー状況:** 未レビュー