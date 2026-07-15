# ジェネリクス構造体のLoad/Store問題分析と改善提案

作成日: 2026-01-10更新日: 2026-01-10（queue<T>での構造体問題を追加）対象バージョン: v0.11.0

## エグゼクティブサマリー

Cm言語でジェネリクスTに構造体を使用した際の問題について調査しました。当初は基本的な機能は動作していると考えられましたが、**queue<T>などのコンテナに構造体を入れた場合、`sizeof(T)`が正しく計算されずメモリオーバーフローが発生する致命的な問題**を発見しました。

## 🔴 致命的問題：queue<T>での構造体サポート

### 問題の詳細

**queue<T>、stack<T>、priority_queue<T>などのジェネリックコンテナに構造体を入れると動作しない**

```cm
struct Item {
    int value;      // 4バイト
    int priority;   // 4バイト
}  // 実際のサイズ = 8バイト

struct Node<T> {
    T data;         // 8バイト（Itemの場合）
    Node<T>* next;  // 8バイト
}  // 実際のサイズ = 16バイト

<T> Node<T>* new_node(T data) {
    void* mem = malloc(sizeof(Node<T>));  // ❌ 8バイトしか確保されない！
    Node<T>* node = mem as Node<T>*;
    node->data = data;  // ❌ メモリオーバーフロー！
    node->next = null;  // ❌ 範囲外メモリへの書き込み！
    return node;
}

int main() {
    Item item = {value: 42, priority: 1};
    Node<Item>* node = new_node(item);  // クラッシュまたは未定義動作
}
```

### 根本原因

**ファイル：** `src/hir/lowering/impl.cpp:486-599`

```cpp
int64_t HirLowering::calculate_type_size(const TypePtr& type) {
    switch (type->kind) {
        case ast::TypeKind::Struct: {
            auto it = struct_defs_.find(type->name);  // ❌ "Node<T>" を検索
            // struct_defs_には "Node" として登録されているため見つからない
            if (it != struct_defs_.end()) {
                // ...
            }
            return 8;  // ❌ デフォルトで8バイトを返す
        }

        // ❌ TypeKind::Generic のケースが存在しない！
        default:
            return 8;  // ジェネリック型は常に8バイトになる
    }
}
```

**問題のメカニズム：**

1. **HIR段階**：`sizeof(Node<T>)` が呼ばれる
2. **型の種類**：`type->kind = TypeKind::Generic`（まだ具体化されていない）
3. **構造体検索**：`struct_defs_.find("Node<T>")` → 見つからない（"Node"として登録）
4. **結果**：デフォルトの8バイトを返す
5. **実行時**：`malloc(8)` で8バイトしか確保されない
6. **メモリ破壊**：16バイト必要な構造体に8バイトしか割り当てられない

### 影響範囲

すべてのジェネリックコンテナ実装が影響を受けます：
- `examples/05_data_structures/priority_queue_generic.cm`
- `tests/test_programs/generics/test_pqueue_simple.cm`
- ユーザー定義のqueue、stack、list等

## 🟡 既知の問題：Aggregate Rvalue処理

### 問題の詳細

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

現在は構造体リテラルがフィールド単位代入で生成されるため問題になっていませんが、将来のMIR最適化で問題となる可能性があります。

## 修正提案

### 優先度1：sizeof(T)計算の修正（致命的）

**修正ファイル：** `src/hir/lowering/impl.cpp`

```cpp
int64_t HirLowering::calculate_type_size(const TypePtr& type) {
    switch (type->kind) {
        // 新規追加：ジェネリック型のケース
        case ast::TypeKind::Generic: {
            // Node<Item> のような具体化されたジェネリック型
            if (!type->type_args.empty()) {
                std::string base_name = extract_base_name(type->name);
                auto it = struct_defs_.find(base_name);
                if (it != struct_defs_.end()) {
                    return calculate_generic_struct_size(
                        it->second, type->type_args);
                }
            }
            // T単体のようなジェネリック型パラメータ
            // モノモーフィゼーション前なので実際のサイズは不明
            // 最大サイズを仮定するか、エラーを報告すべき
            return 8;  // 暫定的にポインタサイズ
        }

        case ast::TypeKind::Struct: {
            // "Node<Item>" 形式のジェネリック構造体サポート
            std::string lookup_name = type->name;
            size_t bracket_pos = lookup_name.find('<');

            if (bracket_pos != std::string::npos) {
                // ジェネリック構造体
                std::string base_name = lookup_name.substr(0, bracket_pos);
                auto it = struct_defs_.find(base_name);

                if (it != struct_defs_.end() && !type->type_args.empty()) {
                    return calculate_generic_struct_size(
                        it->second, type->type_args);
                }
            }

            // 通常の構造体
            auto it = struct_defs_.find(lookup_name);
            if (it != struct_defs_.end()) {
                auto [size, align] = calculate_struct_layout(it->second->fields);
                return size;
            }

            // 構造体が見つからない場合はエラー
            throw std::runtime_error(
                "Unknown struct type for sizeof: " + lookup_name);
        }

        // ... 他のケース
    }
}

// 新規ヘルパー関数
int64_t HirLowering::calculate_generic_struct_size(
    const ast::StructDef* struct_def,
    const std::vector<TypePtr>& type_args) {

    // 型パラメータを型引数で置換
    std::unordered_map<std::string, TypePtr> type_map;
    for (size_t i = 0; i < struct_def->generic_params.size() &&
         i < type_args.size(); ++i) {
        type_map[struct_def->generic_params[i].name] = type_args[i];
    }

    // 各フィールドのサイズを計算
    int64_t total_size = 0;
    int64_t max_align = 1;

    for (const auto& field : struct_def->fields) {
        TypePtr field_type = substitute_type(field.type, type_map);
        int64_t field_size = calculate_type_size(field_type);
        int64_t field_align = calculate_type_align(field_type);

        // アラインメント調整
        total_size = align_to(total_size, field_align);
        total_size += field_size;

        max_align = std::max(max_align, field_align);
    }

    // 構造体全体のアラインメント
    total_size = align_to(total_size, max_align);

    return total_size;
}
```

### 優先度2：Aggregate Rvalue処理の実装

**修正ファイル：** `src/codegen/llvm/core/mir_to_llvm.cpp`

```cpp
case mir::MirRvalue::Aggregate: {
    auto& aggData = std::get<mir::MirRvalue::AggregateData>(rvalue.data);

    if (aggData.kind.type == mir::AggregateKind::Struct) {
        // 構造体型を取得
        std::string structName = mangleStructName(
            aggData.kind.name, aggData.kind.type_args);
        auto* structType = structTypes[structName];

        if (!structType) {
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
            return alloca;
        }
    }

    return nullptr;
}
```

### 優先度3：大きな構造体のmemcpy最適化

```cpp
void copyStruct(llvm::Value* dst, llvm::Value* src, llvm::Type* structType) {
    uint64_t size = dataLayout.getTypeAllocSize(structType);

    if (size >= 64) {  // 64バイト以上
        // memcpyを使用
        builder->CreateMemCpy(dst, llvm::MaybeAlign(8),
                             src, llvm::MaybeAlign(8),
                             size);
    } else {
        // フィールド単位のコピー
        copyFieldByField(dst, src, structType);
    }
}
```

## テストケース

### 構造体を使用するqueue<T>のテスト

```cm
// tests/test_programs/generics/queue_struct_test.cm
struct Person {
    string name;
    int age;
    double height;
}

struct Queue<T> {
    struct Node<T> {
        T data;
        Node<T>* next;
    }

    Node<T>* front;
    Node<T>* rear;
    int size;
}

impl<T> Queue<T> {
    self() {
        self.front = null as Node<T>*;
        self.rear = null as Node<T>*;
        self.size = 0;
    }

    void enqueue(T item) {
        void* mem = malloc(sizeof(Node<T>));  // ここが問題
        Node<T>* new_node = mem as Node<T>*;
        new_node->data = item;
        new_node->next = null as Node<T>*;

        if (self.rear == null as Node<T>*) {
            self.front = new_node;
            self.rear = new_node;
        } else {
            self.rear->next = new_node;
            self.rear = new_node;
        }
        self.size = self.size + 1;
    }

    T dequeue() {
        assert(self.size > 0, "Queue is empty");

        T data = self.front->data;
        Node<T>* temp = self.front;
        self.front = self.front->next;

        if (self.front == null as Node<T>*) {
            self.rear = null as Node<T>*;
        }

        free(temp as void*);
        self.size = self.size - 1;
        return data;
    }
}

int main() {
    Queue<Person> queue;

    Person p1 = {name: "Alice", age: 30, height: 1.65};
    Person p2 = {name: "Bob", age: 25, height: 1.80};

    queue.enqueue(p1);
    queue.enqueue(p2);

    Person p = queue.dequeue();
    assert(p.name == "Alice");
    assert(p.age == 30);

    p = queue.dequeue();
    assert(p.name == "Bob");
    assert(p.age == 25);

    return 0;
}
```

## 実装優先順位

1. **即座（1-2日）**: sizeof(T)計算の修正 ← **最優先、メモリ破壊を防ぐ**
2. **短期（3-5日）**: モノモーフィゼーション時のsizeof再計算
3. **中期（1週間）**: Aggregate Rvalue処理の実装
4. **長期（2週間）**: 大きな構造体の最適化

## 期待される効果

### sizeof修正後
- queue<構造体>が正しく動作
- メモリオーバーフローの防止
- セグメンテーションフォルトの解消

### 全修正適用後
- C++のSTLと同等の型安全性
- 任意の型でのコンテナ使用が可能
- パフォーマンスの向上（memcpy最適化）

## まとめ

**現在の最大の問題は、ジェネリックコンテナで`sizeof(T)`が構造体の実際のサイズを返さないことです。** これによりメモリ割り当てが不足し、メモリ破壊やクラッシュが発生します。この問題は即座に修正が必要です。

Aggregate Rvalue処理や大きな構造体の最適化は、将来的な改善事項として重要ですが、まずはsizeof問題を解決することが最優先です。

---

**調査完了:** 2026-01-10 **更新内容:** queue<T>での構造体問題を追加**次のステップ:** sizeof(T)計算の修正実装