---
title: Character Classification & Number Parsing
---

[日本語](../../../ja/stdlib/strings/chars-parse.html)

# std::strings::chars / parse - Character Classification & Number Parsing

Building blocks for lexers and text processing: single-character classification/conversion (`chars`) and string-to-number parsing (`parse`), added in v0.17.2 (the parse functions moved from std::io).

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

**See also:** [StringBuilder](builder.html) · [String Length](length.html)

---

[Contents](../../index.html)
