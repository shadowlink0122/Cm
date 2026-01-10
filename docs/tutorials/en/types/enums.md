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
    println("North: {}", (int)Direction::North); // 0
    println("South: {}", (int)Direction::South); // 10
    return 0;
}
```

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

- [Control Flow](../../basics/control-flow.html)
- [match Expression](../../advanced/match.html)
- [Structs](structs.html)

---

**Previous:** [Structs](structs.html)  
**Next:** [typedef](typedef.html)