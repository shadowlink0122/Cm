---
title: Character Classification, Parsing & Formatting
---

[日本語](../../../ja/stdlib/strings/chars-parse.html)

# std::strings - Classification, Parsing, Formatting, Hashing & Interning

Building blocks for lexers and text processing: single-character classification/conversion (`chars`), string-to-number parsing (`parse`), number-to-radix-string formatting (`format`), content hashing (`hash`) and string interning (`intern`), added in v0.17.2 (the parse functions moved from std::io).

> **Supported backends:** chars runs everywhere (`digit_value` excluded on SV); parse runs everywhere except SV (Option-returning APIs).

---

## std::strings::chars

```cm
import std::io::println;
import std::strings::chars::*;

int main() {
    println("{is_digit('7')}");        // true
    println("{is_hex_digit('F')}");    // true
    println("{is_ident_start('_')}");  // true
    println("{to_upper('a')}");        // A

    int v = digit_value('f', 16).unwrap_or(-1);
    println("{v}");                    // 15
    return 0;
}
```

| Function | Returns | Description |
|----------|---------|-------------|
| `is_digit(c)` / `is_alpha(c)` / `is_alnum(c)` | `bool` | Digit / letter / alphanumeric |
| `is_space(c)` | `bool` | Whitespace (space, tab, LF, CR) |
| `is_upper(c)` / `is_lower(c)` | `bool` | Upper / lower case |
| `is_hex_digit(c)` | `bool` | Hex digit (0-9 / a-f / A-F) |
| `is_ident_start(c)` / `is_ident_continue(c)` | `bool` | Identifier head / continuation character |
| `to_upper(c)` / `to_lower(c)` | `char` | ASCII case conversion (others unchanged) |
| `digit_value(c, base)` | `Option<int>` | Digit value in base 2-36 (`None` when out of range; not on SV) |

---

## std::strings::parse

String-to-number parsing that reports failure via `Option`.
Moved from `std::io` in v0.17.2; existing code importing via `import std::io::*;` keeps working through a re-export.

```cm
import std::io::println;
import std::strings::parse::*;

int main() {
    int a = parse_int("-123").unwrap_or(0);
    int hex = parse_int_radix("ff", 16).unwrap_or(-1);   // 255
    bool bad = parse_int("abc").is_none();               // true
    println("{a} {hex} {bad}");
    return 0;
}
```

| Function | Returns | Description |
|----------|---------|-------------|
| `parse_int(s)` / `parse_long(s)` | `Option<int>` / `Option<long>` | Decimal integer parsing (leading `-` supported, stops at non-digits) |
| `parse_int_radix(s, base)` / `parse_long_radix(s, base)` | `Option<int>` / `Option<long>` | Integer parsing in base 2-36 (v0.17.2) |
| `parse_double(s)` | `Option<double>` | Floating-point parsing |
| `parse_bool(s)` | `Option<bool>` | `true/1/yes` → `Some(true)`, `false/0/no` → `Some(false)`, otherwise `None` |

**Note:** when using `chars` / `parse` from inside library modules, use wildcard imports (`import std::strings::parse::*;`) — selective imports type-check the target bodies eagerly and leak type collisions into programs that redefine `Option`.

---

## std::strings::format

The reverse of `parse`: number-to-string in a given radix, plus width padding for code generation output.

```cm
import std::strings::format::*;

to_hex(255)                     // "ff"
to_radix(35 as long, 36)        // "z"
pad_left(to_hex(255), 4, '0')   // "00ff"
```

| Function | Description |
|----------|-------------|
| `to_radix(v, base)` | Radix 2-36 (lowercase, `-` prefix for negatives, exact for long min; empty string for invalid radix) |
| `to_hex(v)` / `to_bin(v)` / `to_oct(v)` | Hex / binary / octal |
| `pad_left(s, width, fill)` / `pad_right` | Width padding (returned unchanged when already wide enough) |

---

## std::strings::hash

FNV-1a 32-bit content hash; identical contents hash identically regardless of how the string was built. The foundation of `StringMap` / `StringSet` / `Interner`.

```cm
import std::strings::hash::*;
int h = hash_string("hello");   // non-negative, content-based
```

---

## std::strings::intern

Assigns a unique integer id (symbol) to each distinct string content — the basis for compiler symbol tables; id comparison is O(1).

```cm
import std::strings::intern::*;

Interner it();
const int a = it.intern("foo");      // 0 (newly numbered)
const int b = it.intern("fo" + "o"); // 0 (same content, same id)
string s = it.name_of(a);            // "foo"
```

| Method | Description |
|--------|-------------|
| `intern(s)` / `contains(s)` | Get-or-assign id / check without interning |
| `name_of(id)` / `len()` | Reverse lookup (empty when out of range) / symbol count |

---

**See also:** [StringBuilder](builder.html) · [String Length](length.html)

---

[Contents](../../index.html)
