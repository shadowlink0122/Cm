# イテレータシステム 明示的インターフェース設計

作成日: 2026-01-11
対象バージョン: v0.11.0
関連文書: 023_iterator_system_complete_implementation.md

## 設計原則

### 明示的インターフェース実装の採用

Cm言語の設計哲学として、以下の原則を採用：

1. **Goスタイル**: インターフェースを明示的に宣言
2. **Rustスタイル**: `impl Type for Interface` で明示的に実装を付与
3. **暗黙的実装の禁止**: メソッド名が一致しても自動的にインターフェースを満たさない

```cm
// ❌ 暗黙的実装（採用しない）
struct MyType {
    // has_next()とnext()があってもIteratorにならない
}

// ✅ 明示的実装（採用）
impl MyType for Iterator<int> {
    // 明示的にIteratorインターフェースを実装
}
```

## 1. コアインターフェース定義

### 1.1 基本イテレータインターフェース

```cm
// std/iter/core.cm
module std.iter;

/// イテレータの基本インターフェース
export interface Iterator<T> {
    /// 次の要素があるか確認
    bool has_next();

    /// 次の要素を取得してポインタを進める
    T next();

    /// 現在の要素を覗く（ポインタは進めない）
    T peek();

    /// 残りの要素数（既知の場合）
    int size_hint();  // -1 = 不明
}

/// イテレータを生成可能な型
export interface Iterable<T> {
    /// イテレータを生成
    Iterator<T> iter();
}

/// 所有権を持つイテレータ（要素を移動）
export interface IntoIterator<T> {
    /// 要素の所有権を移動するイテレータを生成
    Iterator<T> into_iter();
}

/// 可変イテレータ
export interface IteratorMut<T> {
    bool has_next();
    T* next_mut();  // 可変参照を返す
    T* peek_mut();
    int size_hint();
}

/// 両方向イテレータ
export interface BidirectionalIterator<T> for Iterator<T> {
    bool has_prev();
    T prev();
}

/// ランダムアクセスイテレータ
export interface RandomAccessIterator<T> for BidirectionalIterator<T> {
    void seek(int index);
    int position();
    T at(int index);
}

/// 正確なサイズを知っているイテレータ
export interface ExactSizeIterator<T> for Iterator<T> {
    int len();
    bool is_empty();
}
```

## 2. 組み込み型への自動実装

### 2.1 配列への自動実装

```cm
// コンパイラが自動生成するコード

// 固定長配列
impl<T, const N: int> T[N] for Iterable<T> {
    ArrayIterator<T> iter() {
        return ArrayIterator<T>{
            current: &self[0],
            end: &self[0] + N,
            start: &self[0]
        };
    }
}

impl<T, const N: int> T[N] for IntoIterator<T> {
    ArrayIntoIterator<T> into_iter() {
        // 配列の所有権を移動
        return ArrayIntoIterator<T>{
            data: self,
            index: 0,
            len: N
        };
    }
}

// 動的配列（スライス）
impl<T> T[] for Iterable<T> {
    SliceIterator<T> iter() {
        return SliceIterator<T>{
            current: self.ptr,
            end: self.ptr + self.len,
            start: self.ptr
        };
    }
}

impl<T> T[] for ExactSizeIterator<T> {
    int len() {
        return self.len;
    }

    bool is_empty() {
        return self.len == 0;
    }
}
```

### 2.2 文字列への自動実装

```cm
// 文字列のイテレータ（UTF-8対応）
impl string for Iterable<char> {
    StringIterator iter() {
        return StringIterator{
            current: self.data,
            end: self.data + self.len
        };
    }
}

// C文字列
impl cstring for Iterable<char> {
    CStringIterator iter() {
        return CStringIterator{
            current: self
        };
    }
}
```

## 3. 標準ライブラリのデータ構造

### 3.1 Vec<T>（動的配列）

```cm
// std/collections/vec.cm
export struct Vec<T> {
    private T* data;
    private int size;
    private int capacity;
}

// 明示的にIterableを実装
impl<T> Vec<T> for Iterable<T> {
    VecIterator<T> iter() {
        return VecIterator<T>{
            current: self.data,
            end: self.data + self.size,
            vec: &self  // 参照を保持
        };
    }
}

impl<T> Vec<T> for IntoIterator<T> {
    VecIntoIterator<T> into_iter() {
        // Vecの所有権を移動
        return VecIntoIterator<T>{
            vec: self,
            index: 0
        };
    }
}

impl<T> Vec<T> for ExactSizeIterator<T> {
    int len() { return self.size; }
    bool is_empty() { return self.size == 0; }
}
```

### 3.2 LinkedList<T>

```cm
// std/collections/linked_list.cm
export struct LinkedList<T> {
    private Node<T>* head;
    private Node<T>* tail;
    private int size;
}

// 明示的にIterableを実装
impl<T> LinkedList<T> for Iterable<T> {
    LinkedListIterator<T> iter() {
        return LinkedListIterator<T>{
            current: self.head,
            remaining: self.size
        };
    }
}

impl<T> LinkedList<T> for BidirectionalIterator<T> {
    // 双方向リストなので両方向イテレータも実装
    LinkedListIterator<T> iter_rev() {
        return LinkedListIterator<T>{
            current: self.tail,
            remaining: self.size,
            reverse: true
        };
    }
}
```

### 3.3 HashMap<K, V>

```cm
// std/collections/hashmap.cm
export struct HashMap<K, V> {
    private Bucket<K, V>* buckets;
    private int size;
    private int capacity;
}

// 明示的にIterableを実装（キーと値のペア）
impl<K, V> HashMap<K, V> for Iterable<Pair<K, V>> {
    HashMapIterator<K, V> iter() {
        return HashMapIterator<K, V>{
            map: &self,
            bucket_index: 0,
            entry_index: 0
        };
    }
}

// キーのみのイテレータ
impl<K, V> HashMap<K, V> {
    KeyIterator<K> keys() {
        return KeyIterator<K>{base: self.iter()};
    }

    ValueIterator<V> values() {
        return ValueIterator<V>{base: self.iter()};
    }
}
```

### 3.4 BTreeMap<K, V>

```cm
// std/collections/btree.cm
export struct BTreeMap<K, V> {
    private BTreeNode<K, V>* root;
    private int size;
}

// 明示的にIterableを実装（順序付き）
impl<K: Ord, V> BTreeMap<K, V> for Iterable<Pair<K, V>> {
    BTreeIterator<K, V> iter() {
        // 中順走査イテレータ
        return BTreeIterator<K, V>{
            stack: [],
            current: leftmost_node(self.root)
        };
    }
}

impl<K: Ord, V> BTreeMap<K, V> for BidirectionalIterator<Pair<K, V>> {
    // BTreeは順序付きなので両方向イテレータも提供
}
```

## 4. イテレータの具象型実装

### 4.1 ArrayIterator

```cm
// std/iter/array.cm
export struct ArrayIterator<T> {
    private T* current;
    private T* end;
    private T* start;  // 双方向用
}

// 明示的にIteratorインターフェースを実装
impl<T> ArrayIterator<T> for Iterator<T> {
    bool has_next() {
        return self.current < self.end;
    }

    T next() {
        if (!self.has_next()) {
            __panic__("Iterator exhausted");
        }
        T value = *self.current;
        self.current++;
        return value;
    }

    T peek() {
        if (!self.has_next()) {
            __panic__("Iterator exhausted");
        }
        return *self.current;
    }

    int size_hint() {
        return (self.end - self.current);
    }
}

// BidirectionalIteratorも明示的に実装
impl<T> ArrayIterator<T> for BidirectionalIterator<T> {
    bool has_prev() {
        return self.current > self.start;
    }

    T prev() {
        if (!self.has_prev()) {
            __panic__("Iterator at start");
        }
        self.current--;
        return *self.current;
    }
}

// RandomAccessIteratorも明示的に実装
impl<T> ArrayIterator<T> for RandomAccessIterator<T> {
    void seek(int index) {
        T* target = self.start + index;
        if (target < self.start || target >= self.end) {
            __panic__("Index out of bounds");
        }
        self.current = target;
    }

    int position() {
        return (self.current - self.start);
    }

    T at(int index) {
        T* target = self.start + index;
        if (target < self.start || target >= self.end) {
            __panic__("Index out of bounds");
        }
        return *target;
    }
}

// ExactSizeIteratorも明示的に実装
impl<T> ArrayIterator<T> for ExactSizeIterator<T> {
    int len() {
        return (self.end - self.current);
    }

    bool is_empty() {
        return self.current >= self.end;
    }
}
```

## 5. for-in文の言語統合

### 5.1 コンパイラの変更

```cpp
// src/hir/lowering/stmt.cpp

HirStmtPtr HirLowering::lower_for_in(ast::ForInStmt& for_in) {
    auto iterable_type = for_in.iterable->type;

    // 明示的にIterableインターフェースを実装しているかチェック
    if (explicitly_implements(iterable_type, "Iterable")) {
        return lower_for_in_with_iterator(for_in);
    }

    // 組み込み型の特別扱い（後方互換性）
    if (is_array_or_slice(iterable_type)) {
        // 配列とスライスは自動的にIterableを実装
        return lower_for_in_with_iterator(for_in);
    }

    error("Type '{}' does not implement Iterable interface",
          type_to_string(iterable_type));
}
```

### 5.2 使用例

```cm
import std::collections::*;

int main() {
    // 配列（自動実装）
    int[5] arr = {1, 2, 3, 4, 5};
    for (int x in arr) {
        println("{x}");
    }

    // Vec（明示的実装）
    Vec<int> vec = vec![10, 20, 30];
    for (int x in vec) {
        println("{x}");
    }

    // HashMap（明示的実装）
    HashMap<string, int> map;
    map.insert("one", 1);
    map.insert("two", 2);

    // ペアのイテレーション
    for (Pair<string, int> kv in map) {
        println("{kv.key}: {kv.value}");
    }

    // キーのみ
    for (string key in map.keys()) {
        println("Key: {key}");
    }

    // LinkedList（明示的実装）
    LinkedList<double> list;
    list.push_back(3.14);
    list.push_back(2.71);

    for (double val in list) {
        println("{val}");
    }

    // カスタム型（明示的実装が必要）
    MyCollection<int> my_coll;
    // for-inを使うにはimpl MyCollection<T> for Iterable<T>が必要
}
```

## 6. イテレータアダプタ

### 6.1 アダプタの明示的実装

```cm
// std/iter/adapters.cm

export struct MapIterator<I: Iterator<T>, T, U> {
    private I source;
    private U (*transform)(T);
}

// MapIteratorも明示的にIteratorを実装
impl<I: Iterator<T>, T, U> MapIterator<I, T, U> for Iterator<U> {
    bool has_next() {
        return self.source.has_next();
    }

    U next() {
        return self.transform(self.source.next());
    }

    U peek() {
        return self.transform(self.source.peek());
    }

    int size_hint() {
        return self.source.size_hint();
    }
}

// ExactSizeの伝播
impl<I: ExactSizeIterator<T>, T, U> MapIterator<I, T, U> for ExactSizeIterator<U> {
    int len() {
        return self.source.len();
    }

    bool is_empty() {
        return self.source.is_empty();
    }
}
```

## 7. 設計の利点

### 明示性の利点

1. **意図の明確化**: どの型がイテレート可能か明確
2. **エラーの早期発見**: インターフェース未実装をコンパイル時検出
3. **ドキュメント性**: コードが自己文書化
4. **拡張性**: 新しいインターフェースを追加しても既存コードに影響なし

### パフォーマンス

1. **ゼロコスト抽象化**: インライン化により実行時オーバーヘッドなし
2. **特殊化**: 具象型ごとの最適化可能
3. **SIMD対応**: 配列イテレータでベクトル化可能

### 型安全性

1. **コンパイル時チェック**: 型の不一致を検出
2. **ジェネリック制約**: `I: Iterator<T>` で制約を表現
3. **所有権の明確化**: `iter()` vs `into_iter()` の区別

## 8. 実装優先順位

### Phase 1: 基礎（必須）
- [ ] ジェネリックインターフェース対応
- [ ] `impl Type for Interface` 構文の完全サポート
- [ ] 配列・スライスへの自動実装

### Phase 2: 標準ライブラリ（高優先度）
- [ ] Vec<T>, LinkedList<T>実装
- [ ] HashMap<K,V>, BTreeMap<K,V>実装
- [ ] 基本的なアダプタ（Map, Filter）

### Phase 3: 高度な機能（中優先度）
- [ ] 並列イテレータ
- [ ] 非同期イテレータ
- [ ] カスタムアダプタ

## まとめ

この設計により、Cm言語は：

1. **Goの明示性**: インターフェースを明示的に宣言・実装
2. **Rustの表現力**: 強力なイテレータアダプタ
3. **C++の効率性**: ゼロコスト抽象化

を兼ね備えたイテレータシステムを実現できます。配列とスライスには自動実装を提供しつつ、その他の型には明示的な `impl for` 実装を要求することで、意図しない暗黙的実装を防ぎます。

---

**作成者:** Claude Code
**ステータス:** 設計改良版