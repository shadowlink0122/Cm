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
Each overloadable operator corresponds to an interface implemented via `impl`.

## Supported Operator Interfaces

| Interface | Operator | Method | Description |
|-----------|----------|--------|-------------|
| **Eq** | `==`, `!=` | `operator bool ==(T other)` | Equality |
| **Ord** | `<`, `>`, `<=`, `>=` | `operator bool <(T other)` | Ordering |
| **Add** | `+` | `operator T +(T other)` | Addition (Planned) |
| **Sub** | `-` | `operator T -(T other)` | Subtraction (Planned) |
| **Mul** | `*` | `operator T *(T other)` | Multiplication (Planned) |
| **Div** | `/` | `operator T /(T other)` | Division (Planned) |
| **Mod** | `%` | `operator T %(T other)` | Remainder (Planned) |

> **Note:** `!=` is auto-derived from `==`, and `>`, `<=`, `>=` are auto-derived from `<`. You don't need to implement them explicitly.

## Eq - Equality

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

int main() {
    Point p1 = Point(10, 20);
    Point p2 = Point(10, 20);
    Point p3 = Point(5, 5);

    if (p1 == p2) {
        println("p1 == p2: true");
    }
    // != is auto-derived from ==
    if (p1 != p3) {
        println("p1 != p3: true");
    }
    return 0;
}
```

## Ord - Ordering

```cm
impl Point for Ord {
    operator bool <(Point other) {
        if (self.x != other.x) {
            return self.x < other.x;
        }
        return self.y < other.y;
    }
}

int main() {
    Point p1 = Point(1, 2);
    Point p2 = Point(3, 4);

    // Implementing < gives you >, <=, >= for free
    if (p1 < p2) { println("p1 < p2"); }
    if (p2 > p1) { println("p2 > p1"); }
    return 0;
}
```

## Add / Sub - Addition & Subtraction (Planned)

> ⚠️ **Note:** Add/Sub operator overloading is not yet supported by the type checker. Planned for a future version.

```cm
struct Vec2 {
    float x;
    float y;
}

impl Vec2 for Add {
    operator Vec2 +(Vec2 other) {
        Vec2 result;
        result.x = self.x + other.x;
        result.y = self.y + other.y;
        return result;
    }
}

impl Vec2 for Sub {
    operator Vec2 -(Vec2 other) {
        Vec2 result;
        result.x = self.x - other.x;
        result.y = self.y - other.y;
        return result;
    }
}

int main() {
    Vec2 a = Vec2(1.0, 2.0);
    Vec2 b = Vec2(3.0, 4.0);

    Vec2 sum = a + b;   // Vec2(4.0, 6.0)
    Vec2 diff = a - b;  // Vec2(-2.0, -2.0)
    return 0;
}
```

## Mul / Div / Mod - Multiplication, Division & Remainder (Planned)

> ⚠️ **Note:** Mul/Div/Mod operator overloading is not yet supported by the type checker.

```cm
struct Number {
    int value;
}

impl Number for Mul {
    operator Number *(Number other) {
        return Number(self.value * other.value);
    }
}

impl Number for Div {
    operator Number /(Number other) {
        return Number(self.value / other.value);
    }
}

impl Number for Mod {
    operator Number %(Number other) {
        return Number(self.value % other.value);
    }
}

int main() {
    Number a = Number(10);
    Number b = Number(3);

    Number prod = a * b;  // Number(30)
    Number quot = a / b;  // Number(3)
    Number rem  = a % b;  // Number(1)
    return 0;
}
```

## with (Auto-Implementation) vs Explicit

### with (Auto-generated)

```cm
// All field comparisons are auto-generated
struct Point with Eq + Ord {
    int x;
    int y;
}
```

### Explicit (Custom Logic)

```cm
struct Point {
    int x;
    int y;
}

// Only compare specific fields
impl Point for Eq {
    operator bool ==(Point other) {
        return self.x == other.x;  // Ignore y
    }
}
```

> 📖 See [with Auto-Implementation](with-keyword.html) for details.

## Implementation Status

| Operator | JIT | LLVM | WASM | JS |
|----------|-----|------|------|-----|
| `==` / `!=` (Eq) | ✅ | ✅ | ✅ | ✅ |
| `<` / `>` / `<=` / `>=` (Ord) | ✅ | ✅ | ✅ | ✅ |
| `+` (Add) | ⬜ | ⬜ | ⬜ | ⬜ |
| `-` (Sub) | ⬜ | ⬜ | ⬜ | ⬜ |
| `*` (Mul) | ⬜ | ⬜ | ⬜ | ⬜ |
| `/` (Div) | ⬜ | ⬜ | ⬜ | ⬜ |
| `%` (Mod) | ⬜ | ⬜ | ⬜ | ⬜ |

---

**Previous:** [with Auto-Implementation](with-keyword.html)  
**Next:** [Function Pointers](function-pointers.html)

---

**Last Updated:** 2026-02-10
