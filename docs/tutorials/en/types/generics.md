---
title: Generics
parent: Tutorials
---

[日本語](../../ja/types/generics.html)

# Types - Generics

**Difficulty:** 🔴 Advanced  
**Time:** 35 minutes

## Generic Functions

```cm
<T> T identity(T value) {
    return value;
}

int main() {
    int i = identity(42);
    double d = identity(3.14);
    string s = identity("Hello");
    return 0;
}
```

## Type Inference

```cm
<T> T max(T a, T b) {
    return a > b ? a : b;
}

int main() {
    int i = max(10, 20);
    double d = max(3.14, 2.71);
    return 0;
}
```

## Generic Structs

```cm
struct Box<T> {
    T value;
}

int main() {
    Box<int> int_box;
    int_box.value = 42;
    
    Box<string> str_box;
    str_box.value = "Hello";
    
    return 0;
}
```

## Multiple Type Parameters

```cm
struct Pair<T, U> {
    T first;
    U second;
}

<T, U> Pair<T, U> make_pair(T first, U second) {
    Pair<T, U> p;
    p.first = first;
    p.second = second;
    return p;
}

int main() {
    Pair<int, string> p = make_pair(1, "one");
    return 0;
}
```

## Type Inference from Pointer and Array Arguments

When a type parameter appears inside a pointer (`T*`) or array (`T[]`) argument, `T` is inferred from the corresponding part of the actual argument type.

```cm
<T> void swap(T* a, T* b) {
    T tmp = *a;
    *a = *b;
    *b = tmp;
}

<T> T first(T[] xs) {
    return xs[0];
}

int main() {
    int a = 1;
    int b = 2;
    swap(&a, &b);           // T=int (inferred from the int* of &a/&b)
    println("{a} {b}");     // 2 1

    int[] xs = [10, 20, 30];
    println("{first(xs)}"); // 10
    return 0;
}
```

## Generic Collections and RAII

Generic collections (like `Vector<T>`) have `self()` constructors and `~self()` destructors.

```cm
import std::collections::vector::*;

struct TrackedObject {
    int id;
}

impl TrackedObject {
    ~self() {
        println("~TrackedObject({self.id})");
    }
}

int main() {
    {
        Vector<TrackedObject> objects();  // Constructor call
        objects.push(TrackedObject { id: 100 });
        objects.push(TrackedObject { id: 200 });
        // On scope exit:
        // 1. ~Vector() is called
        // 2. ~TrackedObject() is called for each element
    }
    return 0;
}
```

**Output:**
```
~TrackedObject(100)
~TrackedObject(200)
```

---

**Previous:** [typedef](typedef.html)  
---

**Last Updated:** 2026-02-08

---

<!-- nav -->
← Prev: [Types - typedef](typedef.html) | [Contents](index.html) | Next: [Types - Interfaces](interfaces.html) →

## Constructing Generic Structs with Literals

Type arguments are inferred from the declared type, so struct literals can be written without them (v0.17.0).

```cm
struct Box<T> { T v; }
struct Pair<A, B> { A first; B second; }

Box<int> b = Box{v: 7};                            // bare-name literal (inferred)
Pair<int, string> p = {first: 7, second: "seven"}; // anonymous literal (inferred)
```

Explicit type arguments in literals (`Box<int>{v: 7}`) are not supported due to parsing ambiguity with comparison operators. Field-by-field assignment (`Box<int> b; b.v = 7;`) also keeps working.

## Structs with Nested Specializations as Type Arguments

A generic specialization can be passed as a type argument of another generic. Inner literal types are also inferred from the declared type.

```cm
struct Box<T> { T v; }
struct Pair<A, B> { A first; B second; }

Pair<Box<int>, Box<string>> nested = Pair { first: Box { v: 42 }, second: Box { v: "deep" } };
println("{nested.first.v} {nested.second.v}");  // 42 deep

Box<Pair<int, string>> outer = Box { v: Pair { first: 5, second: "inner" } };
println("{outer.v.first} {outer.v.second}");    // 5 inner
```
