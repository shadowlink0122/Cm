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

Cm (since v0.11.0) guarantees memory safety without garbage collection using an ownership system similar to Rust.

**Ownership Rules:**
1.  Each value has a single variable that is its "owner".
2.  When the owner goes out of scope, the value is dropped.
3.  Assignment or passing by value transfers ownership ("Move").

---

## Move

For non-primitive types like structs, assignment moves ownership.

```cm
struct Box { int x; }

int main() {
    Box a = {x: 10};
    Box b = a;  // Ownership moved from a to b
    
    // println("{}", a.x); // Error: a is moved
    println("{}", b.x); // OK: b is the owner
    return 0;
}
```

### Copy

Primitive types like `int` implement `Copy`, so they are copied instead of moved.

```cm
int main() {
    int x = 10;
    int y = x;  // Copied
    return 0;
}
```

---

## Borrowing

To use a value without taking ownership, you can "borrow" it.

### Immutable Reference (`&T`)

Allows reading but not modifying or taking ownership.

```cm
void print_box(const Box& b) { // Borrow
    println("Box({})", b.x);
}

int main() {
    Box a = {x: 10};
    print_box(&a); // Pass reference
    println("{}", a.x); // a is still valid
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
