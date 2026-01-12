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

## Borrowing (Pointer-based in v0.11.0)

**Important:** Current Cm uses pointers for borrowing. C++-style references (`&T`) are not yet implemented.

### Immutable Borrowing (const pointer)

```cm
int main() {
    const int value = 42;
    const int* ref = &value;  // Borrow via const pointer

    println("Value: {*ref}");  // Dereference to access

    // *ref = 50;  // Error: Cannot modify through const pointer

    return 0;
}
```

### Mutable Borrowing (pointer)

```cm
int main() {
    int x = 10;
    int* px = &x;  // Mutable borrow via pointer

    *px = 20;  // Modify through pointer
    println("x = {x}");  // Prints: x = 20

    return 0;
}
```

### Borrowing and Move Restrictions

```cm
int main() {
    int y = 100;
    const int* py = &y;  // y is borrowed

    // int z = move y;  // Error: Cannot move while borrowed
    // y = 200;         // Error: Cannot modify while borrowed

    println("*py = {*py}");

    return 0;
}
```

### Struct Borrowing

```cm
struct Point { int x; int y; }

void print_point(const Point* p) {  // Borrow via const pointer
    println("Point({}, {})", p.x, p.y);  // Auto-dereference
}

int main() {
    Point pt = {x: 10, y: 20};
    print_point(&pt);  // Pass address

    // pt is still valid after borrowing
    println("x: {}", pt.x);

    return 0;
}
```

**Note:** Cm's pointer syntax automatically dereferences for struct field access (`p.x` instead of `(*p).x`).

---

## Next Steps

✅ Understood Ownership and Move  
✅ Learned how to Borrow  
⏭️ Next, learn about [Lifetimes](lifetimes.html)

---

**Previous:** [Enums](enums.html)  
**Next:** [Lifetimes](lifetimes.html)
