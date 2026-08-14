---
title: Structs
parent: Tutorials
---

[日本語](../../ja/types/structs.html)

# Types - Structs

**Difficulty:** 🟡 Intermediate  
**Time:** 30 minutes

## 📚 What you'll learn

- Struct definition and usage
- Field access
- Constructors
- Nested structs
- Struct arrays

---

## Defining Structs

### Basic Definition

```cm
struct Point {
    int x;
    int y;
}

struct Rectangle {
    int width;
    int height;
    string color;
}

struct Person {
    string name;
    int age;
    double height;
}
```

### Type Size

The size of a struct is the sum of the sizes of all its fields (plus padding).

```cm
struct Point {
    int x;    // 4 bytes
    int y;    // 4 bytes
}
// Total: 8 bytes
```

---

## Initialization

### Field Initialization

```cm
Point p1;
p1.x = 10;
p1.y = 20;

println("Point: ({p1.x}, {p1.y})");
```

### Default Values

Uninitialized fields have indeterminate values unless a constructor is used.

```cm
struct Config {
    int timeout;
    bool debug;
}

int main() {
    Config cfg;
    cfg.timeout = 30;
    cfg.debug = false;
    return 0;
}
```

**Note:** Struct literal initialization (e.g., `Rectangle{100, 50, "blue"}`) is currently not supported.

---

## Constructors

### Default Constructor

```cm
struct Point {
    int x;
    int y;
}

impl Point {
    self() {
        self.x = 0;
        self.y = 0;
    }
}

int main() {
    Point origin;  // Initialized to (0, 0)
    println("({origin.x}, {origin.y})");
    return 0;
}
```

### Constructor with Arguments

```cm
impl Point {
    overload self(int x, int y) {
        self.x = x;
        self.y = y;
    }
}

int main() {
    Point p1;           // Default: (0, 0)
    Point p2(10, 20);   // With args: (10, 20)
    return 0;
}
```

### Multiple Constructors

```cm
struct Rectangle {
    int width;
    int height;
}

impl Rectangle {
    // Default: 1x1
    self() {
        self.width = 1;
        self.height = 1;
    }
    
    // Square
    overload self(int size) {
        self.width = size;
        self.height = size;
    }
    
    // Rectangle
    overload self(int w, int h) {
        self.width = w;
        self.height = h;
    }
}

int main() {
    Rectangle r1;        // 1x1
    Rectangle r2(5);     // 5x5
    Rectangle r3(3, 4);  // 3x4
    return 0;
}
```

---

## Implementing Methods

### Inherent Impl

You can define methods directly on a struct without an interface. `self` is treated as a pointer to the struct, allowing modification of fields.

```cm
struct Point {
    int x;
    int y;
}

impl Point {
    // Constructor
    self(int x, int y) {
        this.x = x;
        this.y = y;
    }

    // Method (self becomes *Point automatically)
    void move(int dx, int dy) {
        // self.x is equivalent to (*self).x (Auto-dereference)
        self.x += dx;
        self.y += dy;
    }

    void print() {
        println("Point({self.x}, {self.y})");
    }
}

int main() {
    Point p(10, 20);
    p.print();      // Point(10, 20)
    
    p.move(5, 5);   // Modifies state
    p.print();      // Point(15, 25)
    
    return 0;
}
```

### Interface Implementation

Use interfaces when you want to share behavior.

```cm
interface Printable {
    void print();
}

impl Point for Printable {
    void print() {
        println("Point({self.x}, {self.y})");
    }
}
```

---

## Nested Structs

### Struct Fields

```cm
struct Point {
    int x;
    int y;
}

struct Line {
    Point start;
    Point end;
}

int main() {
    Line line;
    line.start.x = 0;
    line.start.y = 0;
    line.end.x = 100;
    line.end.y = 100;
    
    return 0;
}
```

### Nested Type Declarations (struct/enum inside a struct)

You can declare another struct/enum type inside a struct body.
A nested type is an independent type that belongs to the outer type's namespace, and it is referenced from outside with the qualified path `Outer::Inner`.

```cm
struct Outer {
    struct Inner {
        int mem;
    };
    enum Mode {
        FAST,
        SLOW,
    }
    Inner inner;
    Mode mode;
}

int main() {
    Outer o;
    o.inner.mem = 42;            // access through a member chain
    o.mode = Outer::Mode::FAST;  // nested enum values use a qualified chain

    Outer::Inner i;              // use the qualified path outside the struct
    i.mem = 99;
    return 0;
}
```

Nesting depth is arbitrary, so multi-segment paths such as `Outer::Mid::Inner` work as well.
You can also define methods on a nested type with `impl Outer::Inner { ... }`.

### C/C++-Style Single Declarations (declarators)

As in C/C++, you can declare variables or fields together with the type definition.
Anonymous structs/enums require a declarator.

```cm
// Top level: the declarator becomes a zero-initialized global variable
struct {
    struct {
        int mem;
    } str;
} STR;

// Inside a struct body: declarators become fields (comma-separated lists allowed)
struct Outer {
    struct Inner {
        int mem;
    } a, b;
    enum { FAST, SLOW } mode;
};

int main() {
    STR.str.mem = 42;   // anonymous nesting is accessible through member chains
    println(STR.str.mem);

    Outer o;
    o.a.mem = 1;
    o.b.mem = 2;
    return 0;
}
```

**Limitations:**

- Nested type declarations are not allowed inside a generic struct/enum
- Generic parameters are not supported on nested type declarations, and declarators cannot be attached to generic type declarations
- Initializers on declarators (`struct X {} g = ...;`) are not supported (write `X g = ...;` as a separate statement)

---

## Arrays of Structs

```cm
struct Point {
    int x;
    int y;
}

int main() {
    Point[3] points;
    
    points[0].x = 0;
    points[0].y = 0;
    
    points[1].x = 10;
    points[1].y = 20;
    
    // Loop
    for (int i = 0; i < 3; i++) {
        println("Point[{i}]: ({points[i].x}, {points[i].y})");
    }
    
    return 0;
}
```

---

## Copying Structs

### Pass by Value

```cm
void modify(Point p) {
    p.x = 100;  // Modifies local copy
}

int main() {
    Point p1(10, 20);
    modify(p1);
    println("{p1.x}");  // 10 (Unchanged)
    return 0;
}
```

### Pass by Pointer

```cm
void modify(Point* p) {
    (*p).x = 100;  // Modifies original object
}

int main() {
    Point p1(10, 20);
    modify(&p1);
    println("{p1.x}");  // 100 (Changed)
    return 0;
}
```

---

## Common Mistakes

### ❌ Incorrect Field Names

```cm
struct Point {
    int x;
    int y;
}

Point p;
// p.z = 10;  // Error: Field z does not exist
```

### ❌ Using Uninitialized Fields

```cm
Point p;
// println("{p.x}");  // Warning: Potentially uninitialized
```

### ❌ Direct Comparison

```cm
Point p1(10, 20);
Point p2(10, 20);
// if (p1 == p2) { }  // Error: == operator not defined
// Solution: Use `with Eq` or implement operator==
```

---

## Practice Problems

### Problem 1: Circle Struct
Create a struct representing a circle and implement a method to calculate its area.

<details>
<summary>Example Answer</summary>

```cm
struct Circle {
    double x;
    double y;
    double radius;
}

interface Shape {
    double area();
}

impl Circle for Shape {
    double area() {
        const double PI = 3.14159;
        return PI * self.radius * self.radius;
    }
}

int main() {
    Circle c;
    c.radius = 5.0;
    
    println("Area: {c.area():.2}");
    return 0;
}
```
</details>

---

## Next Steps

✅ Can define and initialize structs  
✅ Can implement constructors  
✅ Can use nested structs  
⏭️ Next, learn about [Enums](enums.html)

## Related Links

- [Interfaces](interfaces.html) - Method definition
- [Generics](generics.html) - Generic structs
- [Auto Implementation](../advanced/with-keyword.html) - Automatic trait implementation

---

**Previous:** [Functions](../basics/functions.html)  

---

**Last Updated:** 2026-02-08

---

<!-- nav -->
← Prev: [Type System](index.html) | [Contents](index.html) | Next: [Types - Enums](enums.html) →
