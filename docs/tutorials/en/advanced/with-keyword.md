---
title: with Keyword
parent: Tutorials
---

[日本語](../../ja/advanced/with-keyword.html)

# Advanced - with Keyword (Auto Implementation)

The `with` keyword is a feature redesigned from Rust's `#[derive(...)]` to be more C++-like, providing automatic implementation of interfaces.

## 📋 Table of Contents

- [Basic Usage](#basic-usage)
- [Supported Traits](#supported-traits)
- [Eq - Equality](#eq---equality)
- [Ord - Ordering](#ord---ordering)
- [Clone - Cloning](#clone---cloning)
- [Hash - Hashing](#hash---hashing)
- [Multiple Traits](#multiple-traits)
- [Generic Structs](#generic-structs)
- [How it Works](#how-it-works)

---

## Basic Usage

```cm
// Automatic Eq implementation
struct Point with Eq {
    int x;
    int y;
}

int main() {
    Point p1 = Point(10, 20);
    Point p2 = Point(10, 20);

    if (p1 == p2) {  // Automatically generated == operator
        println("Equal!");
    }
    return 0;
}
```

## Supported Traits

| Trait | Description | Generated Method/Operator | Status |
|-------|-------------|---------------------------|--------|
| **Eq** | Equality | `==`, `!=` | ✅ Complete |
| **Ord** | Ordering | `<`, `>`, `<=`, `>=` | ✅ Complete |
| **Clone** | Deep Copy | `.clone()` | ✅ Complete |
| **Hash** | Hashing | `.hash()` | ✅ Complete |
| **Copy** | Bitwise Copy | (Implicit copy) | ✅ Marker |
| **Debug** | Debug Output | `.debug()` | ⬜ Future |
| **Display** | Stringify | `.toString()` | ⬜ Future |

---

## Eq - Equality

### Usage

```cm
struct Point with Eq {
    int x;
    int y;
}

int main() {
    Point a = Point(1, 2);
    Point b = Point(1, 2);
    Point c = Point(3, 4);

    bool same = (a == b);      // true
    bool different = (a != c);  // true
    return 0;
}
```

### Generated Code

```cm
// Equivalent to manual implementation:
impl Point for Eq {
    operator bool ==(Point other) {
        return self.x == other.x && self.y == other.y;
    }
}

// != is automatically derived:
// a != b  ->  !(a == b)
```

---

## Ord - Ordering

### Usage

```cm
struct Person with Ord {
    int age;
    string name;
}

int main() {
    Person p1 = Person(25, "Alice");
    Person p2 = Person(30, "Bob");

    if (p1 < p2) {  // Lexicographical comparison (age then name)
        println("p1 is younger");
    }
    return 0;
}
```

### Derived Operators

```cm
// If < is implemented, others are derived:
// a > b   ->  b < a
// a <= b  ->  !(b < a)
// a >= b  ->  !(a < b)

struct Number with Ord {
    int value;
}

int main() {
    Number a = Number(10);
    Number b = Number(20);

    bool lt = (a < b);   // true
    bool gt = (a > b);   // false
    bool le = (a <= b);  // true
    bool ge = (a >= b);  // false
    return 0;
}
```

---

## Clone - Cloning

### Usage

```cm
struct Point with Clone {
    int x;
    int y;
}

int main() {
    Point p1 = Point(10, 20);
    Point p2 = p1.clone();  // Deep copy

    p2.x = 30;  // p1 is unchanged
    return 0;
}
```

---

## Hash - Hashing

```cm
struct Point with Hash {
    int x;
    int y;
}

int main() {
    Point p = Point(10, 20);
    uint hash_value = p.hash();  // Combines field hashes
    return 0;
}
```

---

## Multiple Traits

### Combining with `+`

```cm
struct Point with Eq + Ord + Clone {
    int x;
    int y;
}

// Automatically implements:
// - operator ==
// - operator <
// - clone() method
```

---

## Generic Structs

```cm
struct Pair<T> with Eq + Clone {
    T first;
    T second;
}

// Generated impl:
// impl<T> Pair<T> for Eq {
//     operator bool ==(Pair<T> other) {
//         return self.first == other.first 
//             && self.second == other.second;
//     }
// }
```

---

## How it Works

### MIR Level Generation

The `with` keyword generates implementation functions during MIR lowering.

```cm
struct Point with Eq { ... }

// -> Generates:
// Point__op_eq(Point& self, Point& other) -> bool
//   tmp0 = self.x == other.x
//   tmp1 = self.y == other.y
//   return tmp0 && tmp1
```

### Dead Code Elimination

Unused auto-implementations are removed by dead code elimination.

---

## Comparison with Manual Implementation

### Using `with`

```cm
struct Point with Eq {
    int x;
    int y;
}
// Compares all fields
```

### Manual Implementation

```cm
struct Person {
    int id;
    string name;
}

// Compare only by ID
impl Person for Eq {
    operator bool ==(Person other) {
        return self.id == other.id;  // Ignore name
    }
}
```

---

## Next Steps

✅ Learned how to use `with`  
✅ Understood supported traits  
⏭️ Next, learn about [Operator Overloading](operators.html)

## Related Links

- [Interfaces](../types/interfaces.html)
- [Operator Overloading](operators.html)

---

**Previous:** [match Expression](match.html)  
**Next:** [Operator Overloading](operators.html)

---

**Last Updated:** 2026-02-08
