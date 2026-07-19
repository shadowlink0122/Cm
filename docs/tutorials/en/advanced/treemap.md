---
title: TreeMap
parent: Advanced Features
---

[日本語](../../ja/advanced/treemap.html)

# Ordered map with `std::collections::TreeMap`

**Goal:** Store key/value pairs in a balanced binary search tree for O(log n) lookup.
**Level:** 🟡 Intermediate

---

## Overview

`TreeMap<K, V>` is a generic associative container backed by a **self-balancing binary search tree (AVL)**. Lookups and insertions are **O(log n)** — unlike a linear-search map, performance stays good as the number of elements grows. The tree is stored in a fixed-capacity arena of nodes referenced by index (no pointers, no `malloc`), so it behaves identically on every backend (jit/native/wasm/js/ts).

Keys are kept in sorted order, so the key type `K` must be comparable (`<` and `==`). Built-in types like `int` and `string` work directly; structs need `with Ord, Eq`. The value type `V` can be anything.

---

## Usage

```cm
import std::collections::treemap::*;

int main() {
    TreeMap<string, int> m();
    m.put("banana", 2);
    m.put("apple", 1);
    m.put("cherry", 3);

    if (m.contains("apple")) {
        println(m.get("apple"));   // 1
    }
    println(m.size());             // 3

    m.put("apple", 100);           // update existing key
    println(m.get("apple"));       // 100
    return 0;
}
```

### API

| Method | Meaning |
|--------|---------|
| `put(key, val)` | Insert or update. O(log n) |
| `get(key) -> V` | Value for a key (check `contains` first; missing key returns an unspecified value) |
| `contains(key) -> bool` | Whether the key is present. O(log n) |
| `size() -> int` | Number of entries |
| `is_full() -> bool` | Whether the fixed capacity is reached |

---

## Struct keys

Give a struct `with Ord, Eq` to use it as a key — the ordering and equality are derived field by field.

```cm
struct Version with Ord, Eq {
    int major;
    int minor;
}

TreeMap<Version, string> releases();
releases.put(Version { major: 1, minor: 0 }, "first");
releases.put(Version { major: 2, minor: 1 }, "latest");
```

---

## Balance and capacity

Every insert rebalances the tree with AVL rotations, so even inserting already-sorted keys keeps the height logarithmic (100 sorted inserts → height ≈ 7, not 100).

The arena has a fixed capacity (`CAP`, default 128). Exceeding it makes `put` a no-op (check `is_full`). On the native backend, AOT optimization time grows with the capacity (the inline arrays are scalarized by SROA); the jit/js/ts backends have no such cost, so raise `CAP` there when you need larger maps.

---

<!-- nav -->
← Prev: [Advanced - JSON](json.html) | [Contents](../index.html) | Next: [Types](../types/index.html) →
