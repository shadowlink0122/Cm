---
title: Enums
parent: Tutorials
---

[日本語](../../ja/types/enums.html)

# Types - Enums

**Difficulty:** 🟡 Intermediate  
**Time:** 20 minutes

## 📚 What you'll learn

- Defining Enums
- Accessing members
- Explicit integer values
- Using with switch/match

---

## Defining Enums

An Enum (enumeration) is a way to define a set of named constants.

```cm
enum Status {
    Ok,
    Error,
    Pending
}

int main() {
    Status s = Status::Ok;
    
    if (s == Status::Ok) {
        println("Success!");
    }
    return 0;
}
```

### Values and Auto-increment

Members are assigned integer values automatically starting from 0. You can also provide explicit values.

```cm
enum Color {
    Red = 1,
    Green = 2,
    Blue = 4
}

enum Direction {
    North,      // 0
    East,       // 1
    South = 10,
    West        // 11 (10 + 1)
}

int main() {
    println("North: {Direction::North as int}"); // 0
    println("South: {Direction::South as int}"); // 10
    return 0;
}
```

---

## Enums with Associated Data (Tagged Unions)

**Since v0.13.0**

Cm supports enums where each variant can hold associated data (Tagged Unions).

> **Important:** Each variant can hold **only one field**. Use structs to wrap multiple values.

### Basic Definition

```cm
enum Message {
    Quit,                  // No data
    Write(string),         // Single value
    Code(int)              // Single value
}

int main() {
    Message m1 = Message::Quit;
    Message m2 = Message::Write("Hello");
    Message m3 = Message::Code(404);
    return 0;
}
```

### ⚠️ Multiple fields? Use a struct

```cm
// ❌ Invalid: variants cannot hold multiple fields
// enum Shape {
//     Rectangle(int, int),  // Compile error
// }

// ✅ Correct: wrap in a struct
struct Rect { int w; int h; }
struct Color { int r; int g; int b; }

enum Shape {
    Circle(int),         // radius
    Rectangle(Rect),     // struct for multiple values
    Colored(Color),      // RGB via struct
    Point                // no data
}
```

### Destructuring with match

Extract associated data using the `match` expression.

```cm
struct Rect { int w; int h; }

enum Shape {
    Circle(int),
    Rectangle(Rect),
    Point
}

void describe_shape(Shape s) {
    match (s) {
        Shape::Circle(r) => println("Circle with radius {r}"),
        Shape::Rectangle(rect) => println("Rectangle {rect.w}x{rect.h}"),
        Shape::Point => println("A point"),
    }
}

int main() {
    Shape c = Shape::Circle(5);
    describe_shape(c);  // Circle with radius 5

    Rect r = Rect { w: 10, h: 20 };
    Shape rect = Shape::Rectangle(r);
    describe_shape(rect);  // Rectangle 10x20
    return 0;
}
```

---

## Union Type Arrays as Tuples

Cm has union types defined with `typedef`. Using an array of union types, you can return multiple values of different types — like a tuple.

### Defining Union Types

```cm
typedef Value = int | long;
typedef Number = int | double;
typedef Data = int | string;
```

### Returning Multiple Values

```cm
import std::io::println;

typedef Value = int | long;

// Return quotient and remainder (pair-like)
Value[2] divide_with_remainder(int a, int b) {
    Value[2] pair;
    pair[0] = (a / b) as Value;
    pair[1] = (a % b) as Value;
    return pair;
}

// Return coordinates (3-element tuple-like)
Value[3] get_point() {
    Value[3] point;
    point[0] = 10 as Value;
    point[1] = 20 as Value;
    point[2] = 30 as Value;
    return point;
}

int main() {
    Value[2] dr = divide_with_remainder(17, 5);
    int quotient = dr[0] as int;
    int remainder = dr[1] as int;
    println("17 / 5 = {quotient} remainder {remainder}");

    Value[3] pt = get_point();
    int x = pt[0] as int;
    int y = pt[1] as int;
    int z = pt[2] as int;
    println("Point: ({x}, {y}, {z})");
    return 0;
}
```

> **Note:** Union type values use `as` casts for assignment and extraction. This is a separate mechanism from Tagged Union (enum) `match`.

---

## Result/Option (Built-in Error Handling Types)

**Built-in since v0.16.0**

Error handling uses `Result<T, E>` and optional values use `Option<T>`. Since v0.16.0 they are **built-in types** just like in Rust — no enum definition needed (a user-defined enum with the same name takes precedence).

### Result Type

Represents success or failure with a value or error.

```cm
import std::io::println;

Result<int, string> safe_divide(int a, int b) {
    if (b == 0) {
        return Result::Err("Division by zero");
    }
    return Result::Ok(a / b);
}

int main() {
    Result<int, string> r = safe_divide(10, 2);
    match (r) {
        Result::Ok(v) => { println("Result: {v}"); }
        Result::Err(e) => { println("Error: {e}"); }
    }
    return 0;
}
```

### Option Type

Represents the presence or absence of a value.

```cm
import std::io::println;

Option<int> find_value(int[] arr, int target) {
    for (int i = 0; i < arr.len(); i++) {
        if (arr[i] == target) {
            return Option::Some(i);
        }
    }
    return Option::None;
}

int main() {
    int[] data = [1, 2, 3, 4, 5];
    Option<int> idx = find_value(data, 3);
    match (idx) {
        Option::Some(i) => { println("Found at index {i}"); }
        Option::None => { println("Not found"); }
    }
    return 0;
}
```

### Methods (Rust-aligned)

Discriminate and extract without writing match. `unwrap()` panics with "panic: <message>" on Err/None (like Rust's panic!).

```cm
Result<int, string> r = safe_divide(10, 2);
bool ok = r.is_ok();
bool err = r.is_err();
int v = r.unwrap();               // panics on Err
int v2 = r.unwrap_or(-1);
int v3 = r.expect("must divide"); // panics with the message on Err
// string e = r.unwrap_err();     // panics on Ok

Option<int> o = find_value([1, 2, 3], 2);
bool s = o.is_some();
bool n = o.is_none();
int i = o.unwrap_or(0);
```

### The ? Operator (Error Propagation)

`expr?` yields the payload on Ok/Some, and **early-returns from the current function** on Err/None. `?` on a Result requires the enclosing function to return Result; `?` on an Option requires it to return Option.

```cm
Result<int, string> calc(int a, int b) {
    int q = safe_divide(a, b)?;   // propagates Err to the caller
    int q2 = safe_divide(q, 2)?;
    return Result::Ok(q2 * 10);
}
```

### must_use (Static Check for Unused Results)

Discarding a Result value as a bare statement produces a `[must_use]` compile-time warning (like Rust's unused_must_use). Always handle it with match, is_ok(), etc.

```cm
safe_divide(1, 0);   // warning: unused Result value [must_use]
```

### Runtime Errors and Their Safe APIs

Operations that panic (abnormal termination at runtime) have safe counterparts that can be handled with Result/Option. As in Rust, panics are unrecoverable (cannot be caught), so use the safe API when you need to recover.

| Panicking operation | Safe handling |
|------|------|
| `a / b`, `a % b` (zero divisor) | `std::math::checked_div(a, b)` / `checked_mod(a, b)` → `Option<int>` |
| `arr[i]` (out of bounds: fixed array = undefined value, slice = 0) | `arr.get(i)` → `Option<T>` |
| `v as T` (wrong union variant) | check with `v is T` first, or use match type patterns |
| `r.unwrap()` / `o.unwrap()` | `unwrap_or(default)`, `is_ok()`/`is_some()`, match |
| File I/O failures | `std::fs` Result API (`read_to_string`, etc.) |
| `panic(msg)` / `assert` | intentionally unrecoverable (no handling) |

---

## Control Flow Integration

### Using with switch

Cm's `switch` statement works well with Enums. Use the `case(Enum::Member)` syntax.

```cm
enum Status { Ok, Error, Pending }

void handle_status(Status s) {
    switch (s) {
        case(Status::Ok) {
            println("All good.");
        }
        case(Status::Error) {
            println("Something went wrong.");
        }
        else {
            println("Still waiting...");
        }
    }
}

int main() {
    handle_status(Status::Ok);
    return 0;
}
```

### Using with match (Recommended)

The `match` expression is safer as the compiler checks if all variants are covered.

```cm
enum Status { Ok, Error, Pending }

void handle_status_safe(Status s) {
    match (s) {
        Status::Ok => println("OK"),
        Status::Error => println("Error"),
        Status::Pending => println("Pending"),
    }
}

int main() {
    handle_status_safe(Status::Pending);
    return 0;
}
```

---

## Common Mistakes

### ❌ Forgetting the Scope Operator

You must use `EnumName::MemberName` to access members.

```cm
enum Status { Ok }

int main() {
    // Status s = Ok;  // Error: Ok not found
    Status s = Status::Ok; // Correct
    return 0;
}
```

### ❌ Assignment Between Different Enums

Enums with different names are distinct types and cannot be directly assigned to each other.

```cm
enum A { X }
enum B { X }

int main() {
    A val_a = A::X;
    // B val_b = val_a; // Error: Type mismatch
    return 0;
}
```

---

## Practice Problems

### Problem 1: Day Enum
Create an Enum `Day` representing Monday to Sunday. Implement a function `is_weekend` that returns true if the day is Saturday or Sunday.

<details>
<summary>Example Answer</summary>

```cm
enum Day {
    Monday, Tuesday, Wednesday, Thursday, Friday, Saturday, Sunday
}

bool is_weekend(Day d) {
    match (d) {
        Day::Saturday | Day::Sunday => true,
        _ => false,
    }
}

int main() {
    Day today = Day::Saturday;
    if (is_weekend(today)) {
        println("It's weekend!");
    }
    return 0;
}
```
</details>

---

## Next Steps

✅ Understood Enum definition and usage  
✅ Learned how to use switch/match with Enums  
⏭️ Next, learn about [typedef Aliases](typedef.html)

## Related Links

- [Control Flow](../basics/control-flow.html)
- [match Expression](../advanced/match.html)
- [Structs](structs.html)

---

**Previous:** [Structs](structs.html)  
---

**Last Updated:** 2026-02-08

---

<!-- nav -->
← Prev: [Types - Structs](structs.html) | [Contents](index.html) | Next: [Types - typedef](typedef.html) →
