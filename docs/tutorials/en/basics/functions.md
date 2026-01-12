---
title: Functions
parent: Tutorials
---

[日本語](../../ja/basics/functions.html)

# Basics - Functions

**Difficulty:** 🟢 Beginner  
**Time:** 20 minutes

## 📚 What you'll learn

- Function definition and calling
- Return values and parameters
- Function overloading
- Default arguments
- Basics of method calls

---

## Basic Function Definition

Functions are defined in the format `ReturnType FunctionName(Parameters) { ... }`.

```cm
int add(const int a, const int b) {  // Parameters won't change, use const
    return a + b;
}

void greet(const string name) {  // name won't be modified
    println("Hello, {}!", name);
}

int main() {
    const int sum = add(10, 20);  // Result is immutable
    greet("Alice");
    return 0;
}
```

### Void Functions

If a function does not return a value, specify `void` as the return type.

```cm
void print_hello() {
    println("Hello");
}
```

---

## Parameters (Arguments)

### Pass by Value

In Cm, arguments are passed by value by default.

```cm
void increment(int n) {
    n++;  // Modifies local copy
}

int main() {
    int x = 10;
    increment(x);
    println("{}", x);  // 10 (Unchanged)
    return 0;
}
```

### Pass by Pointer

Use pointers to modify the original value.

```cm
void increment(int* n) {
    (*n)++;
}

int main() {
    int x = 10;
    increment(&x);
    println("{}", x);  // 11 (Changed)
    return 0;
}
```

---

## Function Overloading

You can define multiple functions with the same name but different parameters (type or number).
The `overload` keyword is required.

```cm
overload int max(const int a, const int b) {
    return a > b ? a : b;
}

overload double max(double a, double b) {
    return a > b ? a : b;
}

int main() {
    println("max int: {}", max(10, 20));
    println("max double: {}", max(3.14, 2.71));
    return 0;
}
```

---

## Default Arguments

You can set default values for parameters. If omitted during the call, the default value is used.

```cm
void log(string message, int level = 1) {
    println("[Level {}] {}", level, message);
}

int main() {
    log("System started");      // [Level 1] System started
    log("Disk full", 3);        // [Level 3] Disk full
    return 0;
}
```

---

## Method Calls

Functions associated with structs (methods) are called using the `.` operator. Internally, they receive a `self` pointer as the first argument.

```cm
struct Counter {
    int value;
}

impl Counter {
    void increment() {
        self.value++;
    }
}

int main() {
    Counter c;
    c.value = 0;
    c.increment();  // Called as a method
    return 0;
}
```

Details are explained in [Structs](../types/structs.html) and [Interfaces](../types/interfaces.html).

---

## Common Mistakes

### ❌ Forgetting overload Keyword

```cm
int foo(int x) { ... }
// int foo(double x) { ... } // Error: overload keyword required
overload int foo(double x) { ... } // OK
```

### ❌ Overloading only on Return Type

Overloading must be distinguished by parameters.

```cm
int bar(int x) { ... }
// overload double bar(int x) { ... } // Error: Same parameters
```

---

## Practice Problems

### Problem 1: Factorial Function
Create a recursive function `factorial(int n)` that calculates the factorial of a given integer.

<details>
<summary>Example Answer</summary>

```cm
int factorial(int n) {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

int main() {
    println("5! = {}", factorial(5));
    return 0;
}
```
</details>

---

## Next Steps

✅ Can define and call functions  
✅ Understand how to use overloading  
✅ Can use default arguments  
⏭️ Next, learn about [Arrays](arrays.html)

## Related Links

- [Structs](../types/structs.html) - Method definition
- [Interfaces](../types/interfaces.html) - Polymorphism
- [Function Pointers](../advanced/function-pointers.html) - Higher-order functions

---

**Previous:** [Control Flow](control-flow.html)  
**Next:** [Arrays](arrays.html)
