---
title: TreeSet / HashSet
---

[English](../../../en/stdlib/collections/sets.html)

# std::collections - 集合（TreeSet / HashSet）

重複しない値の集まりを扱う2つの集合型です（v0.17.2で追加）。
`TreeSet<T>` は自己平衡二分探索木（AVL木）による**順序付き集合**、`HashSet<T>` はハッシュ表による**順序なし集合**です。

> **対応バックエンド:** TreeSetはJIT / Native / WASM / JS / TS（TreeMapと同じスライスのアリーナ表現）、HashSetはNative (LLVM) のみ（HashMapと同じヒープ確保）

---

## TreeSet - 順序付き集合

```cm
import std::collections::treeset::*;
import std::io::println;

int main() {
    TreeSet<int> s();

    s.insert(30);
    s.insert(10);
    s.insert(20);
    s.insert(10);  // 重複は無視される

    println("len: {s.len()}");            // 3
    println("has 20: {s.contains(20)}");  // true

    // 昇順で全要素を取得
    int[] xs = s.values_in_order();
    for (int i = 0; i < xs.len(); i++) {
        println("{xs[i]}");               // 10, 20, 30
    }

    s.remove(20);
    println("len: {s.len()}");            // 2
    return 0;
}
```

### API一覧

| メソッド | 戻り値 | 説明 |
|---------|--------|------|
| `insert(value)` | `void` | 値を追加（重複は無視）。O(log n) |
| `contains(value)` | `bool` | 値が含まれるか。O(log n) |
| `remove(value)` | `void` | 値を削除（AVL回転で平衡を維持）。O(log n) |
| `len()` | `int` | 要素数 |
| `is_empty()` | `bool` | 空か |
| `clear()` | `void` | 全要素を削除 |
| `values_in_order()` | `T[]` | 昇順の全要素（in-order走査） |

値型 `T` は順序比較（`<` と `==`）が必要です（構造体は `with Ord, Eq`）。

---

## HashSet - 順序なし集合

```cm
import std::collections::hashset::*;
import std::io::println;

int main() {
    HashSet<int> s();

    s.insert(100);
    s.insert(200);
    s.insert(100);  // 重複は無視される

    println("len: {s.len()}");              // 2
    println("has 200: {s.contains(200)}");  // true

    s.remove(100);
    println("has 100: {s.contains(100)}");  // false
    return 0;
}
```

### API一覧

| メソッド | 戻り値 | 説明 |
|---------|--------|------|
| `insert(value)` | `void` | 値を追加（重複は無視）。平均O(1) |
| `contains(value)` | `bool` | 値が含まれるか。平均O(1) |
| `remove(value)` | `void` | 値を削除。平均O(1) |
| `len()` | `int` | 要素数 |
| `is_empty()` | `bool` | 空か |
| `clear()` | `void` | 全要素を削除 |

値型 `T` は `int` にキャスト可能な型**専用**です（HashMapと同じ簡易ハッシュ）。文字列は内容ハッシュの `StringSet`（`std::collections::strset`・API同一＋`values_in_order`）を使ってください（v0.17.2）。

---

## 使い分け

| | TreeSet | HashSet |
|---|---------|---------|
| 探索・挿入・削除 | O(log n) | 平均O(1) |
| 要素の順序 | 昇順を維持（`values_in_order`） | なし |
| 値の制約 | 順序比較（`<`・`==`） | `int` にキャスト可能な型が推奨 |
| 対応バックエンド | 全バックエンド | Nativeのみ |

順序付き走査や範囲的な用途はTreeSet、存在判定だけを高速に行いたい場合はHashSetが向いています。

---

**関連:** [TreeMap](treemap.html) · [HashMap](hashmap.html) · [Vector](vector.html)

---

<!-- nav -->
← 前: [std::collections::treemap - 順序付きマップ](treemap.html) ｜ [目次](../index.html) ｜ 次: [std::json — JSONパーサ](../json.html) →
