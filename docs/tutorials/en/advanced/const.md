---
title: Constants and Constant Expressions
parent: Advanced
nav_order: 5
---

# Constants and Constant Expressions (const)

Cm uses the `const` keyword to define compile-time constants. Since v0.12.0, you can use constant variables and expressions for array sizes.

## Basic Usage

### Defining const Variables

```cm
const int MAX_SIZE = 100;
const double PI = 3.14159;
const bool DEBUG = true;

int main() {
    println("MAX_SIZE = {MAX_SIZE}");  // MAX_SIZE = 100
    println("PI = {PI}");              // PI = 3.14159
    return 0;
}
```

### Using const for Array Sizes

```cm
const int SIZE = 10;
const int DOUBLE_SIZE = SIZE * 2;  // Constant expressions are evaluated

int main() {
    int[SIZE] arr1;           // int[10]
    int[DOUBLE_SIZE] arr2;    // int[20]
    
    println("arr1.size() = {arr1.size()}");  // 10
    println("arr2.size() = {arr2.size()}");  // 20
    
    return 0;
}
```

## Constant Expressions

Expressions evaluated at compile time are called constant expressions.

### Supported Operations

| Type | Operators |
|-----|-----------|
| Arithmetic | `+`, `-`, `*`, `/`, `%` |
| Bitwise | `&`, `\|`, `^`, `<<`, `>>` |
| Comparison | `==`, `!=`, `<`, `>`, `<=`, `>=` |
| Logical | `&&`, `\|\|`, `!` |
| Ternary | `? :` |

### Examples

```cm
const int A = 10;
const int B = 20;

// Arithmetic
const int SUM = A + B;        // 30
const int PRODUCT = A * B;    // 200

// Bitwise
const int SHIFTED = A << 2;   // 40

// Ternary
const int MAX = A > B ? A : B;  // 20

// Use in array size
int[SUM] data;  // int[30]
```

## Limitations

Current implementation has the following limitations:

- Only global scope `const` variables are supported
- Function calls cannot be included in constant expressions
- `constexpr` functions planned for future versions

## Related Topics

- [Variables](../basics/variables.md) - Variable basics
- [Arrays](../basics/arrays.md) - Array usage

---

**Last Updated:** 2026-02-08
