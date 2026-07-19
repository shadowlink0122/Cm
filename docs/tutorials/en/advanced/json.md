---
title: JSON
parent: Advanced Features
---

[日本語](../../ja/advanced/json.html)

# JSON parsing with `std::json`

**Goal:** Parse, inspect, and serialize JSON entirely in Cm.
**Level:** 🟡 Intermediate

---

## Overview

`std::json` is a JSON parser written in pure Cm. It stores the (recursive) JSON tree in a fixed-capacity arena of nodes referenced by index, so it uses no recursive types or pointers and behaves identically on every backend (jit/native/wasm/js/ts). Nodes are referred to by an `int` index; `json_parse` returns the root index, or `-1` on error.

---

## Parsing and access

```cm
import std::json::*;

int main() {
    int root = json_parse("{\"name\":\"Cm\",\"nums\":[1,2,30],\"ok\":true}");
    if (root < 0) { println("parse error"); return 1; }

    int name = json_object_get(root, "name");
    println(json_string(name));                 // Cm

    int nums = json_object_get(root, "nums");
    println(json_array_len(nums));              // 3
    println(json_int(json_array_get(nums, 2))); // 30

    println(json_bool(json_object_get(root, "ok")));  // true
    return 0;
}
```

### API

| Function | Meaning |
|----------|---------|
| `json_parse(text) -> int` | Parse; returns root node index, or `-1` on error |
| `json_kind(node) -> JsonKind` | `Null` / `Bool` / `Number` / `String` / `Array` / `Object` |
| `json_bool` / `json_number` / `json_int` / `json_string` | Read a scalar value |
| `json_is_null(node)` | Is this a JSON null |
| `json_array_len(node)` / `json_array_get(node, i)` | Array length / i-th element index (`-1` if out of range) |
| `json_object_get(node, key)` / `json_object_has(node, key)` | Object member index (`-1` if absent) / membership |
| `json_stringify(node) -> string` | Serialize back to compact JSON |

---

## Serializing

```cm
int root = json_parse("{\"a\":[1,2],\"b\":\"x\"}");
println(json_stringify(root));   // {"a":[1,2],"b":"x"}
```

---

## Capacity

The arena holds up to 48 nodes by default (one node per JSON value). This bound keeps the library compiling under every backend's default optimizer; on the LLVM backends (native/jit) a large global aggregate makes optimization/codegen cost explode. Exceeding the bound makes `json_parse` return `-1`. On js/ts there is no such constraint, so a larger arena can be used when needed.

---

<!-- nav -->
← Prev: [Advanced - Testing](testing.html) | [Contents](../index.html) | Next: [Types](../types/index.html) →
