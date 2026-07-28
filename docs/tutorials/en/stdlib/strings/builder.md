---
title: StringBuilder
---

# std::strings::builder - StringBuilder (mutable string buffer)

`StringBuilder` is a mutable buffer that appends strings in amortized O(1).
`s = s + "..."` inside a loop copies the whole string every time (O(n²)); `StringBuilder` builds the same result in O(n).

> **Supported backends:** JIT / Native / WASM / JS / TS (SV is not supported)

---

## Basic Usage

```cm
import std::strings::StringBuilder;
import std::io::println;

int main() {
    StringBuilder sb();      // constructor call
    sb.append("hello");
    sb.append(", ");
    sb.append("world");
    println(sb.to_string()); // hello, world
    println(sb.len());       // 12 (current byte length, O(1))
    return 0;
}
```

## Building in a Loop (avoiding O(n²))

```cm
import std::strings::StringBuilder;
import std::io::println;

int main() {
    StringBuilder sb();
    int i = 0;
    while (i < 50000) {
        sb.append("{i},");   // interpolation appends numbers too
        i++;
    }
    string result = sb.to_string();
    println(result.len());
    return 0;
}
```

The same code written as `s = s + "{i},"` takes quadratic time in N.
Use `StringBuilder` for heavy appending.

## API

| Method | Description |
|--------|-------------|
| `self()` | Create an empty buffer |
| `void append(string s)` | Append to the end (amortized O(1)) |
| `string to_string()` | Return the current content as a new string (builder remains usable) |
| `long len()` | Current byte length (O(1)) |
| `void clear()` | Empty the content (capacity is kept) |

The buffer is freed automatically by the destructor at scope exit.
The string returned by `to_string()` is owned by the caller.
