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
**Next:** [Interfaces](interfaces.html)