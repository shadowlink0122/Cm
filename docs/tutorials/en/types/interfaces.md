---
title: Interfaces
parent: Tutorials
---

[日本語](../../ja/types/interfaces.html)

# Types - Interfaces

**Difficulty:** 🔴 Advanced  
**Time:** 30 minutes

## 📚 What you'll learn

- Interface definition
- Interface implementation (impl)
- Dynamic dispatch and abstraction
- Private methods

---

## What is an Interface?

An interface defines the signature (name, arguments, return type) of methods that a struct must have. This allows different structs to be treated uniformly.

```cm
interface Drawable {
    void draw();
}

interface Printable {
    void print();
}
```

---

## Implementing Interfaces

Use the `impl StructName for InterfaceName` syntax to implement methods.

```cm
struct Circle {
    int radius;
}

impl Circle for Drawable {
    void draw() {
        println("Drawing a circle with radius {}", self.radius);
    }
}

struct Square {
    int side;
}

impl Square for Drawable {
    void draw() {
        println("Drawing a square with side {}", self.side);
    }
}
```

### About `self`

Inside an interface implementation, `self` is treated as a pointer to the target struct (e.g., `*Circle`). Therefore, you can access and modify fields using `self.field`.

---

## Using Interfaces

### Method Calls

You can call the methods on an instance of a struct that implements the interface.

```cm
int main() {
    Circle c;
    c.radius = 10;
    c.draw();  // Calls Circle's draw
    
    Square s;
    s.side = 5;
    s.draw();  // Calls Square's draw
    
    return 0;
}
```

---

## Implementing Multiple Interfaces

A single struct can implement multiple interfaces.

```cm
impl Circle for Printable {
    void print() {
        println("Circle(radius={})", self.radius);
    }
}

int main() {
    Circle c;
    c.radius = 10;
    c.draw();
    c.print();
    return 0;
}
```

---

## Private Methods

You can define auxiliary methods that can only be used within the `impl` block. These are not part of the interface signature but are useful for organizing common logic.

```cm
interface Calculator {
    int calculate(int x);
}

struct MyCalc {
    int base;
}

impl MyCalc for Calculator {
    // Only callable within this impl block
    private int helper(int n) {
        return n * 2;
    }
    
    int calculate(int x) {
        // Call private method via self
        return self.helper(x) + self.base;
    }
}
```

---

## Common Mistakes

### ❌ Signature Mismatch

It is an error if the method signature in the implementation differs from the interface definition.

```cm
interface Foo {
    void bar(int x);
}

struct S {}

impl S for Foo {
    // void bar(string s) { ... } // Error: Type mismatch
    void bar(int x) { ... }      // OK
}
```

### ❌ Missing Implementation

You must implement all methods declared in the interface.

---

## Practice Problems

### Problem 1: Abstracting Area
Create a `Shape` interface with an `area()` method. Then implement it for `Rectangle` and `Circle` structs.

<details>
<summary>Example Answer</summary>

```cm
interface Shape {
    double area();
}

struct Rectangle {
    double w, h;
}

impl Rectangle for Shape {
    double area() { return self.w * self.h; }
}

struct Circle {
    double radius;
}

impl Circle for Shape {
    double area() { return 3.14159 * self.radius * self.radius; }
}
```
</details>

---

## Next Steps

✅ Can define interfaces  
✅ Can implement interfaces  
✅ Understand usage of self pointer  
⏭️ Next, learn about [Type Constraints](constraints.html)

## Related Links

- [Structs](structs.html) - Inherent Impl
- [Type Constraints](constraints.html) - Constraints using interfaces
- [Generics](generics.html) - Handling abstract types

---

**Previous:** [Generics](generics.html)  
**Next:** [Type Constraints](constraints.html)