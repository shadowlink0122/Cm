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
```

---

**Previous:** [typedef](typedef.html)  
**Next:** [Interfaces](interfaces.html)
