---
title: Function Pointers
parent: Tutorials
---

[日本語](../../ja/advanced/function-pointers.html)

# Advanced - Function Pointers

**Difficulty:** 🔴 Advanced  
**Time:** 25 minutes

## Declaration

```cm
int*(int, int) op;

int add(int a, int b) {
    return a + b;
}

int multiply(int a, int b) {
    return a * b;
}

int main() {
    op = add;
    int result1 = op(10, 20);
    
    op = multiply;
    int result2 = op(10, 20);
    
    return 0;
}
```

## Higher-Order Functions

```cm
int apply(int*(int, int) fn, int x, int y) {
    return fn(x, y);
}

int max(int a, int b) {
    return a > b ? a : b;
}

int min(int a, int b) {
    return a < b ? a : b;
}

int main() {
    int max_val = apply(max, 10, 5);
    int min_val = apply(min, 10, 5);
    return 0;
}
```

## Void Return Type

```cm
void*(string) printer;

void print_upper(string s) {
    println(s.toUpperCase());
}

void print_lower(string s) {
    println(s.toLowerCase());
}

int main() {
    printer = print_upper;
    printer("Hello");
    
    printer = print_lower;
    printer("WORLD");
    
    return 0;
}
```

---

**Previous:** [Operator Overloading](operators.html)  
**Next:** [String Operations](strings.html)

---

**Last Updated:** 2026-02-08
