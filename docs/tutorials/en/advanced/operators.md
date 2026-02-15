---
title: Operator Overloading
parent: Tutorials
---

[日本語](../../ja/advanced/operators.html)

# Advanced - Operator Overloading

**Difficulty:** 🔴 Advanced  
**Time:** 30 minutes

## Overview

In Cm, you can overload operators for structs using the `operator` keyword.
Operators are defined directly inside `impl T { ... }` blocks.

## Supported Operators

### Comparison Operators

| Operator | Signature | Description |
|----------|-----------|-------------|
| `==`, `!=` | `operator bool ==(T other)` | Equality (`!=` auto-derived) |
| `<`, `>`, `<=`, `>=` | `operator bool <(T other)` | Ordering (`>`, `<=`, `>=` auto-derived) |

### Arithmetic Operators

| Operator | Signature | Description |
|----------|-----------|-------------|
| `+` | `operator T +(T other)` | Addition |
| `-` | `operator T -(T other)` | Subtraction |
| `*` | `operator T *(T other)` | Multiplication |
| `/` | `operator T /(T other)` | Division |
| `%` | `operator T %(T other)` | Remainder |

### Bitwise Operators

| Operator | Signature | Description |
|----------|-----------|-------------|
| `&` | `operator T &(T other)` | Bitwise AND |
| `\|` | `operator T \|(T other)` | Bitwise OR |
| `^` | `operator T ^(T other)` | Bitwise XOR |
| `<<` | `operator T <<(T other)` | Left shift |
| `>>` | `operator T >>(T other)` | Right shift |

> **Note:** `!=` is auto-derived from `==`, and `>`, `<=`, `>=` are auto-derived from `<`.

## Basic Usage

Operators are defined inside `impl T { ... }` blocks:

```cm
struct Vec2 {
    int x;
    int y;
}

impl Vec2 {
    operator Vec2 +(Vec2 other) {
        return Vec2{x: self.x + other.x, y: self.y + other.y};
    }

    operator Vec2 -(Vec2 other) {
        return Vec2{x: self.x - other.x, y: self.y - other.y};
    }

    operator Vec2 *(Vec2 other) {
        return Vec2{x: self.x * other.x, y: self.y * other.y};
    }
}

int main() {
    Vec2 a = Vec2{x: 10, y: 20};
    Vec2 b = Vec2{x: 3, y: 7};

    Vec2 sum = a + b;   // Vec2{13, 27}
    Vec2 diff = a - b;  // Vec2{7, 13}
    Vec2 prod = a * b;  // Vec2{30, 140}
    return 0;
}
```

## Comparison Operators

Comparison operators are defined via `impl T for Eq` / `impl T for Ord`:

```cm
struct Point {
    int x;
    int y;
}

impl Point for Eq {
    operator bool ==(Point other) {
        return self.x == other.x && self.y == other.y;
    }
}

impl Point for Ord {
    operator bool <(Point other) {
        if (self.x != other.x) {
            return self.x < other.x;
        }
        return self.y < other.y;
    }
}

int main() {
    Point p1 = Point{x: 1, y: 2};
    Point p2 = Point{x: 3, y: 4};

    if (p1 == p2) { println("equal"); }
    if (p1 < p2) { println("p1 < p2"); }
    return 0;
}
```

> **Note:** `Eq`/`Ord` are built-in interfaces, so `impl T for Eq` syntax works. Arithmetic and bitwise operators use `impl T` syntax.

## Bitwise Operators

```cm
struct Bits {
    int value;
}

impl Bits {
    operator Bits &(Bits other) {
        return Bits{value: self.value & other.value};
    }

    operator Bits |(Bits other) {
        return Bits{value: self.value | other.value};
    }

    operator Bits ^(Bits other) {
        return Bits{value: self.value ^ other.value};
    }

    operator Bits <<(Bits other) {
        return Bits{value: self.value << other.value};
    }

    operator Bits >>(Bits other) {
        return Bits{value: self.value >> other.value};
    }
}
```

## Compound Assignment Operators

Once a binary operator is defined, the corresponding compound assignment operators (`+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`) are automatically available.

```cm
int main() {
    Vec2 v = Vec2{x: 10, y: 20};
    v += Vec2{x: 5, y: 3};   // equivalent to v = v + Vec2{5, 3}
    v -= Vec2{x: 2, y: 1};   // equivalent to v = v - Vec2{2, 1}
    v *= Vec2{x: 3, y: 2};   // equivalent to v = v * Vec2{3, 2}
    return 0;
}
```

## with (Auto-Implementation) vs Explicit

```cm
// All field comparisons are auto-generated
struct Point with Eq + Ord {
    int x;
    int y;
}
```

> 📖 See [with Auto-Implementation](with-keyword.html) for details.

---

**Previous:** [with Auto-Implementation](with-keyword.html)  
**Next:** [Function Pointers](function-pointers.html)

---

**Last Updated:** 2026-02-10
