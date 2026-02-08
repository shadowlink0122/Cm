---
title: Ownership
parent: Tutorials
---

[日本語](../../ja/types/ownership.html)

# Types - Ownership and Borrowing

**Difficulty:** 🔴 Advanced  
**Time:** 40 minutes

## 📚 What you'll learn

- Ownership System
- Move Semantics
- Borrowing
- References

---

## What is Ownership?

Cm (since v0.11.0) guarantees memory safety without garbage collection using an ownership system.

**Ownership Rules:**
1. Each value has a single variable that is its "owner"
2. When the owner goes out of scope, the value is dropped
3. Ownership can be explicitly transferred using the `move` keyword
4. After moving, the original variable cannot be used

---

## Move Semantics (v0.11.0+)

The `move` keyword explicitly transfers ownership of a value.

### Using the move Keyword

```cm
int main() {
    // Primitive types can also be moved
    int x = 42;
    int y = move x;  // x's ownership transferred to y

    println("{y}");     // OK: y owns the value
    // println("{x}");  // Error: x is moved and cannot be used

    // After move, reassignment is also forbidden
    // x = 50;  // Error: Cannot assign to moved variable

    return 0;
}
```

### Struct Move

```cm
struct Box { int value; }

int main() {
    Box a = {value: 10};
    Box b = move a;  // Explicit move

    println("{}", b.value); // OK: b is the owner
    // println("{}", a.value); // Error: a is moved

    return 0;
}
```

### Copy vs Move

Primitive types are copied by default unless explicitly moved:

```cm
int main() {
    // Default copy behavior
    int x = 10;
    int y = x;        // Copied (both x and y are valid)
    println("{x} {y}"); // OK: both accessible

    // Explicit move
    int a = 20;
    int b = move a;   // Moved
    println("{b}");    // OK
    // println("{a}"); // Error: a is moved

    return 0;
}
```

---

## Borrowing (Address-based Borrow Counting)

**Important:** In Cm v0.11.0, taking a variable's address with the `&` operator marks that variable as "borrowed".

### How Borrowing Works

```cm
int main() {
    int x = 100;
    int* px = &x;  // Taking address with & marks x as borrowed

    // Restrictions while borrowed:
    // int y = move x;  // Compile error: Cannot move while borrowed
    // x = 200;         // Compile error: Cannot modify while borrowed
    // x++;             // Compile error: Cannot increment while borrowed

    println("*px = {*px}");  // Access through pointer is OK
    return 0;
}
```

### const Pointers vs Regular Pointers

```cm
int main() {
    // const pointer - cannot modify the target
    const int value = 42;
    const int* cptr = &value;
    // *cptr = 50;  // Error: Cannot modify through const pointer

    // Regular pointer - can modify the target
    int mutable = 10;
    int* ptr = &mutable;  // mutable becomes borrowed
    *ptr = 20;  // Modifying through pointer is OK

    // However, direct operations on mutable are blocked:
    // mutable = 30;  // Error: Cannot modify while borrowed
    // mutable++;     // Error: Cannot increment while borrowed

    return 0;
}
```

### Current Limitations

**Note:** The current implementation has these limitations:

1. **Borrow release timing** - Borrows persist until scope exit (manual release not implemented)
2. **Multiple borrows** - Multiple pointers to the same variable are allowed and all counted
3. **Function argument borrowing** - Borrowing through function calls is not tracked

```cm
void use_pointer(int* p) {
    *p = 100;
}

int main() {
    int x = 50;
    use_pointer(&x);  // Borrowing in function calls not tracked
    x = 60;  // This currently doesn't error (implementation limitation)
    return 0;
}
```

---

## Next Steps

✅ Understood Ownership and Move  
✅ Learned how to Borrow  
⏭️ Next, learn about [Lifetimes](lifetimes.html)

---

**Previous:** [Enums](enums.html)  
**Next:** [Lifetimes](lifetimes.html)

---

**Last Updated:** 2026-02-08
