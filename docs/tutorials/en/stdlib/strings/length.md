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
- Indices of `substring()` / `slice()` are code-point based (negative indices count from the end). `codepoint_at(i)` returns the Unicode scalar value at code-point index `i` as `uint` (0 when out of range)
- `chars()` returns the code points as a `uint[]` slice; iterate with `for (cp in s.chars())`
- `indexOf()` returns a code-point index (`"あいうえお".indexOf("うえ")` is 2; -1 when not found)
- `charAt(i)` / `at(i)` are **codepoint-indexed** element access (same unit as `len()`; changed from byte units in v0.17.0). The `char` return type is one byte, so only ASCII values are returned faithfully; non-ASCII codepoints and out-of-range return `'\0'`. Use `codepoint_at(i)` when you need non-ASCII values
- Raw byte access is `byte_at(i)` (the byte-series API paired with `byte_len()`; returns the byte value 0..255 as `int` at a byte index, 0 when out of range; added in v0.17.0)
- The JS backend counts surrogate pairs (such as emoji) as one code point
- To build a string from raw bytes, use `std::strings::from_bytes(utiny[])` (byte sequences containing embedded NUL (0x00) are preserved; v0.17.0)
