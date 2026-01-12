# イテレータシステム 標準ライブラリ実装仕様

作成日: 2026-01-11
関連文書: 025_iterator_explicit_interface_design.md

## 標準ライブラリコレクションのイテレータ実装

### 設計原則

1. **すべてのSTLコレクションは明示的にIterableを実装**
2. **配列とスライスのみコンパイラが自動実装を提供**
3. **イテレータの種類を明確に区別**（iter/iter_mut/into_iter）

## 1. Vec<T>（動的配列）

### 1.1 基本実装

```cm
// std/collections/vec.cm
module std.collections;

export struct Vec<T> {
    private T* data;
    private int size;
    private int capacity;
}

// ============================================================
// イテレータ型の定義
// ============================================================

/// 借用イテレータ（要素を参照）
export struct VecIter<T> {
    private T* current;
    private T* end;
}

/// 可変借用イテレータ（要素を可変参照）
export struct VecIterMut<T> {
    private T* current;
    private T* end;
}

/// 所有イテレータ（Vecを消費）
export struct VecIntoIter<T> {
    private Vec<T> vec;  // 所有権を持つ
    private int index;
}

// ============================================================
// イテレータの実装
// ============================================================

impl<T> VecIter<T> for Iterator<T> {
    bool has_next() {
        return self.current < self.end;
    }

    T next() {
        if (!self.has_next()) {
            __panic__("VecIter: no more elements");
        }
        T value = *self.current;
        self.current++;
        return value;
    }

    T peek() {
        if (!self.has_next()) {
            __panic__("VecIter: no more elements");
        }
        return *self.current;
    }

    int size_hint() {
        return (self.end - self.current);
    }
}

impl<T> VecIter<T> for ExactSizeIterator<T> {
    int len() {
        return (self.end - self.current);
    }

    bool is_empty() {
        return self.current >= self.end;
    }
}

// ============================================================
// Vec<T>へのIterable実装
// ============================================================

impl<T> Vec<T> for Iterable<T> {
    VecIter<T> iter() {
        return VecIter<T>{
            current: self.data,
            end: self.data + self.size
        };
    }
}

impl<T> Vec<T> for IntoIterator<T> {
    VecIntoIter<T> into_iter() {
        return VecIntoIter<T>{
            vec: *self,  // 所有権を移動
            index: 0
        };
    }
}

// ============================================================
// Vec<T>のメソッド
// ============================================================

impl<T> Vec<T> {
    /// 可変イテレータを返す
    VecIterMut<T> iter_mut() {
        return VecIterMut<T>{
            current: self.data,
            end: self.data + self.size
        };
    }

    /// 逆順イテレータ
    ReverseIterator<VecIter<T>> iter_rev() {
        return ReverseIterator<VecIter<T>>{
            base: self.iter()
        };
    }
}
```

## 2. HashMap<K, V>

### 2.1 基本実装

```cm
// std/collections/hashmap.cm

export struct HashMap<K, V> {
    private struct Bucket {
        K key;
        V value;
        bool occupied;
    }
    private Bucket* buckets;
    private int size;
    private int capacity;
}

// ============================================================
// イテレータ型
// ============================================================

export struct HashMapIter<K, V> {
    private Bucket* current;
    private Bucket* end;
    private HashMap<K, V>* map;  // 参照を保持
}

export struct KeyIter<K, V> {
    private HashMapIter<K, V> base;
}

export struct ValueIter<K, V> {
    private HashMapIter<K, V> base;
}

// ============================================================
// HashMapIterの実装
// ============================================================

impl<K, V> HashMapIter<K, V> for Iterator<Pair<K, V>> {
    bool has_next() {
        // 次の占有バケットを探す
        while (self.current < self.end) {
            if (self.current->occupied) {
                return true;
            }
            self.current++;
        }
        return false;
    }

    Pair<K, V> next() {
        if (!self.has_next()) {
            __panic__("HashMapIter: no more elements");
        }

        Pair<K, V> result = {
            key: self.current->key,
            value: self.current->value
        };
        self.current++;
        return result;
    }

    Pair<K, V> peek() {
        if (!self.has_next()) {
            __panic__("HashMapIter: no more elements");
        }

        return Pair<K, V>{
            key: self.current->key,
            value: self.current->value
        };
    }

    int size_hint() {
        return self.map->size;  // 正確な残り要素数は不明
    }
}

// ============================================================
// HashMap<K, V>へのIterable実装
// ============================================================

impl<K, V> HashMap<K, V> for Iterable<Pair<K, V>> {
    HashMapIter<K, V> iter() {
        return HashMapIter<K, V>{
            current: self.buckets,
            end: self.buckets + self.capacity,
            map: &self
        };
    }
}

// ============================================================
// 特殊なイテレータメソッド
// ============================================================

impl<K, V> HashMap<K, V> {
    /// キーのイテレータ
    KeyIter<K, V> keys() {
        return KeyIter<K, V>{
            base: self.iter()
        };
    }

    /// 値のイテレータ
    ValueIter<K, V> values() {
        return ValueIter<K, V>{
            base: self.iter()
        };
    }

    /// 可変値イテレータ
    ValueIterMut<K, V> values_mut() {
        return ValueIterMut<K, V>{
            base: self.iter_mut()
        };
    }
}

impl<K, V> KeyIter<K, V> for Iterator<K> {
    bool has_next() {
        return self.base.has_next();
    }

    K next() {
        return self.base.next().key;
    }

    K peek() {
        return self.base.peek().key;
    }

    int size_hint() {
        return self.base.size_hint();
    }
}
```

## 3. BTreeMap<K, V>（順序付きマップ）

### 3.1 特徴

- **順序保証**: キーの順序でイテレート
- **両方向イテレータ**: 前後両方向に移動可能
- **範囲イテレータ**: 特定範囲のキーのみイテレート

```cm
// std/collections/btreemap.cm

export struct BTreeMap<K: Ord, V> {
    private struct Node {
        K* keys;
        V* values;
        Node** children;
        int num_keys;
        bool is_leaf;
    }
    private Node* root;
    private int size;
}

// ============================================================
// 順序付きイテレータ
// ============================================================

export struct BTreeIter<K, V> {
    private struct StackFrame {
        Node* node;
        int index;
    }
    private StackFrame[32] stack;  // 最大深さ32
    private int stack_size;
    private bool finished;
}

impl<K: Ord, V> BTreeMap<K, V> for Iterable<Pair<K, V>> {
    BTreeIter<K, V> iter() {
        BTreeIter<K, V> iter;
        iter.stack_size = 0;
        iter.finished = false;

        // 最左端のノードまで降りる
        if (self.root != null) {
            push_left(iter, self.root);
        }

        return iter;
    }
}

impl<K: Ord, V> BTreeIter<K, V> for Iterator<Pair<K, V>> {
    bool has_next() {
        return !self.finished && self.stack_size > 0;
    }

    Pair<K, V> next() {
        if (!self.has_next()) {
            __panic__("BTreeIter: no more elements");
        }

        // 現在のノードから値を取得
        StackFrame* frame = &self.stack[self.stack_size - 1];
        Node* node = frame->node;
        int index = frame->index;

        Pair<K, V> result = {
            key: node->keys[index],
            value: node->values[index]
        };

        // 次の要素に進む
        advance_to_next(self);

        return result;
    }
}

// ============================================================
// 範囲イテレータ
// ============================================================

impl<K: Ord, V> BTreeMap<K, V> {
    /// 範囲内のエントリをイテレート
    RangeIter<K, V> range(K start, K end) {
        return RangeIter<K, V>{
            base: self.iter(),
            start: start,
            end: end,
            started: false
        };
    }

    /// 逆順イテレータ
    ReverseBTreeIter<K, V> iter_rev() {
        ReverseBTreeIter<K, V> iter;
        // 最右端から開始
        if (self.root != null) {
            push_right(iter, self.root);
        }
        return iter;
    }
}
```

## 4. LinkedList<T>（双方向連結リスト）

### 4.1 実装

```cm
// std/collections/linkedlist.cm

export struct LinkedList<T> {
    private struct Node {
        T value;
        Node* next;
        Node* prev;
    }
    private Node* head;
    private Node* tail;
    private int size;
}

// ============================================================
// 双方向イテレータ
// ============================================================

export struct LinkedListIter<T> {
    private Node* current;
    private Node* tail;     // 逆方向用
    private int remaining;
}

impl<T> LinkedList<T> for Iterable<T> {
    LinkedListIter<T> iter() {
        return LinkedListIter<T>{
            current: self.head,
            tail: self.tail,
            remaining: self.size
        };
    }
}

impl<T> LinkedListIter<T> for Iterator<T> {
    bool has_next() {
        return self.current != null;
    }

    T next() {
        if (!self.has_next()) {
            __panic__("LinkedListIter: no more elements");
        }

        T value = self.current->value;
        self.current = self.current->next;
        self.remaining--;
        return value;
    }

    T peek() {
        if (!self.has_next()) {
            __panic__("LinkedListIter: no more elements");
        }
        return self.current->value;
    }

    int size_hint() {
        return self.remaining;
    }
}

impl<T> LinkedListIter<T> for BidirectionalIterator<T> {
    bool has_prev() {
        return self.tail != null;
    }

    T prev() {
        if (!self.has_prev()) {
            __panic__("LinkedListIter: no more elements");
        }

        T value = self.tail->value;
        self.tail = self.tail->prev;
        self.remaining--;
        return value;
    }
}

impl<T> LinkedListIter<T> for ExactSizeIterator<T> {
    int len() {
        return self.remaining;
    }

    bool is_empty() {
        return self.remaining == 0;
    }
}
```

## 5. 使用例とベストプラクティス

### 5.1 基本的な使用

```cm
import std::collections::*;

int main() {
    // Vec - 最も基本的な動的配列
    Vec<int> vec = vec![1, 2, 3, 4, 5];

    // 借用イテレータ（要素をコピー）
    for (int x in vec) {
        println("{x}");
    }
    // vecは引き続き使用可能

    // 所有イテレータ（vecを消費）
    for (int x in vec.into_iter()) {
        println("{x}");
    }
    // vecはもう使用不可（所有権が移動）

    // HashMap - キーと値のペア
    HashMap<string, int> map;
    map.insert("one", 1);
    map.insert("two", 2);
    map.insert("three", 3);

    // エントリのイテレート
    for (Pair<string, int> entry in map) {
        println("{entry.key}: {entry.value}");
    }

    // キーのみ
    for (string key in map.keys()) {
        println("Key: {key}");
    }

    // 値のみ
    for (int value in map.values()) {
        println("Value: {value}");
    }

    // BTreeMap - 順序付き
    BTreeMap<int, string> btree;
    btree.insert(3, "three");
    btree.insert(1, "one");
    btree.insert(2, "two");

    // キーの順序でイテレート（1, 2, 3）
    for (Pair<int, string> entry in btree) {
        println("{entry.key}: {entry.value}");
    }

    // 範囲イテレート
    for (Pair<int, string> entry in btree.range(1, 3)) {
        // 1と2のエントリのみ
    }

    // LinkedList - 双方向
    LinkedList<double> list;
    list.push_back(3.14);
    list.push_back(2.71);
    list.push_back(1.41);

    // 前方向
    for (double val in list) {
        println("{val}");
    }

    // 逆方向
    for (double val in list.iter_rev()) {
        println("{val}");
    }

    return 0;
}
```

### 5.2 アダプタチェーン

```cm
// 関数型プログラミングスタイル
Vec<int> vec = vec![1, 2, 3, 4, 5, 6, 7, 8, 9, 10];

// フィルタとマップのチェーン
Vec<int> result = vec.iter()
    .filter((int x) => x % 2 == 0)  // 偶数のみ
    .map((int x) => x * x)           // 二乗
    .collect();                      // Vec<int>に収集

// 結果: [4, 16, 36, 64, 100]
```

## 6. パフォーマンス特性

### 各コレクションのイテレータ性能

| コレクション | next() | size_hint() | 両方向 | ランダムアクセス | メモリ |
|-------------|--------|-------------|--------|-----------------|--------|
| Vec<T> | O(1) | O(1) | ❌ | ✅ | 16B |
| HashMap<K,V> | O(1)* | O(1) | ❌ | ❌ | 24B |
| BTreeMap<K,V> | O(1)* | O(1) | ✅ | ❌ | 264B |
| LinkedList<T> | O(1) | O(1) | ✅ | ❌ | 24B |

*償却計算量

## まとめ

この実装により：

1. **すべてのSTLコレクションが統一的なIterableインターフェースを実装**
2. **明示的な`impl for`により意図が明確**
3. **配列/スライスのみ自動実装で利便性確保**
4. **型安全性とパフォーマンスの両立**

が実現されます。

---

**作成者:** Claude Code
**ステータス:** 実装仕様書