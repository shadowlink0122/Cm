---
title: std::json
---

[日本語](../../ja/stdlib/json.html)

# std::json — JSON Parser

A JSON parse / access / serialize module implemented in pure Cm, added in v0.17.0.
The JSON tree is represented by a node-array arena with integer indices — no recursive types or pointers — so it behaves identically on every backend.

> **Supported backends:** JIT / Native / WASM / JS / TS

---

## Basic Usage

```cm
import std::json::*;
import std::io::println;

int main() {
    string text = "{\"name\": \"cm\", \"version\": 17, \"tags\": [\"fast\", \"typed\"]}";

    int root = json_parse(text);
    if (root < 0) {
        println("parse error");
        return 1;
    }

    // object field access (returns a node index; negative when absent)
    int name = json_object_get(root, "name");
    println("name: {json_string(name)}");        // cm

    int ver = json_object_get(root, "version");
    println("version: {json_int(ver)}");         // 17

    // array iteration
    int tags = json_object_get(root, "tags");
    for (int i = 0; i < json_array_len(tags); i++) {
        int tag = json_array_get(tags, i);
        println("tag[{i}]: {json_string(tag)}");
    }

    // compact serialization
    println(json_stringify(root));
    return 0;
}
```

---

## API

| Function | Returns | Description |
|----------|---------|-------------|
| `json_parse(text)` | `int` | Parse and return the root node index (-1 on failure / capacity overflow) |
| `json_kind(node)` | `JsonKind` | Node kind (Null/Bool/Number/String/Array/Object) |
| `json_is_null(node)` | `bool` | Whether the node is null |
| `json_bool(node)` | `bool` | Boolean value |
| `json_number(node)` | `double` | Numeric value |
| `json_int(node)` | `int` | Numeric value as int |
| `json_string(node)` | `string` | String value |
| `json_array_len(node)` | `int` | Array length |
| `json_array_get(node, i)` | `int` | Node index of an array element |
| `json_object_get(node, key)` | `int` | Node index of an object field (negative when absent) |
| `json_object_has(node, key)` | `bool` | Key existence check |
| `json_stringify(node)` | `string` | Serialize to a compact JSON string |

---

## Notes

- Supports string escapes (`\" \\ \/ \n \t \r \b \f`), signed / decimal / exponent numbers, nesting, and whitespace
- Node capacity defaults to 1024 (a fixed-size global node array); `json_parse` returns -1 beyond that — grow the capacity constant in the source if needed
- Nodes are referenced by `int` indices; check `json_object_has` or a negative index before accessing absent keys

---

<!-- nav -->
← Prev: [TreeMap](collections/treemap.html) | [Contents](../index.html)
