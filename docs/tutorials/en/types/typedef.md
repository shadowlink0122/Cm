---
title: typedef
parent: Tutorials
---

[日本語](../../ja/types/typedef.html)

# Types - typedef

**Difficulty:** 🟡 Intermediate  
**Time:** 15 minutes

## Basic Type Aliases

```cm
typedef Integer = int;
typedef Real = double;
typedef Text = string;

Integer x = 42;
Real pi = 3.14159;
Text name = "Alice";
```

## Struct Aliases

```cm
struct Point {
    int x;
    int y;
}

typedef Position = Point;

int main() {
    Position pos;
    pos.x = 10;
    pos.y = 20;
    return 0;
}
```

## Union Types

Combining multiple types with `|` in a `typedef` defines a **union type**.  
A union type stores values of different types in a single variable.

```cm
import std::io::println;

// Define a union type that can hold multiple types
typedef Value = string | int | bool;

int main() {
    // Store different types into a union variable
    Value v1 = "hello" as Value;
    Value v2 = 42 as Value;
    Value v3 = true as Value;

    // Cast back to the original type
    string s = v1 as string;
    int n = v2 as int;
    bool b = v3 as bool;

    println("s={s}, n={n}, b={b}");
    // Output: s=hello, n=42, b=true
    return 0;
}
```

> **Note:** Use `as Value` to store a value into the union, and `as string` to extract it back to its original type. Extraction is tag-checked: extracting with a type that does not match the active variant panics at runtime.
> Slice-variant checks and extraction (`v is int[]` / `v as int[]`) are also supported since v0.17.0 (previously `int[]` was a syntax error in this position).
> Variant values can be passed directly to union-typed function parameters (`take_union(9)`; no `as` needed since v0.17.0), and as/is against a typedef alias (`v as IntSlice`) matches by the resolved type.
> Unions with a `null` variant (`int | null`) are constructed with `null as NU` and tested with `n is null`. Storing a fixed-length array into a slice variant (`int[]`) automatically materializes it as a slice (both fixed in v0.17.0).

### Union equality (fixed in v0.17.0)

`==`/`!=` between unions, or between a union and a variant value, compares **tag equality plus the active variant's payload** (string variants compare contents):

```cm
typedef IU = int | string;

IU a = 1 as IU;
IU b = 1 as IU;
IU c = 2 as IU;
IU s = "hello" as IU;

a == b    // true (same variant, same value)
a == c    // false (same variant, different value)
a == s    // false (different variants)
a == 1    // true (direct comparison with a variant value)
a != c    // true
```

> **Note for versions before v0.17.0:** comparisons fell through to raw-representation compares — native/JIT compared tags only (`1 == 2` was true) and JS compared references (`1 == 1` was false) — wrong on every backend.

Unions can also be passed as type arguments of generic structs (`Box<IU>`). Specializations of generic structs with `#[derive(Eq)]` work since v0.17.0, and equality uses the same tag + payload comparison (see [Auto Implementation (with / #[derive])](../advanced/with-keyword.html)).

### Runtime Type Discrimination: the `is` Operator and Match Type Patterns (v0.16.0)

The `is` operator safely discriminates the active variant of a union value at runtime. Check with `is` first, then extract with `as` without panicking.

```cm
import std::io::println;

typedef Value = int | string;

int main() {
    Value v = 42;

    if (v is int) {
        int n = v as int;
        println("int: {n}");
    }

    v = "hello";
    bool s = v is string;
    println("is string: {s}");
    // Output: int: 42 / is string: true
    return 0;
}
```

Match **type patterns** (`TypeName binder`) combine discrimination and extraction in one step.

```cm
import std::io::println;

typedef Value = int | string | bool;

int main() {
    Value v = 42;
    match (v) {
        int i => println("int: {i}"),
        string s => println("str: {s}"),
        bool b => println("bool: {b}"),
        _ => println("other"),
    }
    // Output: int: 42
    return 0;
}
```

> **Note:** The target type of `is` must be one of the union's variants (a type outside the variants is a compile error), and applying `is` to a non-union value is also a compile error.

### Union Arrays (Tuple-like Usage)

Using arrays of union types, you can **group values of different types into a single array**.  
This provides tuple-like behavior (a collection of values with different types).

```cm
import std::io::println;

typedef Value = string | int | bool;

int main() {
    // Group different types in one array (tuple-like usage)
    Value[3] data = [
        "test" as Value,
        999 as Value,
        true as Value
    ];

    // Access by index and cast
    string s = data[0] as string;
    int n = data[1] as int;
    bool b = data[2] as bool;

    println("s={s}, n={n}, b={b}");
    // Output: s=test, n=999, b=true
    return 0;
}
```

### Union Types in Function Arguments and Return Values

Union type arrays can be passed between functions.

```cm
import std::io::println;

typedef Value = string | int | bool;

// Function returning a union array
Value[3] make_values() {
    Value[3] arr = [
        "hello" as Value,
        42 as Value,
        true as Value
    ];
    return arr;
}

// Function receiving a union array
void print_values(Value[3] vals) {
    string s = vals[0] as string;
    int n = vals[1] as int;
    bool b = vals[2] as bool;
    println("s={s}, n={n}, b={b}");
}

int main() {
    Value[3] v = make_values();
    print_values(v);
    // Output: s=hello, n=42, b=true
    return 0;
}
```

## Literal Types

Literal types **restrict the allowed values to specific literals**.  
Invalid assignments are detected at compile time, improving type safety.

```cm
typedef HttpMethod = "GET" | "POST" | "PUT" | "DELETE";
typedef Digit = 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9;
```

### Literal Types in Function Arguments and Return Values

Literal types can be used as function parameters and return types.  
String literal types are treated as `string`, and integer literal types are treated as `int`.

```cm
import std::io::println;

typedef HttpMethod = "GET" | "POST" | "PUT" | "DELETE";
typedef StatusCode = 200 | 400 | 404 | 500;

// Function taking a literal type as argument
void handle_request(HttpMethod method) {
    println("Method: {method}");
}

// Function returning a literal type
HttpMethod get_method() {
    return "GET";
}

// Integer literal type
StatusCode get_status() {
    return 200;
}

int main() {
    handle_request("POST");
    // Output: Method: POST

    HttpMethod m = get_method();
    println("m={m}");
    // Output: m=GET

    StatusCode s = get_status();
    println("s={s}");
    // Output: s=200
    return 0;
}
```

---

**Previous:** [Enums](enums.html)  

---

**Last Updated:** 2026-02-09

---

<!-- nav -->
← Prev: [Types - Enums](enums.html) | [Contents](index.html) | Next: [Types - Generics](generics.html) →
