---
title: Lambda Expressions
parent: Advanced
---

[日本語](../../ja/advanced/lambda.html)

# Lambda Expressions

**Goal:** Learn how to use lambda expressions (anonymous functions) in Cm.  
**Time:** 15 minutes  
**Difficulty:** 🟡 Intermediate

---

## Overview

A lambda expression lets you define a function inline.

---

## Basic Syntax

```cm
// Basic form: (typed parameter list) => { statements } or (typed parameter list) => expression
int*(int) double_it = (int x) => { return x * 2; };

// Example: expression form without a block (no explicit return)
int*(int) triple = (int x) => x * 3;

int main() {
    println("{double_it(5)}");  // 10
    println("{triple(5)}");     // 15
    return 0;
}
```

---

## Usage

### Assigning to a Variable

```cm
// Assign to a function-pointer variable of type ReturnType*(ParamTypes)
int*(int) double_it = (int x) => { return x * 2; };
const int result = double_it(5);  // 10
```

### Passing to Higher-Order Functions

```cm
int[5] arr = [1, 2, 3, 4, 5];

// Use a lambda with map
int[] doubled = arr.map((int x) => x * 2);   // [2, 4, 6, 8, 10]

// Use a lambda with filter
int[] evens = arr.filter((int x) => x % 2 == 0);   // [2, 4]

// Use a lambda with reduce (fold starting from 0)
int total = arr.reduce((int acc, int x) => acc + x, 0);   // 15
```

---

## Type Inference

Parameter types are not inferred and must always be written explicitly (`(x) => ...` is a syntax error).
The return type of an expression-form lambda, however, is inferred from the expression:

```cm
// The return type is inferred as int from the expression x * 2
int*(int) double_it = (int x) => x * 2;
```

---

## Multiple Parameters

```cm
int*(int, int) add = (int a, int b) => { return a + b; };

println("{add(3, 4)}");  // 7
```

---

## No Return Value

```cm
void*(int) print_it = (int x) => {
    println("Value: {x}");
};

print_it(42);  // "Value: 42"
```

---

## Common Patterns

### Callbacks

```cm
void process(int*(int) callback, int value) {
    println("{callback(value)}");
}

int main() {
    process((int x) => x + 100, 5);  // 105
    return 0;
}
```

### Custom Sort Comparison

```cm
int[4] a = [3, 1, 4, 2];

// Pass a comparison lambda to sortBy for descending order (sort() takes no arguments and is ascending only)
int[] desc = a.sortBy((int x, int y) => y - x);   // [4, 3, 2, 1]
```

---

## Closure captures are value copies (read-only)

When a lambda refers to an outer variable, that variable is **captured by value (copied)**.
Writing to the copy would never affect the original variable, so assignments, compound assignments, and increments/decrements targeting a captured variable are compile errors (writes to struct members or array elements of a captured variable are rejected as well).

```cm
int x = 1;
const void*() f = () => {
    x = 42;  // error: Cannot assign to captured variable 'x' inside a closure
};
```

To mutate an outer variable, capture a pointer and write through it (the copied pointer still points at the original variable, so the write propagates).

```cm
int x = 1;
int* px = &x;
const void*() f = () => { *px = 42; };  // OK: writes through a pointer propagate
f();
println("{x}");  // 42
```

---

## Next Steps

- [Function Pointers](function-pointers.html) - More about functions
- [Slices](slices.html) - More about higher-order functions

---

**Last Updated:** 2026-02-08

---

<!-- nav -->
← Prev: [Advanced - Function Pointers](function-pointers.html) | [Contents](index.html) | Next: [Advanced - String Operations](strings.html) →
