---
title: TreeSet / HashSet
---

[日本語](../../../ja/stdlib/collections/sets.html)

# std::collections - Sets (TreeSet / HashSet)

Two set types for collections of unique values (added in v0.17.2).
`TreeSet<T>` is an **ordered set** backed by a self-balancing binary search tree (AVL), and `HashSet<T>` is an **unordered set** backed by a hash table.

> **Supported backends:** TreeSet runs on JIT / Native / WASM / JS / TS (same slice-arena representation as TreeMap); HashSet is Native (LLVM) only (heap allocation like HashMap).

---

## TreeSet - ordered set

```cm
import std::collections::treeset::*;
import std::io::println;

int main() {
    TreeSet<int> s();

    s.insert(30);
    s.insert(10);
    s.insert(20);
    s.insert(10);  // duplicates are ignored

    println("len: {s.len()}");            // 3
    println("has 20: {s.contains(20)}");  // true

    // all elements in ascending order
    int[] xs = s.values_in_order();
    for (int i = 0; i < xs.len(); i++) {
        println("{xs[i]}");               // 10, 20, 30
    }

    s.remove(20);
    println("len: {s.len()}");            // 2
    return 0;
}
```

| Method | Returns | Description |
|--------|---------|-------------|
| `insert(value)` | `void` | Add a value (duplicates ignored). O(log n) |
| `contains(value)` | `bool` | Membership test. O(log n) |
| `remove(value)` | `void` | Delete a value (rebalances with AVL rotations). O(log n) |
| `len()` / `is_empty()` | `int` / `bool` | Element count / emptiness |
| `clear()` | `void` | Remove all elements |
| `values_in_order()` | `T[]` | All elements in ascending order (in-order traversal) |

The element type `T` needs ordered comparison (`<` and `==`); structs require `with Ord, Eq`.

---

## HashSet - unordered set

```cm
import std::collections::hashset::*;
import std::io::println;

int main() {
    HashSet<int> s();
    s.insert(100);
    s.insert(200);
    s.insert(100);  // duplicates are ignored

    println("len: {s.len()}");              // 2
    println("has 200: {s.contains(200)}");  // true

    s.remove(100);
    println("has 100: {s.contains(100)}");  // false
    return 0;
}
```

| Method | Returns | Description |
|--------|---------|-------------|
| `insert(value)` / `remove(value)` | `void` | Add / delete a value. Average O(1) |
| `contains(value)` | `bool` | Membership test. Average O(1) |
| `len()` / `is_empty()` | `int` / `bool` | Element count / emptiness |
| `clear()` | `void` | Remove all elements |

The element type must be castable to `int` (same simple hash as HashMap). For strings use the content-hashing `StringSet` (`std::collections::strset`, same API plus `values_in_order`, v0.17.2).

---

## Choosing between them

| | TreeSet | HashSet |
|---|---------|---------|
| Search / insert / delete | O(log n) | Average O(1) |
| Element order | Kept ascending (`values_in_order`) | None |
| Element constraint | Ordered comparison (`<`, `==`) | Castable to `int` recommended |
| Backends | All | Native only |

---

**See also:** [TreeMap](treemap.html)

---

<!-- nav -->
← Prev: [TreeMap](treemap.html) | [Contents](../../index.html) | Next: [std::json — JSON parser](../json.html) →
