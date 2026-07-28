---
title: String Length (len / byte_len)
---

# String Length - len() and byte_len()

Cm strings are UTF-8 encoded.
`len()` returns the number of **code points** (characters), and `byte_len()` returns the number of **UTF-8 bytes**.

> **Supported backends:** JIT / Native / WASM / JS / TS (SV resolves string lengths statically and is not supported)

---

## Basics

```cm
import std::io::println;

int main() {
    string ascii = "hello";
    println(ascii.len());       // 5
    println(ascii.byte_len());  // 5 (equal for ASCII)

    string ja = "こんにちは";
    println(ja.len());          // 5 (code points)
    println(ja.byte_len());     // 15 (3 bytes x 5 chars)

    string emoji = "😀🚀";
    println(emoji.len());       // 2
    println(emoji.byte_len());  // 8 (4 bytes x 2 chars)
    return 0;
}
```

## Notes

- In v0.17.0 the meaning of `len()` changed from byte count to code point count. ASCII-only strings are unaffected. Use `byte_len()` when you need the byte count
- Indices of `charAt()` / `substring()` remain byte-based for now (code-point indexing is planned for a future version)
- The JS backend counts surrogate pairs (such as emoji) as one code point
