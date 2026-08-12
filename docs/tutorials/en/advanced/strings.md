---
title: String Operations
parent: Tutorials
---

[日本語](../../ja/advanced/strings.html)

# Advanced - String Operations

**Difficulty:** 🟡 Intermediate  
**Time:** 20 minutes

## String Methods

```cm
int main() {
    string str = "Hello, World!";

    int len = str.len();
    char first = str.charAt(0)  // codepoint index (ASCII only; use byte_at for raw bytes);
    string sub1 = str.substring(0, 5);
    int pos = str.indexOf("World");
    string upper = str.toUpperCase();
    string lower = str.toLowerCase();
    string trimmed = "  text  ".trim();
    bool starts = str.startsWith("Hello");
    bool ends = str.endsWith("!");
    bool contains = str.contains("World");
    string repeated = "Ha".repeat(3);
    string replaced = str.replace("World", "Cm");
    return 0;
}
```

## String Slicing

```cm
int main() {
    string s = "Hello, World!";

    string sub1 = s[0:5];
    string sub2 = s[7:12];
    string tail = s[7:];
    string head = s[:5];
    string copy = s[:];
    string last3 = s[-3:];
    return 0;
}
```

## Escape Sequences and Raw Strings

String and char literals support the following escape sequences (v0.17.0 implements `\x`/`\u`/`\U` decoding; unknown escapes are now compile errors instead of silently dropping the backslash):

| Escape | Meaning |
|---|---|
| `\n` `\t` `\r` `\b` `\f` `\v` `\a` `\0` | control characters |
| `\\` `\"` `\'` | backslash and quotes |
| `\{` `\}` `\$` | interpolation escapes (literal `{` `}` `$`) |
| `\xHH` | one byte (two hex digits) |
| `\uHHHH` / `\UHHHHHHHH` | Unicode code point encoded as UTF-8 |

```cm
string a = "\x41";        // "A" (len=1)
string e = "\u00e9";      // "é" (len=1, byte_len=2)
string g = "\U0001F600";  // "😀"
char c = '\x41';          // 'A' (char literals share the same escape table)
```

Raw strings (backticks) do not interpret escapes: backslashes are kept as-is (handy for Windows paths and regexes). The only exception is the delimiter escape `` \` ``, and interpolation is available via `${expr}` only.

```cm
string path = `C:\path\n`;   // a 9-character literal (\n is not a newline)
string tick = `a\`b`;         // "a`b"
```

---

**Previous:** [Function Pointers](function-pointers.html)  

---

**Last Updated:** 2026-02-08

---

<!-- nav -->
← Prev: [ラムダ式](lambda.html) | [Contents](index.html) | Next: [スライス型](slices.html) →
