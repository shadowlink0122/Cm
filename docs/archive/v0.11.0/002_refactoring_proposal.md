# Cm言語 STL実装のためのリファクタリング提案書

作成日: 2026-01-10
対象バージョン: v0.11.0

## 概要

本文書では、C++風STLコンテナ実装を可能にするための、Cm言語コンパイラと標準ライブラリの包括的なリファクタリング提案を行います。特に、現在のアーキテクチャの問題点を特定し、具体的な改善策とその実装方法を提示します。

## 1. コンパイラアーキテクチャのリファクタリング

### 1.1 MIR Lowering層の改修

#### 問題: selfメソッド変更の反映されないバグ

**現在のコード構造（推定）:**
```cpp
// src/mir/lowering/expr_call.cpp
Value* LoweringContext::lower_method_call(MethodCall* call) {
    // 問題のあるコード
    Value* self_value = lower_expr(call->self);
    Value* self_copy = emit_copy(self_value);      // ← ここでコピー
    Value* self_ref = emit_reference(self_copy);   // ← コピーの参照を取る

    // メソッド呼び出し
    return emit_call(method, self_ref, args);
}
```

**リファクタリング案:**
```cpp
// src/mir/lowering/expr_call.cpp
Value* LoweringContext::lower_method_call(MethodCall* call) {
    Value* self_value = lower_expr(call->self);

    // メソッドの mutability をチェック
    if (method->is_mutating()) {
        // 可変参照を直接取る
        Value* self_mut_ref = emit_mutable_reference(self_value);
        return emit_call(method, self_mut_ref, args);
    } else {
        // 不変参照の場合は現在の挙動を維持
        Value* self_ref = emit_reference(self_value);
        return emit_call(method, self_ref, args);
    }
}

// メソッドの mutability 判定を追加
bool Method::is_mutating() const {
    // self引数が可変参照かチェック
    return self_param && self_param->is_mutable;
}
```

**新しいMIR命令の追加:**
```cpp
// src/mir/instructions.hpp
class MutableBorrowInst : public Instruction {
    Value* source;
public:
    MutableBorrowInst(Value* src) : source(src) {}
    // 可変借用を表現
};
```

### 1.2 型システムの拡張

#### ジェネリック特殊化の改善

**現在の課題:** ネストしたジェネリック型の特殊化が複雑

**リファクタリング案:**

```cpp
// src/mir/lowering/monomorphization.hpp
class MonomorphizationContext {
private:
    // 特殊化キャッシュの改善
    struct SpecializationKey {
        FunctionId func_id;
        std::vector<TypeId> type_args;

        bool operator==(const SpecializationKey& other) const;
        size_t hash() const;
    };

    std::unordered_map<SpecializationKey, FunctionId> specialization_cache;

public:
    // より効率的な特殊化
    FunctionId specialize_generic_function(
        FunctionId generic_func,
        const std::vector<TypeId>& type_args
    ) {
        SpecializationKey key{generic_func, type_args};

        // キャッシュチェック
        if (auto it = specialization_cache.find(key);
            it != specialization_cache.end()) {
            return it->second;
        }

        // 新規特殊化
        FunctionId specialized = perform_specialization(generic_func, type_args);
        specialization_cache[key] = specialized;
        return specialized;
    }
};
```

### 1.3 メモリ管理の統合

#### スマートポインタサポートの追加

```cpp
// src/frontend/types/smart_ptr.hpp
class SmartPtrType : public Type {
public:
    enum Kind { Unique, Shared, Weak };

private:
    Kind kind;
    TypeId element_type;

public:
    SmartPtrType(Kind k, TypeId elem) : kind(k), element_type(elem) {}

    // 自動的にデストラクタを生成
    void generate_destructor(CodeGen& gen) {
        switch (kind) {
            case Unique:
                gen.emit_unique_ptr_destructor(element_type);
                break;
            case Shared:
                gen.emit_shared_ptr_destructor(element_type);
                break;
            // ...
        }
    }
};
```

## 2. 標準ライブラリアーキテクチャ

### 2.1 ディレクトリ構造の再編成

```
src/
├── compiler/           # コンパイラコード（既存を移動）
│   ├── frontend/
│   ├── mir/
│   └── codegen/
└── std/               # 標準ライブラリ（新規）
    ├── core/          # 基本機能
    │   ├── mem.cm     # メモリプリミティブ
    │   ├── ptr.cm     # ポインタユーティリティ
    │   └── types.cm   # 基本型定義
    ├── collections/   # コンテナ
    │   ├── mod.cm     # モジュール定義
    │   ├── traits.cm  # コンテナインターフェース
    │   ├── vec.cm     # Vector実装
    │   ├── map.cm     # HashMap実装
    │   └── set.cm     # HashSet実装
    ├── iter/          # イテレータ
    │   ├── traits.cm  # Iterator, IntoIterator
    │   └── adapters.cm # map, filter, fold等
    └── algorithm/     # アルゴリズム
        ├── sort.cm
        ├── search.cm
        └── numeric.cm
```

### 2.2 コアインターフェースの設計

```cm
// src/std/collections/traits.cm

// 基本コンテナインターフェース
interface Collection<T> {
    int len();
    bool is_empty();
    void clear();
}

// インデックスアクセス可能
interface IndexedCollection<T> for Collection<T> {
    T get(int index);
    void set(int index, T value);
}

// 成長可能なコンテナ
interface GrowableCollection<T> for Collection<T> {
    void push(T item);
    T pop();
    int capacity();
    void reserve(int additional);
}

// マップインターフェース
interface Map<K, V> for Collection<(K, V)> {
    void insert(K key, V value);
    V* get(K key);
    bool contains_key(K key);
    void remove(K key);
}

// イテレータインターフェース
interface Iterator<T> {
    T* next();
    int size_hint();  // オプショナルな最適化ヒント
}

interface IntoIterator<T> {
    Iterator<T> into_iter();
}
```

### 2.3 メモリアロケータの抽象化

```cm
// src/std/core/allocator.cm

// アロケータインターフェース
interface Allocator {
    void* allocate(ulong size, ulong align);
    void deallocate(void* ptr, ulong size, ulong align);
    void* reallocate(void* ptr, ulong old_size, ulong new_size, ulong align);
}

// デフォルトアロケータ
struct SystemAllocator { }

impl SystemAllocator for Allocator {
    void* allocate(ulong size, ulong align) {
        // POSIXシステムではaligned_alloc使用
        return aligned_alloc(align, size);
    }

    void deallocate(void* ptr, ulong size, ulong align) {
        free(ptr);
    }

    void* reallocate(void* ptr, ulong old_size, ulong new_size, ulong align) {
        void* new_ptr = self.allocate(new_size, align);
        memcpy(new_ptr, ptr, min(old_size, new_size));
        self.deallocate(ptr, old_size, align);
        return new_ptr;
    }
}

// アロケータを使用するコンテナ
struct Vec<T, A: Allocator = SystemAllocator> {
    T* data;
    int len;
    int capacity;
    A allocator;
}
```

## 3. 最適化戦略

### 3.1 インライン展開の改善

```cpp
// src/mir/optimization/inlining.hpp
class InliningOptimizer : public MIRPass {
private:
    struct InliningMetrics {
        size_t instruction_count;
        bool has_loops;
        bool is_recursive;
        int call_depth;
    };

    bool should_inline(const Function& func) {
        InliningMetrics metrics = analyze_function(func);

        // STLコンテナメソッドは積極的にインライン化
        if (func.is_stl_container_method()) {
            return metrics.instruction_count < 50 && !metrics.is_recursive;
        }

        // 通常の関数
        return metrics.instruction_count < 20;
    }

public:
    void optimize(MIRModule& module) override {
        // コンテナのget/setメソッドは常にインライン化
        for (auto& func : module.functions) {
            if (is_container_accessor(func)) {
                func.force_inline = true;
            }
        }
    }
};
```

### 3.2 メモリレイアウトの最適化

```cm
// src/std/collections/vec_optimized.cm

// Small Vector Optimization (SVO)
struct SmallVec<T, const int N = 8> {
    union Storage {
        T inline_data[N];  // スタック上のインライン格納
        struct {
            T* heap_data;
            int heap_capacity;
        };
    }

    Storage storage;
    int len;
    bool is_inline;

    // 小さいデータはヒープ割り当てを回避
}

impl<T, const int N> SmallVec<T, N> {
    void push(T item) {
        if (self.is_inline && self.len < N) {
            // インライン格納
            self.storage.inline_data[self.len] = item;
        } else {
            // ヒープへ移行
            self.move_to_heap();
            self.storage.heap_data[self.len] = item;
        }
        self.len = self.len + 1;
    }

    private void move_to_heap() {
        if (self.is_inline) {
            int new_cap = max(N * 2, 16);
            T* heap = allocate<T>(new_cap);

            // インラインデータをコピー
            for (int i = 0; i < self.len; i++) {
                heap[i] = self.storage.inline_data[i];
            }

            self.storage.heap_data = heap;
            self.storage.heap_capacity = new_cap;
            self.is_inline = false;
        }
    }
}
```

## 4. エラー処理の改善

### 4.1 Result型の導入

```cm
// src/std/core/result.cm

enum Result<T, E> {
    Ok(T),
    Err(E)
}

impl<T, E> Result<T, E> {
    bool is_ok() {
        match self {
            Ok(_) => true,
            Err(_) => false
        }
    }

    T unwrap() {
        match self {
            Ok(val) => val,
            Err(e) => panic("Called unwrap on Err: {}", e)
        }
    }

    T unwrap_or(T default) {
        match self {
            Ok(val) => val,
            Err(_) => default
        }
    }
}

// 使用例
impl<T> Vec<T> {
    Result<T, string> try_pop() {
        if (self.len == 0) {
            return Err("Vector is empty");
        }
        self.len = self.len - 1;
        return Ok(self.data[self.len]);
    }
}
```

### 4.2 パニックハンドラの統合

```cm
// src/std/core/panic.cm

struct PanicInfo {
    string message;
    string file;
    int line;
}

// グローバルパニックハンドラ
static void (*panic_handler)(PanicInfo info) = default_panic_handler;

void set_panic_handler(void (*handler)(PanicInfo)) {
    panic_handler = handler;
}

void panic(string msg) {
    PanicInfo info = {
        .message = msg,
        .file = __FILE__,
        .line = __LINE__
    };
    panic_handler(info);
    abort();  // 終了
}

void default_panic_handler(PanicInfo info) {
    eprintln("panic at {}:{}: {}", info.file, info.line, info.message);
    print_backtrace();
}
```

## 5. テスト基盤の構築

### 5.1 単体テストフレームワーク

```cm
// src/std/test/framework.cm

struct TestCase {
    string name;
    void (*test_fn)();
    bool should_panic;
}

struct TestRunner {
    Vec<TestCase> tests;
    int passed;
    int failed;
}

impl TestRunner {
    void register_test(string name, void (*test_fn)()) {
        self.tests.push(TestCase{
            .name = name,
            .test_fn = test_fn,
            .should_panic = false
        });
    }

    void run_all() {
        for (int i = 0; i < self.tests.len(); i++) {
            TestCase test = self.tests.get(i);
            print("Running test: {} ... ", test.name);

            bool success = self.run_single_test(test);
            if (success) {
                println("[32mPASSED[0m");
                self.passed = self.passed + 1;
            } else {
                println("[31mFAILED[0m");
                self.failed = self.failed + 1;
            }
        }

        println("\nTest results: {} passed, {} failed",
                self.passed, self.failed);

        if (self.failed > 0) {
            exit(1);
        }
    }

    private bool run_single_test(TestCase test) {
        // フォークして子プロセスでテスト実行（クラッシュ対策）
        int pid = fork();
        if (pid == 0) {
            // 子プロセス
            test.test_fn();
            exit(0);
        } else {
            // 親プロセス
            int status;
            wait(&status);
            return status == 0;
        }
    }
}

// テストマクロ
#define TEST(name) \
    void test_##name(); \
    static int _test_##name = test_runner.register_test(#name, test_##name); \
    void test_##name()

#define ASSERT_EQ(a, b) \
    if ((a) != (b)) { \
        panic("Assertion failed: {} != {} at {}:{}", #a, #b, __FILE__, __LINE__); \
    }
```

### 5.2 パフォーマンステスト

```cm
// src/std/test/benchmark.cm

struct BenchmarkResult {
    string name;
    double min_time;
    double max_time;
    double avg_time;
    double std_dev;
    int iterations;
}

struct Benchmark {
    string name;
    void (*bench_fn)();
    int warmup_iters;
    int bench_iters;
}

impl Benchmark {
    BenchmarkResult run() {
        // ウォームアップ
        for (int i = 0; i < self.warmup_iters; i++) {
            self.bench_fn();
        }

        Vec<double> times;
        for (int i = 0; i < self.bench_iters; i++) {
            double start = get_time_ns();
            self.bench_fn();
            double end = get_time_ns();
            times.push((end - start) / 1000000.0);  // ミリ秒に変換
        }

        return BenchmarkResult{
            .name = self.name,
            .min_time = times.min(),
            .max_time = times.max(),
            .avg_time = times.average(),
            .std_dev = times.std_deviation(),
            .iterations = self.bench_iters
        };
    }
}

// 使用例
void bench_vector_push() {
    Vec<int> v;
    for (int i = 0; i < 10000; i++) {
        v.push(i);
    }
}

int main() {
    Benchmark bench = {
        .name = "Vector::push 10k items",
        .bench_fn = bench_vector_push,
        .warmup_iters = 10,
        .bench_iters = 100
    };

    BenchmarkResult result = bench.run();
    println("Benchmark: {}", result.name);
    println("  Average: {:.2f} ms", result.avg_time);
    println("  Min: {:.2f} ms, Max: {:.2f} ms", result.min_time, result.max_time);
}
```

## 6. 段階的移行計画

### フェーズ1: 基盤修正（Week 1-2）
```
1. selfメソッドバグの修正
   - MIR lowering層の変更
   - テストケースの追加
   - 回帰テストの実行

2. 基本的なメモリ管理
   - allocator.cmの実装
   - メモリユーティリティ関数

3. テストフレームワークの導入
```

### フェーズ2: コンテナ実装（Week 3-4）
```
1. Vector<T>の実装
   - 基本機能
   - Small Vector Optimization
   - 包括的テスト

2. HashMap<K, V>の実装
   - チェイン法ベース
   - 再ハッシュ機能

3. イテレータ基盤の構築
```

### フェーズ3: 高度な機能（Week 5-6）
```
1. Result/Option型
2. スマートポインタ
3. アルゴリズムライブラリ
4. 並行コンテナ（オプション）
```

## 7. 互換性とマイグレーション

### 7.1 既存コードへの影響

```cm
// 既存コード（変更不要）
struct MyStruct {
    int value;
}

impl MyStruct {
    self() { self.value = 0; }
}

// 新しいSTL使用（追加）
int main() {
    Vec<MyStruct> vec;  // 新機能
    vec.push(MyStruct());

    MyStruct s;  // 既存も動作
    s.value = 42;
}
```

### 7.2 マイグレーションガイド

```cm
// Before (手動メモリ管理)
struct Node {
    int value;
    Node* next;
}

Node* list = malloc(sizeof(Node)) as Node*;
// ... 使用
free(list as void*);

// After (STLコンテナ使用)
Vec<int> list;
list.push(value);
// 自動的にメモリ管理される
```

## 8. まとめ

本リファクタリング提案は、Cm言語にC++風のSTLコンテナを導入するための包括的な計画です。主要な変更点は：

1. **コンパイラの修正**: selfメソッドバグの解決が最優先
2. **標準ライブラリの構築**: モジュラーで拡張可能な設計
3. **最適化の導入**: SVO、インライン化等
4. **テスト基盤**: 品質保証のための包括的テスト

これらの変更により、Cm言語はC++のパワーとRustの安全性を併せ持つモダンなシステムプログラミング言語として進化します。

---

**付録**: 詳細な実装コードは各フェーズの実装時に別途提供します。