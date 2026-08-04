---
title: TreeMap
---

[日本語](../../../ja/stdlib/collections/treemap.html)

# std::collections::treemap - Ordered Map

`TreeMap<K, V>` is an ordered key-value map backed by a self-balancing binary search tree (AVL tree), added in v0.17.0.
Lookup and insertion are **O(log n)**, and keys are always managed by ordering comparison.

> **Supported backends:** JIT / Native / WASM / JS / TS (nodes live in an index-based arena, so behavior is identical on every backend)

---

## Basic Usage

```cm
import std::collections::treemap::*;
import std::io::println;

int main() {
    TreeMap<string, int> m();

    m.put("apple", 1);
    m.put("banana", 2);
    m.put("cherry", 3);

    println("size: {m.size()}");                       // 3
    println("apple: {m.get_or(\"apple\", 0)}");        // 1
    println("contains(kiwi): {m.contains(\"kiwi\")}"); // false

    // get returns Option<V> (None when absent)
    Option<int> v = m.get("banana");
    println("banana: {v.unwrap_or(-1)}");              // 2

    m.put("apple", 10);  // existing keys are overwritten
    println("apple: {m.get_or(\"apple\", 0)}");        // 10
    return 0;
}
```

---

## API

| Method | Returns | Description |
|--------|---------|-------------|
| `put(key, value)` | `void` | Insert (overwrites an existing key). O(log n) |
| `get(key)` | `Option<V>` | Fetch (`Some(value)` if present, `None` otherwise). O(log n) |
| `get_or(key, default)` | `V` | Fetch with an explicit default for absent keys. O(log n) |
| `contains(key)` | `bool` | Key existence check. O(log n) |
| `size()` | `int` | Number of entries |

---

## Key Type Requirements

The key type `K` must support ordering comparison (`<` and `==`).

- Built-in types such as `int` / `string` work out of the box
- Struct keys need `with Ord, Eq`
- Types without an ordering cannot be keys (use a HashMap for those)

The value type `V` is unconstrained.

---

<!-- nav -->
[Contents](../../index.html) | Next: [std::json — JSON parser](../json.html) →
