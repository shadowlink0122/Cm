---
title: Arrays
parent: Tutorials
---

[日本語](../../ja/basics/arrays.html)

# Arrays

Cm supports C++-style fixed-size arrays.
For the growable slice type `T[]`, see [Slices](../advanced/slices.html).

## 📋 Table of Contents

- [Basic Usage](#basic-usage)
- [Element Access](#element-access)
- [Method Reference](#method-reference)
- [Search Methods](#search-methods)
- [Higher-Order Methods](#higher-order-methods)
- [Sorting and First/Last](#sorting-and-firstlast)
- [Arrays of Structs](#arrays-of-structs)
- [Pointer Decay](#pointer-decay)
- [for-in Loops](#for-in-loops)
- [Multidimensional Arrays](#multidimensional-arrays)

## Basic Usage

```cm
// Basic declaration (zero-initialized)
int[5] numbers;

// Declaration with initialization
int[3] values = [1, 2, 3];

// Partial initialization (rest are 0)
int[5] partial = [1, 2];  // [1, 2, 0, 0, 0]
```

## Element Access

```cm
int main() {
    int[5] numbers;
    int first = numbers[0];
    numbers[1] = 10;
    return 0;
}
```

> **Since v0.11.0**: bounds checking is enabled. Out-of-range access stops the program safely (panic).

## Method Reference

Methods available on fixed-size arrays `T[N]` (all of them also work on slices `T[]`).

| Method | Returns | Description |
|---|---|---|
| `.size()` / `.len()` / `.length()` | `int` | Number of elements |
| `.dim()` | `int` | Number of dimensions (for multidimensional arrays) |
| `.indexOf(v)` | `int` | Position of first match (-1 if none) |
| `.includes(v)` / `.contains(v)` | `bool` | Whether the value is present |
| `.find(fn)` | `T` | First element matching the predicate |
| `.findIndex(fn)` | `int` | Index of first match (-1 if none) |
| `.some(fn)` | `bool` | Whether any element matches |
| `.every(fn)` | `bool` | Whether all elements match |
| `.map(fn)` | `T[]` | New slice with transformed elements |
| `.filter(fn)` | `T[]` | New slice with matching elements |
| `.reduce(fn, init)` | `T` | Fold (argument order: function, initial) |
| `.forEach(fn)` | `void` | Apply a function to each element |
| `.sort()` | `T[]` | New slice sorted ascending |
| `.sortBy(cmp)` | `T[]` | New slice sorted by comparator |
| `.reverse()` | `T[]` | New slice in reverse order |
| `.first()` / `.last()` | `T` | First / last element |
| `arr[a:b]` | `T[]` | Subslice (see [Slices](../advanced/slices.html)) |

Methods taking a function accept named functions or lambdas (parameter type annotations required).

## Search Methods

```cm
int main() {
    int[5] numbers = [1, 2, 3, 4, 5];

    int pos = numbers.indexOf(3);        // 2
    int not_found = numbers.indexOf(10); // -1

    bool has_3 = numbers.includes(3);    // true
    bool has_5 = numbers.contains(5);    // true (alias of includes)
    return 0;
}
```

## Higher-Order Methods

```cm
import std::io::println;

bool is_even(int x) {
    return x % 2 == 0;
}

int add(int acc, int x) {
    return acc + x;
}

int main() {
    int[5] numbers = [1, 2, 3, 4, 5];

    // Lambdas require parameter type annotations
    bool has_even = numbers.some((int x) => { return x % 2 == 0; });  // true
    bool all_positive = numbers.every((int x) => { return x > 0; });  // true
    int idx = numbers.findIndex((int x) => { return x > 3; });        // 3 (index of 4)

    // Named functions also work
    int[] evens = numbers.filter(is_even);   // [2, 4]
    int[] doubled = numbers.map((int x) => { return x * 2; });  // [2, 4, 6, 8, 10]
    int total = numbers.reduce(add, 0);      // 15 (argument order: function, initial)

    bool s = numbers.some(is_even);
    println("some={s} total={total}");
    return 0;
}
```

Note (as of v0.16.0): using method calls with function arguments directly inside string interpolation (`{numbers.some(fn)}`) has a known limitation — assign the result to a variable first.

## Sorting and First/Last

```cm
int main() {
    int[5] nums = [3, 1, 4, 1, 5];

    int[] sorted = nums.sort();       // [1, 1, 3, 4, 5] (original unchanged)
    int[] rev = nums.reverse();       // [5, 1, 4, 1, 3]
    int[] desc = nums.sortBy((int a, int b) => { return b - a; });  // descending

    int f = nums.first();  // 3
    int l = nums.last();   // 5
    return 0;
}
```

## Arrays of Structs

```cm
struct Point {
    int x;
    int y;
}

int main() {
    // Declaration
    Point[3] points;

    // Field assignment
    points[0].x = 10;
    points[0].y = 20;

    // Struct literal initialization
    Point[2] pts = [
        Point { x: 1, y: 2 },
        Point { x: 3, y: 4 }
    ];
    return 0;
}
```

## Pointer Decay

Arrays convert to pointers automatically.

```cm
int main() {
    int[5] arr = [1, 2, 3, 4, 5];

    // Array-to-pointer conversion
    int* p = arr;  // address of arr[0]

    int first = *p;  // 1
    return 0;
}
```

## for-in Loops

```cm
import std::io::println;

int main() {
    int[5] numbers = [1, 2, 3, 4, 5];

    // With explicit type
    for (int n in numbers) {
        println("{n}");
    }

    // With type inference
    for (n in numbers) {
        println("{n}");
    }
    return 0;
}
```

## Multidimensional Arrays

```cm
int main() {
    // 2D array
    int[3][4] matrix;

    matrix[0][0] = 1;
    matrix[0][1] = 2;

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) {
            matrix[i][j] = i * 4 + j;
        }
    }

    int d = matrix.dim();  // 2 (number of dimensions)
    return 0;
}
```

### Performance (since v0.11.0)

Multidimensional arrays are automatically flattened internally for cache locality (transparent to user code; 200-250x speedups on large matrices).

## Backend Support

| Backend | Status |
|------------|------|
| JIT / LLVM Native | ✅ Full support |
| WASM | ✅ Full support |
| JS | ✅ Full support |
| SV | ⚠️ Fixed arrays map to RAM/ROM inference (methods within the synthesizable subset) |

## Related

- [Slices](../advanced/slices.html) - growable slices, push/pop, etc.
- [Pointers](pointers.html)
- [for-in](control-flow.html)

---

**Last Updated:** 2026-07-12

---

<!-- nav -->
← Prev: [Basics - Functions](functions.html) | [Contents](index.html) | Next: [Basics - Pointers](pointers.html) →
