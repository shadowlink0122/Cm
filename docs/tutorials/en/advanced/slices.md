---
title: Slices
parent: Advanced
---

[日本語](../../ja/advanced/slices.html)

# Slices

**Goal:** learn the growable slice type and its higher-order functions.
**Time:** 20 min
**Level:** 🟡 Intermediate

---

## Overview

The slice type `T[]` is a growable array with mutation operations (push/pop, etc.) and higher-order functions.

---

## Creating Slices

```cm
int main() {
    // Empty slice + push
    int[] xs = [];
    xs.push(10);
    xs.push(20);

    // Literal initialization
    int[] ys = [1, 2, 3];

    // From a fixed-size array (elements are copied)
    int[5] arr = [1, 2, 3, 4, 5];
    int[] zs = arr;

    // Subslices
    int[] mid = arr[1:4];   // [2, 3, 4]
    int[] head = arr[:2];   // [1, 2]
    int[] tail = arr[3:];   // [4, 5]
    return 0;
}
```

---

## Reading and Writing Elements

```cm
import std::io::println;

int main() {
    int[] xs = [1, 2, 3];
    int a = xs[0];   // read
    xs[1] = 99;      // write
    println("x1={xs[1]}");
    return 0;
}
```

---

## Slice-Only Methods

In addition to the array methods (see the [Arrays](../basics/arrays.html) method reference), slices provide:

| Method | Returns | Description |
|---|---|---|
| `.push(v)` | `void` | Append an element |
| `.pop()` | `T` | Remove and return the last element |
| `.remove(i)` / `.delete(i)` | `void` | Remove the element at index i |
| `.clear()` | `void` | Remove all elements |
| `.cap()` / `.capacity()` | `int` | Allocated capacity |

```cm
import std::io::println;

int main() {
    int[] xs = [];
    xs.push(10);
    xs.push(20);
    xs.push(30);

    int p = xs.pop();      // 30
    xs.remove(0);          // [20]
    println("len={xs.len()} cap={xs.cap()}");
    xs.clear();            // []
    return 0;
}
```

Any element type works (primitives, string, structs, union types, nested slices `T[][]`).
Struct and union elements are stored by value — mutating the source variable after push does not affect the slice.

---

## Higher-Order Functions

`map` / `filter` / `reduce` / `find` / `findIndex` / `some` / `every` / `forEach` / `sort` / `sortBy` / `reverse` / `first` / `last` are available (shared with fixed-size arrays).

```cm
import std::io::println;

bool is_even(int x) {
    return x % 2 == 0;
}

int main() {
    int[] nums = [1, 2, 3, 4, 5];

    int[] doubled = nums.map((int x) => { return x * 2; });    // [2, 4, 6, 8, 10]
    int[] evens = nums.filter(is_even);                        // [2, 4]
    int total = nums.reduce((int acc, int x) => { return acc + x; }, 0);  // 15
    int found = nums.find(is_even);                            // 2
    bool has_even = nums.some(is_even);                        // true
    bool all_even = nums.every(is_even);                       // false
    println("total={total} found={found}");
    return 0;
}
```

### Method Chaining

Higher-order functions can be chained (lambda parameters require type annotations, and the `reduce` argument order is `reduce(function, initial)`):

```cm
int[10] numbers = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];

int total = numbers
    .filter((int x) => { return x % 2 == 0; })  // [2, 4, 6, 8, 10]
    .map((int x) => { return x * x; })          // [4, 16, 36, 64, 100]
    .reduce((int acc, int x) => { return acc + x; }, 0);
// 220
```

Method calls with function arguments (such as `{xs.some(fn)}`) also work directly inside string interpolation.

---

## Slices of Structs

```cm
struct Person {
    string name;
    int age;
}

Person[] people = [
    Person { name: "Alice", age: 30 },
    Person { name: "Bob", age: 25 },
    Person { name: "Carol", age: 35 }
];

// Collect names of people aged 30 or older
string[] names = people
    .filter((Person p) => { return p.age >= 30; })
    .map((Person p) => { return p.name; });
// ["Alice", "Carol"]
```

> Backend note: higher-order methods on scalar elements (tiny/short/int/long/float/double and their unsigned variants) work on all backends. Higher-order methods on struct slices (filter/map/...) and mutation of global or struct-member slices are js/ts-only (they map to JS arrays); on native/jit/wasm they are a compile error (they used to misbehave without any diagnostic). On native/jit struct slices support the basic operations (push/len/indexing/for-in/passing as argument or return value/updating an element field/first/last). Use `--target=ts` for growable struct data such as web development.

---

## Slices of Union Types

A union element type lets values of different types coexist in one slice.
Since the actual type of each element is not statically known, extract with `as` (extracting as the wrong type stops with a runtime error).

```cm
import std::io::println;

typedef Value = int | string;

int main() {
    Value[] vals = [];
    vals.push(42 as Value);
    vals.push("hello" as Value);

    int n = vals[0] as int;
    string s = vals[1] as string;
    println("n={n} s={s}");
    return 0;
}
```

---

## Passing Fixed-Size Arrays to Slice Parameters

Fixed-size arrays can be passed directly to slice-typed (`T[]`) parameters. The array contents are copied into a heap slice at the call site, so `push` or element assignments in the callee do not affect the caller's array.

```cm
int sum(int[] xs) {
    int s = 0;
    for (int i = 0; i < xs.len(); i++) {
        s = s + xs[i];
    }
    return s;
}

int main() {
    int[3] fixed = [1, 2, 3];
    println("{sum(fixed)}");
    // 6 (a copy is passed; mutations inside sum do not change fixed)
    return 0;
}
```

## Next Steps

- [Arrays](../basics/arrays.html) - array basics and the method reference
- [Lambdas](lambda.html)

---

**Last Updated:** 2026-07-12

---

<!-- nav -->
← Prev: [Advanced - Strings](strings.html) | [Contents](index.html) | Next: [FFI (Foreign Function Interface)](ffi.html) →
