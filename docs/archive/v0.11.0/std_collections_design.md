# v0.11.0 標準ライブラリ データ構造設計

## 概要

v0.11.0では、C++ STLに相当するデータ構造をCm標準ライブラリ（`std::collections`）に追加する。

---

## 1. 追加するデータ構造

### 1.1 PriorityQueue<T>

```cm
// std/collections/pqueue.cm
interface Comparable<T> {
    int compare_to(T other);
}

struct PriorityQueue<T> {
    T[] data;
    int size;
}

interface PQueueOps<T> {
    void push(T item);
    T pop();
    T peek();
    bool is_empty();
    int len();
}

impl<T: Comparable<T>> PriorityQueue<T> for PQueueOps<T> {
    // ヒープ操作を実装
}
```

### 1.2 List<T>（動的配列）

```cm
// std/collections/list.cm
struct List<T> {
    T[] data;
    int size;
    int capacity;
}

interface ListOps<T> {
    void push(T item);
    T pop();
    T get(int index);
    void set(int index, T value);
    void insert(int index, T item);
    void remove(int index);
    int len();
    bool is_empty();
}
```

### 1.3 HashMap<K, V>

```cm
// std/collections/hashmap.cm
interface Hashable {
    ulong hash();
}

interface Eq<T> {
    bool equals(T other);
}

struct HashMap<K, V> {
    // バケット配列
    Entry<K, V>[] buckets;
    int size;
}

interface HashMapOps<K, V> {
    void insert(K key, V value);
    V get(K key);
    bool contains_key(K key);
    void remove(K key);
    int len();
}
```

### 1.4 Set<T>

```cm
// std/collections/set.cm
struct Set<T> {
    HashMap<T, bool> map;
}

interface SetOps<T> {
    void insert(T item);
    bool contains(T item);
    void remove(T item);
    int len();
}
```

### 1.5 Deque<T>（両端キュー）

```cm
// std/collections/deque.cm
struct Deque<T> {
    T[] data;
    int front;
    int back;
    int size;
}

interface DequeOps<T> {
    void push_front(T item);
    void push_back(T item);
    T pop_front();
    T pop_back();
}
```

---

## 2. 前提条件（必要なバグ修正）

### 2.1 インタプリタのself変更バグ

**問題**: interfaceメソッド内で`self.field = value`が反映されない

**症状**: 
```
Initial: c.value = 0
Before inc: self.value = 0
After inc: self.value = 1    ← メソッド内では変更されている
After c.inc(): c.value = 0   ← 呼び出し後は元の値のまま
```

**根本原因（MIRレベル）**:
メソッド呼び出し時に以下のMIRが生成される：
```
bb2: {
    _5 = _1;        // レシーバーを一時変数にコピー
    _6 = &_5;       // コピーのアドレスを取得
    call Counter__inc(_6) -> bb3;  // コピーへのポインタを渡す
}
bb3: {
    _8 = _1.0;      // 元の変数を読む（変更されていない）
}
```

**問題点**: `_5`（コピー）への変更は`_1`（元の変数）に反映されない

**修正方法**:
MIR生成時にメソッド呼び出し後にコピーバックを生成する：
```
bb2: {
    _5 = _1;
    _6 = &_5;
    call Counter__inc(_6) -> bb3;
}
bb3: {
    _1 = _5;        // 変更をコピーバック（追加）
    _8 = _1.0;
}
```

または、コピーを作らず直接`&_1`を渡す：
```
bb2: {
    _6 = &_1;       // 直接アドレスを取得
    call Counter__inc(_6) -> bb3;
}
```

**修正箇所**: `src/mir/lowering/expr_call.cpp` または `src/mir/lowering/stmt.cpp` のメソッド呼び出し生成部分

### 2.2 2D配列アクセスバグ

**問題**: `arr[i][j]`の読み書きが動作しない

**修正箇所**: `src/codegen/interpreter/eval.hpp`のIndex projection処理

---

## 3. ディレクトリ構造

```
std/
├── collections/
│   ├── mod.cm          # モジュール定義
│   ├── pqueue.cm       # PriorityQueue
│   ├── list.cm         # List (Vector)
│   ├── hashmap.cm      # HashMap
│   ├── set.cm          # Set
│   └── deque.cm        # Deque
├── core/
├── io/
├── math/
└── mem/
```

---

## 4. 使用例

```cm
import std::collections::PriorityQueue;

struct Task with Comparable<Task> {
    int priority;
    string name;
}

impl Task for Comparable<Task> {
    int compare_to(Task other) {
        return self.priority - other.priority;
    }
}

int main() {
    PriorityQueue<Task> pq;
    
    pq.push({priority: 3, name: "Low"});
    pq.push({priority: 1, name: "High"});
    pq.push({priority: 2, name: "Medium"});
    
    while (!pq.is_empty()) {
        Task t = pq.pop();
        println("Task: {t.name}");
    }
    // 出力: High, Medium, Low
    
    return 0;
}
```

---

## 5. TODO

- [ ] インタプリタのself変更バグ修正
- [ ] 2D配列アクセスバグ修正
- [ ] PriorityQueue実装
- [ ] List実装
- [ ] HashMap実装
- [ ] Set実装
- [ ] Deque実装
- [ ] テストケース作成
