# Cm言語 STL実装ロードマップ

作成日: 2026-01-10
対象: v0.11.0

## エグゼクティブサマリー

本文書は、Cm言語にC++風STLコンテナを実装するための具体的なロードマップです。6週間の開発期間で、完全に機能するSTLライブラリを構築し、既存のCmプロジェクトとシームレスに統合します。

## Week 1: 基盤修正とVector実装

### Day 1-2: selfメソッドバグの修正

#### タスク詳細
```
優先度: P0 (Critical Blocker)
担当: コンパイラチーム
```

**修正対象ファイル:**
- `src/mir/lowering/expr_call.cpp`
- `src/mir/lowering/stmt.cpp`
- `src/mir/instructions.hpp`

**修正内容:**
```cpp
// src/mir/lowering/expr_call.cpp の修正
Value* LoweringContext::lower_method_call(MethodCall* call) {
    // Before (バグあり)
    // Value* self_copy = emit_copy(self_value);
    // Value* self_ref = emit_reference(self_copy);

    // After (修正版)
    Value* self_ref = call->method->is_mutating()
        ? emit_mutable_reference(self_value)
        : emit_reference(self_value);

    return emit_call(call->method, self_ref, args);
}
```

**テストケース:**
```cm
// tests/regression/self_mutation_fix.cm
struct Counter {
    int count;
}

impl Counter {
    void increment() {
        self.count = self.count + 1;
    }

    void add(int n) {
        self.count = self.count + n;
    }
}

int main() {
    Counter c;
    c.count = 0;

    c.increment();
    assert(c.count == 1, "increment failed");

    c.add(5);
    assert(c.count == 6, "add failed");

    println("✓ All self mutation tests passed");
    return 0;
}
```

### Day 3-4: メモリ管理基盤

**実装ファイル:**
```cm
// src/std/core/memory.cm
use libc {
    void* malloc(ulong size);
    void* calloc(ulong num, ulong size);
    void* realloc(void* ptr, ulong size);
    void free(void* ptr);
    void* memcpy(void* dst, void* src, ulong n);
    void* memmove(void* dst, void* src, ulong n);
    void* memset(void* ptr, int value, ulong n);
}

// 型安全なアロケーション
<T> T* allocate(int count) {
    if (count <= 0) {
        return null as T*;
    }

    ulong size = sizeof(T) * count as ulong;
    void* mem = calloc(count as ulong, sizeof(T) as ulong);

    if (mem == null as void*) {
        panic("Failed to allocate memory for {} items of type {}",
              count, type_name<T>());
    }

    return mem as T*;
}

<T> void deallocate(T* ptr) {
    if (ptr != null as T*) {
        free(ptr as void*);
    }
}

<T> T* reallocate(T* ptr, int old_count, int new_count) {
    if (new_count <= 0) {
        deallocate(ptr);
        return null as T*;
    }

    ulong new_size = sizeof(T) * new_count as ulong;
    void* new_mem = realloc(ptr as void*, new_size);

    if (new_mem == null as void*) {
        panic("Failed to reallocate memory");
    }

    return new_mem as T*;
}

// メモリコピーユーティリティ
<T> void copy_memory(T* dst, T* src, int count) {
    if (dst == null as T* || src == null as T* || count <= 0) {
        return;
    }

    ulong size = sizeof(T) * count as ulong;
    memcpy(dst as void*, src as void*, size);
}

<T> void move_memory(T* dst, T* src, int count) {
    if (dst == null as T* || src == null as T* || count <= 0) {
        return;
    }

    ulong size = sizeof(T) * count as ulong;
    memmove(dst as void*, src as void*, size);
}
```

### Day 5: Vector<T> 基本実装

**実装ファイル:**
```cm
// src/std/collections/vector.cm
use std::core::memory;

struct Vec<T> {
    T* data;
    int len;
    int capacity;
}

// コンストラクタ/デストラクタ
impl<T> Vec<T> {
    self() {
        self.data = null as T*;
        self.len = 0;
        self.capacity = 0;
    }

    overload self(int initial_capacity) {
        if (initial_capacity <= 0) {
            self.data = null as T*;
            self.len = 0;
            self.capacity = 0;
        } else {
            self.data = allocate<T>(initial_capacity);
            self.len = 0;
            self.capacity = initial_capacity;
        }
    }

    ~self() {
        self.clear();
        deallocate(self.data);
    }
}

// 基本操作
impl<T> Vec<T> {
    void push(T item) {
        if (self.len >= self.capacity) {
            self.grow();
        }

        self.data[self.len] = item;
        self.len = self.len + 1;
    }

    T pop() {
        if (self.len == 0) {
            panic("pop from empty vector");
        }

        self.len = self.len - 1;
        return self.data[self.len];
    }

    T get(int index) {
        if (index < 0 || index >= self.len) {
            panic("index {} out of bounds for vector of length {}",
                  index, self.len);
        }
        return self.data[index];
    }

    void set(int index, T value) {
        if (index < 0 || index >= self.len) {
            panic("index {} out of bounds for vector of length {}",
                  index, self.len);
        }
        self.data[index] = value;
    }

    int len() { return self.len; }
    int capacity() { return self.capacity; }
    bool is_empty() { return self.len == 0; }

    void clear() {
        // TODO: デストラクタ呼び出し（T型にデストラクタがある場合）
        self.len = 0;
    }

    void reserve(int additional) {
        int required = self.len + additional;
        if (required > self.capacity) {
            self.grow_to(required);
        }
    }

    private void grow() {
        int new_cap = self.capacity * 2;
        if (new_cap == 0) {
            new_cap = 8;
        }
        self.grow_to(new_cap);
    }

    private void grow_to(int new_capacity) {
        if (new_capacity <= self.capacity) {
            return;
        }

        T* new_data = allocate<T>(new_capacity);

        if (self.data != null as T*) {
            copy_memory(new_data, self.data, self.len);
            deallocate(self.data);
        }

        self.data = new_data;
        self.capacity = new_capacity;
    }
}

// 便利メソッド
impl<T> Vec<T> {
    bool contains(T item) where T: Eq {
        for (int i = 0; i < self.len; i++) {
            if (self.data[i] == item) {
                return true;
            }
        }
        return false;
    }

    int index_of(T item) where T: Eq {
        for (int i = 0; i < self.len; i++) {
            if (self.data[i] == item) {
                return i;
            }
        }
        return -1;
    }

    void insert(int index, T item) {
        if (index < 0 || index > self.len) {
            panic("insert index {} out of bounds", index);
        }

        if (self.len >= self.capacity) {
            self.grow();
        }

        // 後ろの要素をシフト
        for (int i = self.len; i > index; i--) {
            self.data[i] = self.data[i - 1];
        }

        self.data[index] = item;
        self.len = self.len + 1;
    }

    T remove(int index) {
        if (index < 0 || index >= self.len) {
            panic("remove index {} out of bounds", index);
        }

        T item = self.data[index];

        // 前に詰める
        for (int i = index; i < self.len - 1; i++) {
            self.data[i] = self.data[i + 1];
        }

        self.len = self.len - 1;
        return item;
    }
}
```

## Week 2: HashMap実装とテスト基盤

### Day 6-7: ハッシュ基盤とHashMap

**ハッシュインターフェース:**
```cm
// src/std/core/hash.cm
interface Hash {
    ulong hash();
}

// 基本型のHash実装
impl int for Hash {
    ulong hash() {
        // FNV-1a hash
        ulong h = 14695981039346656037;
        h = h ^ (self as ulong);
        h = h * 1099511628211;
        return h;
    }
}

impl string for Hash {
    ulong hash() {
        ulong h = 14695981039346656037;
        for (int i = 0; i < self.len(); i++) {
            h = h ^ (self[i] as ulong);
            h = h * 1099511628211;
        }
        return h;
    }
}
```

**HashMap実装:**
```cm
// src/std/collections/hashmap.cm
use std::core::hash::Hash;

struct Entry<K, V> {
    K key;
    V value;
    Entry<K, V>* next;
}

struct HashMap<K, V> {
    Entry<K, V>** buckets;
    int bucket_count;
    int size;
}

impl<K, V> HashMap<K, V> {
    self() {
        self.bucket_count = 16;
        self.buckets = allocate<Entry<K, V>*>(self.bucket_count);
        self.size = 0;

        // バケットを初期化
        for (int i = 0; i < self.bucket_count; i++) {
            self.buckets[i] = null as Entry<K, V>*;
        }
    }

    ~self() {
        self.clear();
        deallocate(self.buckets);
    }
}

impl<K: Hash + Eq, V> HashMap<K, V> {
    void put(K key, V value) {
        int index = (key.hash() % self.bucket_count as ulong) as int;
        Entry<K, V>* entry = self.buckets[index];

        // 既存のキーを探す
        while (entry != null as Entry<K, V>*) {
            if (entry->key == key) {
                entry->value = value;
                return;
            }
            entry = entry->next;
        }

        // 新しいエントリを追加
        Entry<K, V>* new_entry = allocate<Entry<K, V>>(1);
        new_entry->key = key;
        new_entry->value = value;
        new_entry->next = self.buckets[index];
        self.buckets[index] = new_entry;
        self.size = self.size + 1;

        // リハッシュが必要かチェック
        if (self.size > self.bucket_count * 3 / 4) {
            self.rehash();
        }
    }

    V* get(K key) {
        int index = (key.hash() % self.bucket_count as ulong) as int;
        Entry<K, V>* entry = self.buckets[index];

        while (entry != null as Entry<K, V>*) {
            if (entry->key == key) {
                return &entry->value;
            }
            entry = entry->next;
        }

        return null as V*;
    }

    bool contains(K key) {
        return self.get(key) != null as V*;
    }

    void remove(K key) {
        int index = (key.hash() % self.bucket_count as ulong) as int;
        Entry<K, V>* entry = self.buckets[index];
        Entry<K, V>* prev = null as Entry<K, V>*;

        while (entry != null as Entry<K, V>*) {
            if (entry->key == key) {
                if (prev == null as Entry<K, V>*) {
                    self.buckets[index] = entry->next;
                } else {
                    prev->next = entry->next;
                }
                deallocate(entry);
                self.size = self.size - 1;
                return;
            }
            prev = entry;
            entry = entry->next;
        }
    }

    void clear() {
        for (int i = 0; i < self.bucket_count; i++) {
            Entry<K, V>* entry = self.buckets[i];
            while (entry != null as Entry<K, V>*) {
                Entry<K, V>* next = entry->next;
                deallocate(entry);
                entry = next;
            }
            self.buckets[i] = null as Entry<K, V>*;
        }
        self.size = 0;
    }

    private void rehash() {
        int new_bucket_count = self.bucket_count * 2;
        Entry<K, V>** new_buckets = allocate<Entry<K, V>*>(new_bucket_count);

        // 初期化
        for (int i = 0; i < new_bucket_count; i++) {
            new_buckets[i] = null as Entry<K, V>*;
        }

        // 既存エントリを再配置
        for (int i = 0; i < self.bucket_count; i++) {
            Entry<K, V>* entry = self.buckets[i];
            while (entry != null as Entry<K, V>*) {
                Entry<K, V>* next = entry->next;

                int new_index = (entry->key.hash() % new_bucket_count as ulong) as int;
                entry->next = new_buckets[new_index];
                new_buckets[new_index] = entry;

                entry = next;
            }
        }

        deallocate(self.buckets);
        self.buckets = new_buckets;
        self.bucket_count = new_bucket_count;
    }

    int len() { return self.size; }
    bool is_empty() { return self.size == 0; }
}
```

### Day 8-9: テストフレームワーク

```cm
// src/std/test/test.cm
struct TestRunner {
    Vec<TestCase> tests;
    int passed;
    int failed;
    int skipped;
    bool verbose;
}

struct TestCase {
    string name;
    void (*test_fn)();
    bool should_panic;
    bool skip;
}

static TestRunner global_test_runner;

void test(string name, void (*test_fn)()) {
    TestCase tc;
    tc.name = name;
    tc.test_fn = test_fn;
    tc.should_panic = false;
    tc.skip = false;
    global_test_runner.tests.push(tc);
}

void test_panic(string name, void (*test_fn)()) {
    TestCase tc;
    tc.name = name;
    tc.test_fn = test_fn;
    tc.should_panic = true;
    tc.skip = false;
    global_test_runner.tests.push(tc);
}

void run_tests() {
    println("Running {} tests", global_test_runner.tests.len());

    for (int i = 0; i < global_test_runner.tests.len(); i++) {
        TestCase tc = global_test_runner.tests.get(i);

        if (tc.skip) {
            print("test {} ... ", tc.name);
            println("[33mSKIPPED[0m");
            global_test_runner.skipped = global_test_runner.skipped + 1;
            continue;
        }

        print("test {} ... ", tc.name);

        bool passed = run_test_case(tc);

        if (passed) {
            println("[32mOK[0m");
            global_test_runner.passed = global_test_runner.passed + 1;
        } else {
            println("[31mFAILED[0m");
            global_test_runner.failed = global_test_runner.failed + 1;
        }
    }

    println("\nTest result: [{}]. {} passed; {} failed; {} skipped",
            global_test_runner.failed == 0 ? "32mOK[0m" : "31mFAILED[0m",
            global_test_runner.passed,
            global_test_runner.failed,
            global_test_runner.skipped);

    if (global_test_runner.failed > 0) {
        exit(1);
    }
}

// アサーションマクロ
void assert_eq<T: Eq>(T actual, T expected, string msg) {
    if (actual != expected) {
        panic("assertion failed: {}\n  actual: {}\n  expected: {}",
              msg, actual, expected);
    }
}

void assert_ne<T: Eq>(T actual, T not_expected, string msg) {
    if (actual == not_expected) {
        panic("assertion failed: {}\n  value should not be: {}",
              msg, actual);
    }
}

void assert(bool condition, string msg) {
    if (!condition) {
        panic("assertion failed: {}", msg);
    }
}
```

### Day 10: Vector/HashMap テスト

```cm
// tests/std/vector_test.cm
use std::collections::Vec;
use std::test::{test, assert_eq, run_tests};

test("vec_new", || {
    Vec<int> v;
    assert_eq(v.len(), 0, "new vector should be empty");
    assert(v.is_empty(), "new vector should be empty");
});

test("vec_push_pop", || {
    Vec<int> v;
    v.push(1);
    v.push(2);
    v.push(3);

    assert_eq(v.len(), 3, "should have 3 elements");
    assert_eq(v.pop(), 3, "should pop 3");
    assert_eq(v.pop(), 2, "should pop 2");
    assert_eq(v.pop(), 1, "should pop 1");
    assert(v.is_empty(), "should be empty after popping all");
});

test("vec_index", || {
    Vec<string> v;
    v.push("hello");
    v.push("world");

    assert_eq(v.get(0), "hello", "first element");
    assert_eq(v.get(1), "world", "second element");

    v.set(0, "hi");
    assert_eq(v.get(0), "hi", "modified first element");
});

test("vec_grow", || {
    Vec<int> v;

    // 大量の要素を追加して自動拡張をテスト
    for (int i = 0; i < 1000; i++) {
        v.push(i);
    }

    assert_eq(v.len(), 1000, "should have 1000 elements");

    for (int i = 0; i < 1000; i++) {
        assert_eq(v.get(i), i, "element check");
    }
});

int main() {
    run_tests();
    return 0;
}
```

## Week 3: イテレータとSet実装

### Day 11-12: イテレータシステム

```cm
// src/std/iter/iterator.cm
interface Iterator<T> {
    T* next();
    bool has_next();
}

interface IntoIterator<T> {
    Iterator<T>* iter();
}

// Vectorのイテレータ
struct VecIter<T> {
    Vec<T>* vec;
    int index;
}

impl<T> VecIter<T> for Iterator<T> {
    T* next() {
        if (self.index >= self.vec->len) {
            return null as T*;
        }

        T* result = &self.vec->data[self.index];
        self.index = self.index + 1;
        return result;
    }

    bool has_next() {
        return self.index < self.vec->len;
    }
}

impl<T> Vec<T> for IntoIterator<T> {
    VecIter<T>* iter() {
        VecIter<T>* it = allocate<VecIter<T>>(1);
        it->vec = &self;
        it->index = 0;
        return it;
    }
}

// イテレータアダプタ
struct Map<I: Iterator<T>, T, U> {
    I inner;
    U (*f)(T);
}

impl<I: Iterator<T>, T, U> Map<I, T, U> for Iterator<U> {
    U* next() {
        T* item = self.inner.next();
        if (item == null as T*) {
            return null as U*;
        }

        // TODO: 一時オブジェクトの管理
        U result = self.f(*item);
        return &result;
    }

    bool has_next() {
        return self.inner.has_next();
    }
}

// for-eachループサポート
#define foreach(item, container) \
    for (auto* _iter = (container).iter(); _iter->has_next(); ) \
        for (auto* item = _iter->next(); item != null; item = null)
```

### Day 13: HashSet実装

```cm
// src/std/collections/hashset.cm
use std::collections::HashMap;

struct HashSet<T> {
    HashMap<T, bool> map;
}

impl<T> HashSet<T> {
    self() {
        // HashMap のコンストラクタが自動で呼ばれる
    }
}

impl<T: Hash + Eq> HashSet<T> {
    void insert(T item) {
        self.map.put(item, true);
    }

    bool contains(T item) {
        return self.map.contains(item);
    }

    void remove(T item) {
        self.map.remove(item);
    }

    int len() {
        return self.map.len();
    }

    bool is_empty() {
        return self.map.is_empty();
    }

    void clear() {
        self.map.clear();
    }

    // 集合演算
    HashSet<T> union(HashSet<T> other) {
        HashSet<T> result;

        // TODO: イテレータを使用した実装
        // foreach (item, self) {
        //     result.insert(*item);
        // }
        // foreach (item, other) {
        //     result.insert(*item);
        // }

        return result;
    }

    HashSet<T> intersection(HashSet<T> other) {
        HashSet<T> result;

        // TODO: イテレータを使用した実装

        return result;
    }

    HashSet<T> difference(HashSet<T> other) {
        HashSet<T> result;

        // TODO: イテレータを使用した実装

        return result;
    }
}
```

### Day 14-15: Deque実装

```cm
// src/std/collections/deque.cm
struct Deque<T> {
    T* data;
    int front;
    int back;
    int capacity;
}

impl<T> Deque<T> {
    self() {
        self.capacity = 16;
        self.data = allocate<T>(self.capacity);
        self.front = 0;
        self.back = 0;
    }

    ~self() {
        deallocate(self.data);
    }
}

impl<T> Deque<T> {
    void push_front(T item) {
        self.ensure_capacity();

        self.front = (self.front - 1 + self.capacity) % self.capacity;
        self.data[self.front] = item;
    }

    void push_back(T item) {
        self.ensure_capacity();

        self.data[self.back] = item;
        self.back = (self.back + 1) % self.capacity;
    }

    T pop_front() {
        if (self.is_empty()) {
            panic("pop_front from empty deque");
        }

        T item = self.data[self.front];
        self.front = (self.front + 1) % self.capacity;
        return item;
    }

    T pop_back() {
        if (self.is_empty()) {
            panic("pop_back from empty deque");
        }

        self.back = (self.back - 1 + self.capacity) % self.capacity;
        return self.data[self.back];
    }

    T front() {
        if (self.is_empty()) {
            panic("front on empty deque");
        }
        return self.data[self.front];
    }

    T back() {
        if (self.is_empty()) {
            panic("back on empty deque");
        }
        int index = (self.back - 1 + self.capacity) % self.capacity;
        return self.data[index];
    }

    int len() {
        return (self.back - self.front + self.capacity) % self.capacity;
    }

    bool is_empty() {
        return self.front == self.back;
    }

    private void ensure_capacity() {
        if (self.len() == self.capacity - 1) {
            self.grow();
        }
    }

    private void grow() {
        int new_capacity = self.capacity * 2;
        T* new_data = allocate<T>(new_capacity);

        int current_len = self.len();
        for (int i = 0; i < current_len; i++) {
            int old_index = (self.front + i) % self.capacity;
            new_data[i] = self.data[old_index];
        }

        deallocate(self.data);
        self.data = new_data;
        self.capacity = new_capacity;
        self.front = 0;
        self.back = current_len;
    }
}
```

## Week 4: アルゴリズムとユーティリティ

### Day 16-17: ソートアルゴリズム

```cm
// src/std/algorithm/sort.cm
use std::collections::Vec;

// クイックソート
<T: Ord> void quick_sort(T[] arr, int left, int right) {
    if (left >= right) {
        return;
    }

    int pivot_index = partition(arr, left, right);
    quick_sort(arr, left, pivot_index - 1);
    quick_sort(arr, pivot_index + 1, right);
}

<T: Ord> int partition(T[] arr, int left, int right) {
    T pivot = arr[right];
    int i = left - 1;

    for (int j = left; j < right; j++) {
        if (arr[j] <= pivot) {
            i = i + 1;
            T temp = arr[i];
            arr[i] = arr[j];
            arr[j] = temp;
        }
    }

    T temp = arr[i + 1];
    arr[i + 1] = arr[right];
    arr[right] = temp;

    return i + 1;
}

// ベクター用のソート
<T: Ord> void sort(Vec<T>* vec) {
    if (vec->len <= 1) {
        return;
    }
    quick_sort(vec->data, 0, vec->len - 1);
}

// 安定ソート（マージソート）
<T: Ord> void stable_sort(Vec<T>* vec) {
    if (vec->len <= 1) {
        return;
    }

    T* temp = allocate<T>(vec->len);
    merge_sort_impl(vec->data, temp, 0, vec->len - 1);
    deallocate(temp);
}

<T: Ord> void merge_sort_impl(T[] arr, T[] temp, int left, int right) {
    if (left >= right) {
        return;
    }

    int mid = left + (right - left) / 2;
    merge_sort_impl(arr, temp, left, mid);
    merge_sort_impl(arr, temp, mid + 1, right);
    merge(arr, temp, left, mid, right);
}

<T: Ord> void merge(T[] arr, T[] temp, int left, int mid, int right) {
    // 一時配列にコピー
    for (int i = left; i <= right; i++) {
        temp[i] = arr[i];
    }

    int i = left;
    int j = mid + 1;
    int k = left;

    while (i <= mid && j <= right) {
        if (temp[i] <= temp[j]) {
            arr[k] = temp[i];
            i = i + 1;
        } else {
            arr[k] = temp[j];
            j = j + 1;
        }
        k = k + 1;
    }

    // 残りをコピー
    while (i <= mid) {
        arr[k] = temp[i];
        i = i + 1;
        k = k + 1;
    }
}

// ヒープソート
<T: Ord> void heap_sort(Vec<T>* vec) {
    // ヒープ構築
    for (int i = vec->len / 2 - 1; i >= 0; i--) {
        heapify(vec->data, vec->len, i);
    }

    // ヒープから要素を取り出す
    for (int i = vec->len - 1; i > 0; i--) {
        T temp = vec->data[0];
        vec->data[0] = vec->data[i];
        vec->data[i] = temp;

        heapify(vec->data, i, 0);
    }
}

<T: Ord> void heapify(T[] arr, int n, int i) {
    int largest = i;
    int left = 2 * i + 1;
    int right = 2 * i + 2;

    if (left < n && arr[left] > arr[largest]) {
        largest = left;
    }

    if (right < n && arr[right] > arr[largest]) {
        largest = right;
    }

    if (largest != i) {
        T temp = arr[i];
        arr[i] = arr[largest];
        arr[largest] = temp;

        heapify(arr, n, largest);
    }
}
```

### Day 18: 検索アルゴリズム

```cm
// src/std/algorithm/search.cm

// 線形探索
<T: Eq> int linear_search(T[] arr, int len, T target) {
    for (int i = 0; i < len; i++) {
        if (arr[i] == target) {
            return i;
        }
    }
    return -1;
}

// 二分探索（ソート済み配列）
<T: Ord> int binary_search(T[] arr, int len, T target) {
    int left = 0;
    int right = len - 1;

    while (left <= right) {
        int mid = left + (right - left) / 2;

        if (arr[mid] == target) {
            return mid;
        } else if (arr[mid] < target) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }

    return -1;
}

// Vector用の検索
<T: Eq> int find(Vec<T>* vec, T target) {
    return linear_search(vec->data, vec->len, target);
}

<T: Ord> int binary_find(Vec<T>* vec, T target) {
    return binary_search(vec->data, vec->len, target);
}

// 条件を満たす最初の要素を探す
<T> T* find_if(Vec<T>* vec, bool (*predicate)(T)) {
    for (int i = 0; i < vec->len; i++) {
        if (predicate(vec->data[i])) {
            return &vec->data[i];
        }
    }
    return null as T*;
}

// 全ての要素が条件を満たすか
<T> bool all_of(Vec<T>* vec, bool (*predicate)(T)) {
    for (int i = 0; i < vec->len; i++) {
        if (!predicate(vec->data[i])) {
            return false;
        }
    }
    return true;
}

// いずれかの要素が条件を満たすか
<T> bool any_of(Vec<T>* vec, bool (*predicate)(T)) {
    for (int i = 0; i < vec->len; i++) {
        if (predicate(vec->data[i])) {
            return true;
        }
    }
    return false;
}

// 条件を満たす要素の数
<T> int count_if(Vec<T>* vec, bool (*predicate)(T)) {
    int count = 0;
    for (int i = 0; i < vec->len; i++) {
        if (predicate(vec->data[i])) {
            count = count + 1;
        }
    }
    return count;
}
```

### Day 19-20: Option/Result型

```cm
// src/std/core/option.cm
enum Option<T> {
    Some(T),
    None
}

impl<T> Option<T> {
    bool is_some() {
        match self {
            Some(_) => true,
            None => false
        }
    }

    bool is_none() {
        return !self.is_some();
    }

    T unwrap() {
        match self {
            Some(val) => val,
            None => panic("called unwrap on None value")
        }
    }

    T unwrap_or(T default_val) {
        match self {
            Some(val) => val,
            None => default_val
        }
    }

    <U> Option<U> map(U (*f)(T)) {
        match self {
            Some(val) => Some(f(val)),
            None => None
        }
    }

    Option<T> filter(bool (*predicate)(T)) {
        match self {
            Some(val) => {
                if (predicate(val)) {
                    return Some(val);
                } else {
                    return None;
                }
            },
            None => None
        }
    }
}

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

    bool is_err() {
        return !self.is_ok();
    }

    T unwrap() {
        match self {
            Ok(val) => val,
            Err(e) => panic("called unwrap on Err: {}", e)
        }
    }

    T unwrap_or(T default_val) {
        match self {
            Ok(val) => val,
            Err(_) => default_val
        }
    }

    E unwrap_err() {
        match self {
            Ok(_) => panic("called unwrap_err on Ok value"),
            Err(e) => e
        }
    }

    <U> Result<U, E> map(U (*f)(T)) {
        match self {
            Ok(val) => Ok(f(val)),
            Err(e) => Err(e)
        }
    }

    <F> Result<T, F> map_err(F (*f)(E)) {
        match self {
            Ok(val) => Ok(val),
            Err(e) => Err(f(e))
        }
    }

    <U> Result<U, E> and_then(Result<U, E> (*f)(T)) {
        match self {
            Ok(val) => f(val),
            Err(e) => Err(e)
        }
    }
}
```

## Week 5: 高度な機能

### Day 21-23: スマートポインタ

```cm
// src/std/memory/unique_ptr.cm
struct UniquePtr<T> {
    T* ptr;
}

impl<T> UniquePtr<T> {
    self() {
        self.ptr = null as T*;
    }

    overload self(T* p) {
        self.ptr = p;
    }

    static UniquePtr<T> make(T value) {
        T* p = allocate<T>(1);
        *p = value;
        return UniquePtr<T>(p);
    }

    ~self() {
        if (self.ptr != null as T*) {
            // Tのデストラクタを呼ぶ必要がある場合
            deallocate(self.ptr);
        }
    }

    T* get() {
        return self.ptr;
    }

    T* release() {
        T* p = self.ptr;
        self.ptr = null as T*;
        return p;
    }

    void reset(T* p = null as T*) {
        if (self.ptr != null as T*) {
            deallocate(self.ptr);
        }
        self.ptr = p;
    }

    bool is_null() {
        return self.ptr == null as T*;
    }

    void swap(UniquePtr<T>* other) {
        T* temp = self.ptr;
        self.ptr = other->ptr;
        other->ptr = temp;
    }
}

// 演算子オーバーロード
impl<T> UniquePtr<T> {
    T* operator*() {
        return self.ptr;
    }

    T* operator->() {
        return self.ptr;
    }

    bool operator==(UniquePtr<T> other) {
        return self.ptr == other.ptr;
    }

    bool operator!=(UniquePtr<T> other) {
        return self.ptr != other.ptr;
    }
}

// ムーブセマンティクス
impl<T> UniquePtr<T> {
    // ムーブコンストラクタ（言語機能として未サポート）
    static UniquePtr<T> move_from(UniquePtr<T>* other) {
        UniquePtr<T> result;
        result.ptr = other->ptr;
        other->ptr = null as T*;
        return result;
    }
}
```

### Day 24-25: 文字列ユーティリティ

```cm
// src/std/string/utils.cm
struct StringBuilder {
    Vec<char> buffer;
}

impl StringBuilder {
    self() {
        // Vec のコンストラクタが自動で呼ばれる
    }

    void append(string s) {
        for (int i = 0; i < s.len(); i++) {
            self.buffer.push(s[i]);
        }
    }

    void append_char(char c) {
        self.buffer.push(c);
    }

    void append_int(int n) {
        // 整数を文字列に変換
        if (n == 0) {
            self.append_char('0');
            return;
        }

        if (n < 0) {
            self.append_char('-');
            n = -n;
        }

        // 逆順で数字を取得
        Vec<char> digits;
        while (n > 0) {
            digits.push('0' + (n % 10) as char);
            n = n / 10;
        }

        // 正しい順序で追加
        while (!digits.is_empty()) {
            self.append_char(digits.pop());
        }
    }

    string to_string() {
        // null終端を追加
        self.buffer.push('\0');

        // 文字配列を文字列に変換
        return string(self.buffer.data);
    }

    void clear() {
        self.buffer.clear();
    }

    int len() {
        return self.buffer.len();
    }
}

// 文字列分割
Vec<string> split(string s, char delimiter) {
    Vec<string> result;
    StringBuilder current;

    for (int i = 0; i < s.len(); i++) {
        if (s[i] == delimiter) {
            if (current.len() > 0) {
                result.push(current.to_string());
                current.clear();
            }
        } else {
            current.append_char(s[i]);
        }
    }

    if (current.len() > 0) {
        result.push(current.to_string());
    }

    return result;
}

// 文字列結合
string join(Vec<string>* strings, string separator) {
    StringBuilder sb;

    for (int i = 0; i < strings->len(); i++) {
        if (i > 0) {
            sb.append(separator);
        }
        sb.append(strings->get(i));
    }

    return sb.to_string();
}

// 文字列のトリム
string trim(string s) {
    int start = 0;
    int end = s.len() - 1;

    // 先頭の空白を除去
    while (start <= end && is_whitespace(s[start])) {
        start = start + 1;
    }

    // 末尾の空白を除去
    while (end >= start && is_whitespace(s[end])) {
        end = end - 1;
    }

    // 部分文字列を作成
    StringBuilder sb;
    for (int i = start; i <= end; i++) {
        sb.append_char(s[i]);
    }

    return sb.to_string();
}

bool is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}
```

## Week 6: 統合テストと最適化

### Day 26-27: 統合テスト

```cm
// tests/integration/stl_integration_test.cm
use std::collections::{Vec, HashMap, HashSet, Deque};
use std::algorithm::{sort, find};
use std::memory::UniquePtr;
use std::test::{test, assert_eq, run_tests};

test("complex_data_structure", || {
    // ネストしたデータ構造のテスト
    HashMap<string, Vec<int>> map;

    Vec<int> v1;
    v1.push(1);
    v1.push(2);
    v1.push(3);
    map.put("first", v1);

    Vec<int> v2;
    v2.push(4);
    v2.push(5);
    map.put("second", v2);

    Vec<int>* first = map.get("first");
    assert(first != null as Vec<int>*, "should find first");
    assert_eq(first->len(), 3, "first should have 3 elements");
    assert_eq(first->get(0), 1, "first element check");
});

test("algorithm_integration", || {
    Vec<int> v;
    v.push(3);
    v.push(1);
    v.push(4);
    v.push(1);
    v.push(5);
    v.push(9);

    sort(&v);

    assert_eq(v.get(0), 1, "sorted first element");
    assert_eq(v.get(1), 1, "sorted second element");
    assert_eq(v.get(2), 3, "sorted third element");
    assert_eq(v.get(3), 4, "sorted fourth element");
    assert_eq(v.get(4), 5, "sorted fifth element");
    assert_eq(v.get(5), 9, "sorted sixth element");

    int index = binary_find(&v, 5);
    assert_eq(index, 4, "binary search for 5");
});

test("memory_management", || {
    // スマートポインタのテスト
    UniquePtr<Vec<int>> ptr = UniquePtr<Vec<int>>::make(Vec<int>());

    ptr->push(10);
    ptr->push(20);
    ptr->push(30);

    assert_eq(ptr->len(), 3, "smart pointer vector length");
    assert_eq(ptr->get(1), 20, "smart pointer vector element");

    // ptrのデストラクタが自動的にVectorを解放
});

test("container_composition", || {
    // コンテナの組み合わせ
    Deque<HashSet<string>> deque;

    HashSet<string> set1;
    set1.insert("apple");
    set1.insert("banana");

    HashSet<string> set2;
    set2.insert("cherry");
    set2.insert("date");

    deque.push_back(set1);
    deque.push_back(set2);

    assert_eq(deque.len(), 2, "deque length");

    HashSet<string> front = deque.front();
    assert(front.contains("apple"), "front set contains apple");
    assert(front.contains("banana"), "front set contains banana");
});

int main() {
    run_tests();
    return 0;
}
```

### Day 28-29: パフォーマンス最適化

```cm
// src/std/collections/vec_optimized.cm

// Small Vector Optimization実装
struct SmallVec<T, const int N = 16> {
    union Data {
        T stack[N];
        struct {
            T* heap;
            int heap_capacity;
        };
    }

    Data data;
    int len;
    bool on_heap;
}

impl<T, const int N> SmallVec<T, N> {
    self() {
        self.len = 0;
        self.on_heap = false;
        // stack配列は未初期化で良い
    }

    ~self() {
        if (self.on_heap) {
            deallocate(self.data.heap);
        }
    }

    void push(T item) {
        if (!self.on_heap && self.len < N) {
            // スタックに格納
            self.data.stack[self.len] = item;
        } else {
            // ヒープに移行または既にヒープ上
            if (!self.on_heap) {
                self.move_to_heap();
            }

            if (self.len >= self.data.heap_capacity) {
                self.grow_heap();
            }

            self.data.heap[self.len] = item;
        }
        self.len = self.len + 1;
    }

    T get(int index) {
        if (index < 0 || index >= self.len) {
            panic("index out of bounds");
        }

        if (self.on_heap) {
            return self.data.heap[index];
        } else {
            return self.data.stack[index];
        }
    }

    private void move_to_heap() {
        int new_capacity = max(N * 2, self.len * 2);
        T* heap_data = allocate<T>(new_capacity);

        // スタックからコピー
        for (int i = 0; i < self.len; i++) {
            heap_data[i] = self.data.stack[i];
        }

        self.data.heap = heap_data;
        self.data.heap_capacity = new_capacity;
        self.on_heap = true;
    }

    private void grow_heap() {
        int new_capacity = self.data.heap_capacity * 2;
        T* new_data = allocate<T>(new_capacity);

        copy_memory(new_data, self.data.heap, self.len);
        deallocate(self.data.heap);

        self.data.heap = new_data;
        self.data.heap_capacity = new_capacity;
    }
}

// COW (Copy-On-Write) String実装
struct CowString {
    struct Data {
        char* str;
        int len;
        int ref_count;
    }

    Data* data;
}

impl CowString {
    self(string s) {
        self.data = allocate<Data>(1);
        self.data->len = s.len();
        self.data->str = allocate<char>(self.data->len + 1);
        self.data->ref_count = 1;

        copy_memory(self.data->str, s.data(), self.data->len);
        self.data->str[self.data->len] = '\0';
    }

    // コピーコンストラクタ
    overload self(CowString other) {
        self.data = other.data;
        self.data->ref_count = self.data->ref_count + 1;
    }

    ~self() {
        self.data->ref_count = self.data->ref_count - 1;
        if (self.data->ref_count == 0) {
            deallocate(self.data->str);
            deallocate(self.data);
        }
    }

    void make_unique() {
        if (self.data->ref_count > 1) {
            // コピーを作成
            Data* new_data = allocate<Data>(1);
            new_data->len = self.data->len;
            new_data->str = allocate<char>(new_data->len + 1);
            new_data->ref_count = 1;

            copy_memory(new_data->str, self.data->str, new_data->len + 1);

            self.data->ref_count = self.data->ref_count - 1;
            self.data = new_data;
        }
    }

    void set_char(int index, char c) {
        self.make_unique();  // 書き込み前にコピー
        self.data->str[index] = c;
    }

    char get_char(int index) {
        return self.data->str[index];
    }
}
```

### Day 30: ドキュメント作成とファイナライズ

```markdown
# Cm Standard Library Documentation

## Overview
Cm STL provides a comprehensive set of containers, algorithms, and utilities
similar to C++ STL but with modern safety features.

## Containers

### Vector<T>
Dynamic array with automatic memory management.

#### Example:
```cm
Vec<int> numbers;
numbers.push(10);
numbers.push(20);
int sum = numbers.get(0) + numbers.get(1);
```

#### Methods:
- `push(T item)` - Add element to end
- `pop() -> T` - Remove and return last element
- `get(int index) -> T` - Get element at index
- `set(int index, T value)` - Set element at index
- `len() -> int` - Get number of elements
- `capacity() -> int` - Get allocated capacity
- `clear()` - Remove all elements
- `is_empty() -> bool` - Check if empty

### HashMap<K, V>
Hash table implementation with chaining.

#### Example:
```cm
HashMap<string, int> ages;
ages.put("Alice", 30);
ages.put("Bob", 25);
int* alice_age = ages.get("Alice");
```

#### Methods:
- `put(K key, V value)` - Insert or update
- `get(K key) -> V*` - Get value pointer (null if not found)
- `contains(K key) -> bool` - Check if key exists
- `remove(K key)` - Remove key-value pair
- `clear()` - Remove all entries
- `len() -> int` - Get number of entries

### HashSet<T>
Set implementation using HashMap.

### Deque<T>
Double-ended queue with O(1) operations at both ends.

## Algorithms

### Sorting
- `sort(Vec<T>* vec)` - Quick sort
- `stable_sort(Vec<T>* vec)` - Merge sort
- `heap_sort(Vec<T>* vec)` - Heap sort

### Searching
- `find(Vec<T>* vec, T target) -> int` - Linear search
- `binary_find(Vec<T>* vec, T target) -> int` - Binary search
- `find_if(Vec<T>* vec, predicate) -> T*` - Find by condition

## Memory Management

### UniquePtr<T>
Smart pointer with unique ownership.

```cm
UniquePtr<MyClass> ptr = UniquePtr<MyClass>::make(MyClass());
ptr->method();
// Automatically deleted when ptr goes out of scope
```

## Error Handling

### Option<T>
Represents optional values.

```cm
Option<int> divide(int a, int b) {
    if (b == 0) {
        return None;
    }
    return Some(a / b);
}
```

### Result<T, E>
Represents success or failure.

```cm
Result<int, string> parse_int(string s) {
    // parsing logic
    if (success) {
        return Ok(value);
    }
    return Err("Invalid integer");
}
```

## Performance Considerations

- Vector uses exponential growth (2x) to amortize insertions
- HashMap rehashes at 75% load factor
- SmallVec optimizes small collections to avoid heap allocation
- All containers properly implement move semantics

## Thread Safety

Current implementation is NOT thread-safe. For concurrent access,
external synchronization is required.

## Future Enhancements

- Concurrent containers
- B-tree based ordered maps
- Lock-free data structures
- SIMD optimized algorithms
```

## 成果物チェックリスト

### 実装完了項目
- [ ] selfメソッドバグ修正
- [ ] メモリ管理基盤 (memory.cm)
- [ ] Vector<T> 完全実装
- [ ] HashMap<K, V> 完全実装
- [ ] HashSet<T> 実装
- [ ] Deque<T> 実装
- [ ] イテレータ基盤
- [ ] ソートアルゴリズム (3種類)
- [ ] 検索アルゴリズム
- [ ] Option<T> / Result<T, E>
- [ ] UniquePtr<T> スマートポインタ
- [ ] 文字列ユーティリティ
- [ ] テストフレームワーク
- [ ] SmallVec最適化
- [ ] 包括的テストスイート
- [ ] ドキュメント

### テストメトリクス
- ユニットテスト: 100+ ケース
- 統合テスト: 20+ シナリオ
- パフォーマンステスト: 各コンテナ
- コードカバレッジ: 90%以上目標

### パフォーマンス目標
- Vector::push: O(1) amortized
- HashMap::get: O(1) average
- Sort: O(n log n) average
- メモリオーバーヘッド: C++ STL比 +10%以内

## リスク管理

| リスク | 対策 | ステータス |
|--------|------|------------|
| selfバグ修正の複雑性 | 早期着手、広範なテスト | Week 1 |
| パフォーマンス劣化 | ベンチマーク継続実施 | 継続 |
| 互換性問題 | 段階的ロールアウト | Week 4-6 |
| ドキュメント不足 | 並行作成 | Week 6 |

## まとめ

このロードマップに従うことで、6週間でCm言語に完全に機能するC++風STLライブラリを実装できます。各週のマイルストーンは明確に定義されており、段階的な実装により早期に価値を提供できます。

最重要事項:
1. Week 1でselfバグを必ず修正
2. Vector/HashMapを優先実装
3. 継続的なテストとベンチマーク
4. ドキュメントの並行作成

成功の鍵は、基盤を確実に固めてから機能を積み上げることです。