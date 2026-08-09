---
title: String Interpolation
parent: Tutorials
---

[日本語](../../ja/basics/string-interpolation.html)

# Basics - String Interpolation

Cm string literals support interpolation with `{}`. This page covers embedding variables, expressions, and function calls, plus format specifiers.

---

## Basics: Embedding Variables

```cm
import std::io::println;

int main() {
    string name = "Alice";
    int age = 25;
    println("Hello, {name}! You are {age} years old.");
    // → Hello, Alice! You are 25 years old.
    return 0;
}
```

## Embedding Expressions

Member access and array elements can also be embedded.

```cm
struct Point { int x; int y; }

int main() {
    Point p;
    p.x = 3;
    p.y = 4;
    int[3] arr = [10, 20, 30];
    println("p = ({p.x}, {p.y}), arr[1] = {arr[1]}");
    // → p = (3, 4), arr[1] = 20
    return 0;
}
```

Any expression works in a placeholder (v0.17.0). Expressions starting with a numeric, array, or string literal, as well as ternary operators, are parsed with the same grammar as ordinary expressions.

```cm
int main() {
    bool ok = true;
    println("{2 + 3}");            // → 5 (expression starting with a number)
    println("{[1, 2, 3].len()}");  // → 3 (starting with an array literal)
    println("{ok ? 1 : 0}");       // → 1 (ternary; the : is not mistaken for a format specifier)
    return 0;
}
```

## Embedding Function Calls (fixed and extended in v0.15.1)

Functions can be called inside interpolations. **Variable arguments, multiple arguments, and negative literals** are supported.

```cm
int add3(int a, int b, int c) {
    return a + b + c;
}

int is_big(int status) {
    if (status >= 500) { return 1; }
    return 0;
}

int main() {
    int s = 503;
    int x = 10;
    println("check: {is_big(s)}");        // → check: 1 (variable argument)
    println("sum: {add3(x, 20, -1)}");    // → sum: 29 (multiple arguments, negative literal)
    return 0;
}
```

**Extended in v0.16.0**: Nested calls like `{f(g(x))}`, expressions like `{a + b}` or `{xs[1] * 10}`, and method calls (including Result/Option methods such as `{xs.some(fn)}` and `{o.unwrap_or(-1)}`) can now be embedded.

```cm
int main() {
    Option<int> o = Option::Some(5);
    int a = 3;
    int b = 4;
    println("sum: {a + b}");             // → sum: 7
    println("value: {o.unwrap_or(-1)}"); // → value: 5
    return 0;
}
```

## Scope Checking (v0.16.0)

Variable references inside placeholders are scope-checked at compile time just like expressions in statements. Referencing an out-of-scope or undefined variable is a compile error (previously it was unchecked and printed an undefined value).

```cm
int main() {
    {
        int b = 42;
        println("in: {b}");   // OK
    }
    println("out: {b}");      // error: Undefined variable 'b' in interpolation placeholder '{b}'
    return 0;
}
```

## Format Specifiers

The `{variable:specifier}` form lets you specify a radix and more.

```cm
int main() {
    int value = 255;
    println("hex: {value:x}");    // → hex: ff
    println("HEX: {value:X}");    // → HEX: FF
    println("bin: {value:b}");    // → bin: 11111111
    println("oct: {value:o}");    // → oct: 377
    double pi = 3.14159;
    println("pi: {pi:.2}");       // → pi: 3.14 (2 decimal places)
    return 0;
}
```

Width, alignment, zero-padding, and scientific notation are also supported (output unified across all backends in v0.17.0; the semantics follow C/printf):

```cm
int main() {
    int n = 255;
    double pi = 3.14159265;
    println("[{n:6}]");      // → [   255] (width 6; numbers right-align by default)
    println("[{n:<6}]");     // → [255   ] (left)
    println("[{n:^6}]");     // → [ 255  ] (center)
    println("[{n:06}]");     // → [000255] (zero pad; negatives keep the sign first: -00042)
    println("[{n:*>6}]");    // → [***255] (custom fill)
    println("[{n:8x}]");     // → [      ff] (width + radix combined)
    println("{pi:.2e}");     // → 3.14e+00 (scientific, 2-digit precision, 2-digit exponent)
    println("{pi:e}");       // → 3.141593e+00 (default precision is 6)
    return 0;
}
```

## Escaping Braces and Combining with Interpolation

To output literal `{` `}`, write `{{` `}}`.
A placeholder that is not a valid expression (a typo like `{x +}`) is printed literally and produces a compile-time warning (an error with `--strict`); added in v0.17.0 — previously an uninitialized value was printed with no diagnostic.
**As of v0.17.0, escaping and interpolation can be combined in every form**: you can embed placeholders inside escaped braces, as in `{{ ... {value} ... }}`.

```cm
import std::io::println;

int main() {
    int val = 42;
    string name = "cm";

    // placeholders inside escaped braces
    println("{{text {val} ...}}");   // → {text 42 ...}
    println("{{{val}}}");            // → {42}  ({{ + {val} + }})

    // JSON template
    println("json: {{\"key\": {val}, \"name\": \"{name}\"}}");
    // → json: {"key": 42, "name": "cm"}

    // CSS template (plain string literals interpolate the same way)
    string css = "css {{ width: {val}px; }}";
    println(css);                    // → css { width: 42px; }
    return 0;
}
```

**Interpolation works in every string literal (v0.17.0)**: placeholders are evaluated not only in `println` arguments but also in ordinary string literals such as `string s = "sum: {x + 1}";`.
Whenever you need literal braces, escape them with `{{` `}}` regardless of context (`string braces = "{{x}}";` yields the string `{x}`). `\{` `\}` are equivalent escapes.
To emit a `${x}`-style placeholder literally, escape the leading `$` with `\$` (`"\${x}"` yields the string `${x}`).

**Known limitation**: if an interpolated value itself contains `{` or `}`, later placeholders in the same string may be mis-detected (for example, embedding a variable whose content is `{x}` can break the placeholders that follow).
When embedding values that contain braces, build the string with `+` concatenation instead.

## Interpolation on the SV Backend

On the SystemVerilog target, strings are treated as packed vector constants, so `println`-style interpolation is only usable in limited contexts such as simulation `initial` blocks. See the [SV Backend Semantic Guarantees](../compiler/sv/semantics.html) for details.

---

<!-- nav -->
← Prev: [Basics - Modules](modules.html) | [Contents](index.html) | Next: [Type System](../types/index.html) →
