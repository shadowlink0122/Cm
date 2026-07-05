---
title: String Interpolation
parent: Tutorials
---

[日本語](../../ja/basics/string-interpolation.html)

# Basics - String Interpolation

Cm string literals support interpolation with `{}`.
This page covers embedding variables, expressions, and function calls, plus format specifiers.

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

> **Note (known limitation):** Nested calls like `{f(g(x))}` and
> binary expressions like `{a + b}` are not yet supported.
> Assign to a local variable first, then embed the variable.

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

## Escaping Braces

To output literal `{` `}`, write `{{` `}}`.

```cm
println("JSON: {{\"key\": {value}}}");
// when value=42 → JSON: {"key": 42}
```

## Interpolation on the SV Backend

On the SystemVerilog target, strings are treated as packed vector constants, so
`println`-style interpolation is only usable in limited contexts such as simulation `initial` blocks.
See the [SV Backend Semantic Guarantees](../compiler/sv/semantics.html) for details.

---

← [Variables and Types](variables.html) | [Operators](operators.html) →
