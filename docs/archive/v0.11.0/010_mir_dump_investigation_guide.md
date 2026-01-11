# MIRダンプによるジェネリック型伝播の詳細調査ガイド

作成日: 2026-01-10
対象バージョン: v0.11.0

## 概要

ジェネリック構造体の型不一致問題（008/009）を解決するため、MIR（Middle-level IR）ダンプを使用して型情報の伝播を詳細に調査する方法をまとめます。

## MIRダンプの実装と使用方法

### 1. MIRダンプ機能の実装

**ファイル:** `src/mir/printer.hpp`（既存）

```cpp
class MirPrinter {
public:
    void print(const mir::Module& module, std::ostream& out);
    void print(const mir::Function& func, std::ostream& out);
    void print(const mir::StructDef& struct_def, std::ostream& out);
};
```

### 2. デバッグフラグの追加

**ファイル:** `src/main.cpp`に追加

```cpp
// コマンドライン引数の解析部分
bool dump_mir = false;
bool dump_mir_after_mono = false;

for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--dump-mir") == 0) {
        dump_mir = true;
    } else if (strcmp(argv[i], "--dump-mir-mono") == 0) {
        dump_mir_after_mono = true;
    }
}
```

### 3. ダンプポイントの追加

```cpp
// MIR lowering後
if (dump_mir) {
    std::cerr << "=== MIR Before Monomorphization ===\n";
    MirPrinter printer;
    printer.print(mir_module, std::cerr);
}

// モノモーフィゼーション後
if (dump_mir_after_mono) {
    std::cerr << "=== MIR After Monomorphization ===\n";
    MirPrinter printer;
    printer.print(mir_module, std::cerr);
}
```

## ジェネリック型伝播の調査ポイント

### 調査ポイント1: 構造体定義の型情報

```
=== 調査対象 ===
struct Node<T> {
    T data;
    Node<T>* next;
}

=== 期待されるMIRダンプ ===
[モノモーフィゼーション前]
struct Node<T> {
    field data: T (generic)
    field next: Node<T>* (pointer to generic struct)
}

[モノモーフィゼーション後]
struct Node__int {
    field data: int (concrete)
    field next: Node__int* (pointer to concrete struct)
}
```

### 調査ポイント2: ローカル変数の型

```
=== 調査対象 ===
<T> void process(T value) {
    Node<T> node;
    node.data = value;
}

=== 期待されるMIRダンプ ===
[モノモーフィゼーション前]
function process<T>(_1: T) {
    let _2: Node<T>
    _2.data = _1
}

[モノモーフィゼーション後（T=Item）]
function process__Item(_1: Item) {
    let _2: Node__Item
    _2.data = _1  // 両方ともItem型であるべき
}
```

### 調査ポイント3: Store/Load命令の型

```
=== 調査対象 ===
node.data = item;

=== 期待されるMIRダンプ ===
[問題のある出力]
store i32 to Item*  // ❌ 型不一致

[正しい出力]
store Item to Item*  // ✅ 型一致
```

## デバッグ出力の追加方法

### 1. 型置換のトレース

```cpp
// src/mir/lowering/monomorphization_impl.cpp
hir::TypePtr substitute_type_in_type(
    const hir::TypePtr& type,
    const TypeSubstitutionMap& type_subst,
    MonomorphizationContext* ctx) {

    // デバッグ出力を追加
    std::cerr << "[TYPE_SUBST] Input: " << type_to_string(type) << "\n";

    // ... 置換処理 ...

    std::cerr << "[TYPE_SUBST] Output: " << type_to_string(result) << "\n";

    return result;
}
```

### 2. 構造体フィールドの型情報

```cpp
// generate_specialized_struct()内
for (auto& field : struct_def->fields) {
    std::cerr << "[STRUCT_FIELD] " << struct_name << "." << field.name
              << ": Before=" << type_to_string(field.type)
              << ", After=" << type_to_string(substituted_type) << "\n";
}
```

### 3. Store命令生成時の型情報

```cpp
// src/codegen/llvm/core/mir_to_llvm.cpp
void generateStore(llvm::Value* value, llvm::Value* ptr) {
    std::cerr << "[STORE] Value type: "
              << llvm_type_to_string(value->getType())
              << ", Ptr elem type: "
              << llvm_type_to_string(ptr->getType()->getPointerElementType())
              << "\n";
}
```

## 実行コマンド

### 基本的なMIRダンプ

```bash
# MIRダンプ（モノモーフィゼーション前）
./build/bin/cm --dump-mir test.cm 2>mir_before.txt

# MIRダンプ（モノモーフィゼーション後）
./build/bin/cm --dump-mir-mono test.cm 2>mir_after.txt

# 差分確認
diff mir_before.txt mir_after.txt
```

### 詳細なデバッグトレース

```bash
# すべてのデバッグ情報を有効化
CM_DEBUG=1 ./build/bin/cm --debug --dump-mir --dump-mir-mono test.cm 2>debug.log

# 型置換のみトレース
./build/bin/cm test.cm 2>&1 | grep "TYPE_SUBST"

# Store命令のみトレース
./build/bin/cm test.cm 2>&1 | grep "STORE"
```

## テストケースの作成

### 最小再現コード

```cm
// test_generic_struct.cm
struct Item {
    int value;
    int priority;
}

struct Node<T> {
    T data;
    Node<T>* next;
}

<T> Node<T>* create_node(T data) {
    void* mem = malloc(sizeof(Node<T>));
    Node<T>* node = mem as Node<T>*;
    node->data = data;  // ここで型不一致エラー
    node->next = null as Node<T>*;
    return node;
}

int main() {
    Item item = {value: 42, priority: 1};
    Node<Item>* node = create_node(item);
    return 0;
}
```

### 期待されるMIRダンプ（正常な場合）

```
=== MIR After Monomorphization ===
struct Item {
    field value: i32
    field priority: i32
}

struct Node__Item {
    field data: Item      // ✅ 正しく Item 型
    field next: Node__Item*
}

function create_node__Item(_1: Item) -> Node__Item* {
    bb0:
        _2 = malloc(16)           // sizeof(Node__Item) = 16
        _3 = bitcast _2 to Node__Item*
        _4 = &(*_3).data          // フィールドアクセス
        store _1 to _4            // Item to Item* ✅
        _5 = &(*_3).next
        _6 = null as Node__Item*
        store _6 to _5
        return _3
}
```

## 型情報の追跡チェックリスト

- [ ] ジェネリック構造体定義でTが正しく記録されているか
- [ ] モノモーフィゼーション時に型引数が正しく渡されているか
- [ ] `substitute_type_in_type()`が構造体型を正しく置換しているか
- [ ] フィールド型が具体型に置換されているか
- [ ] MIR構造体定義が正しく特殊化されているか
- [ ] LLVM型変換で特殊化構造体が登録されているか
- [ ] Store命令の両オペランドの型が一致しているか

## トラブルシューティング

### 問題1: MIRダンプが出力されない

```bash
# デバッグビルドを確認
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --clean-first
```

### 問題2: 型情報が不完全

```cpp
// 型情報の詳細出力を追加
std::string type_to_string_detailed(const hir::TypePtr& type) {
    std::stringstream ss;
    ss << "Type{kind=" << type->kind
       << ", name=" << type->name
       << ", is_ref=" << type->is_ref
       << ", type_args=[";
    for (const auto& arg : type->type_args) {
        ss << type_to_string_detailed(arg) << ", ";
    }
    ss << "]}";
    return ss.str();
}
```

### 問題3: モノモーフィゼーションがスキップされる

```cpp
// モノモーフィゼーションのトリガーを確認
std::cerr << "[MONO] Instantiating " << generic_func_name
          << " with types: ";
for (const auto& type : type_args) {
    std::cerr << type_to_string(type) << " ";
}
std::cerr << "\n";
```

## 次のステップ

1. **MIRダンプ機能の拡張**
   - 型情報の詳細表示
   - グラフィカルな表示（Graphviz出力）
   - JSON形式での出力

2. **自動テストの追加**
   - MIRダンプの期待値との比較
   - 型一致のアサーション
   - 回帰テスト

3. **修正の実装**
   - モノモーフィゼーションの型置換修正
   - LLVM型登録の改善
   - Store命令生成の型チェック強化

---

**作成日:** 2026-01-10
**目的:** MIRダンプを使用した型伝播の詳細調査
**次回作業:** このガイドに従ってMIRダンプを取得し、型不一致の根本原因を特定