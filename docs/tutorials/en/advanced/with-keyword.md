---
title: Auto Implementation (with / #[derive])
parent: Tutorials
---

[日本語](../../ja/advanced/with-keyword.html)

# Auto Implementation (with / #[derive])

Automatic interface implementation (auto-derive) for structs is specified with either the `with` keyword or the `#[derive(...)]` attribute.
They are **exactly the same feature** — the only difference is the spelling (the Rust-style `#[derive]` is recommended for new code).

## 📋 Table of Contents

- [Basic Usage](#basic-usage)
- [Derivable Interfaces](#derivable-interfaces)
- [Multiple Interfaces](#multiple-interfaces)
- [Generic Structs](#generic-structs)
- [Supported Field Types](#supported-field-types)
- [Invalid Usages](#invalid-usages)
- [How it Works](#how-it-works)

## Basic Usage

```cm
// Recommended: #[derive(...)] attribute
#[derive(Eq)]
struct Point {
    int x;
    int y;
}

// The classic with syntax remains valid (same meaning)
struct Color with Eq {
    int r;
    int g;
    int b;
}

int main() {
    Point p1;
    p1.x = 10;
    p1.y = 20;
    Point p2;
    p2.x = 10;
    p2.y = 20;

    if (p1 == p2) {  // auto-generated == operator
        println("Equal!");
    }
    return 0;
}
```

## Derivable Interfaces

Only the 8 compiler built-ins are derivable (implement user-defined interfaces with `impl <type> for <interface>`).

| Interface | Description | Generated members |
|---------|------|-------------------------|
| **Eq** | Equality | `==`, `!=` |
| **Ord** | Ordering | `<`, `>`, `<=`, `>=` |
| **Copy** | Bitwise copy | (marker only) |
| **Clone** | Deep copy | `.clone()` |
| **Hash** | Hashing | `.hash()` |
| **Debug** | Debug output | `.debug()` |
| **Display** | Stringify | `.toString()` |
| **Css** | CSS generation (js/web only) | `.css()`, `.to_css()`, `.isCss()` |

Nested struct fields are handled recursively (comparison, hashing, and formatting descend into the nested struct).

## Multiple Interfaces

Interfaces are comma-separated.
Multiple `#[derive]` attributes are merged, and mixing with `with` is allowed (specifying the same interface twice is an error).

```cm
#[derive(Eq, Ord, Clone)]
struct Point {
    int x;
    int y;
}

// Multiple derive attributes are merged
#[derive(Eq)]
#[derive(Clone, Hash)]
struct Entry {
    int key;
    int value;
}

// Mixing with `with` (the lists are unioned)
#[derive(Ord)]
struct Item with Eq {
    int priority;
}
```

## Generic Structs

```cm
#[derive(Eq)]
struct Pair<T, U> {
    T first;
    U second;
}
```

Auto implementations for generic structs are generated per instantiation after monomorphization.
In addition to the Eq/Ord operators, the Clone/Hash/Debug/Display methods (`clone()`/`hash()`/`debug()`/`toString()`) are callable on specialized receivers such as `G<int>`.
Type arguments are validated against the table below at every instantiation, so unsupported combinations (e.g. Eq on `Box<int[]>`) become compile errors at the usage site.

## Supported Field Types

| Field type | Eq | Ord | Hash | Debug/Display | Clone/Copy |
|---|---|---|---|---|---|
| integers / bool / char | ✅ | ✅ | ✅ | ✅ | ✅ |
| float / double | ✅ | ✅ | ❌ | ✅ | ✅ |
| string | ✅ | ✅ | ❌ | ✅ | ✅ |
| nested struct | ✅ | ✅ | ✅ | ✅ | ✅ |
| value enum (no payload) | ✅ | ✅ | ✅ as int | ✅ as int | ✅ |
| fixed-size 1-D array | ✅ | ❌ | ✅ integer elements only | ❌ | ✅ |
| multi-dim array / slice / union / payload enum | ❌ | ❌ | ❌ | ❌ | ✅ |

Combinations marked ❌ produce a compile error (never invalid code generation). For generic structs the same rules are checked on the field types after substituting the type arguments.
Value-enum fields use int semantics: Debug/Display format them as their numeric value (e.g. `c: 5`).

## Invalid Usages

```cm
#[derive(Foo)]        // error: unknown interface
#[derive(Greet)]      // error: not derivable (use impl P for Greet)
#[derive(Eq, Eq)]     // error: duplicate
#[derive]             // error: interface name required
struct P { int x; }

#[derive(Eq)]
enum Color { Red }    // error: derive on enums is not supported yet
```

## How it Works

`with` and `#[derive]` merge into the same auto-implementation list in the parser, and implementation functions (such as `Point__op_eq`) are generated during MIR lowering.
Unused auto implementations are removed by dead-code elimination, so an unused derive costs nothing.

## Related

- [Interfaces](../types/interfaces.html)
- [Operator Overloading](../advanced/operators.html)
- [Canonical Spec](../../../design/CANONICAL_SPEC.html)
- [Design 10: #[derive] attribute](../../../archive/v0.16.0/10_derive_attribute.html)

---

**Last Updated:** 2026-07-11

---

<!-- nav -->
← Prev: [Advanced - match Expression](match.html) | [Contents](index.html) | Next: [Advanced - Operator Overloading](operators.html) →
