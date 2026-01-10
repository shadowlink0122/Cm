---
title: Pointers
parent: Tutorials
---

[日本語](../../ja/basics/pointers.html)

# Basics - Pointers

**Difficulty:** 🟡 Intermediate  
**Time:** 30 minutes

## 📚 What you'll learn

- Pointer declaration
- Address operator `&`
- Dereference operator `*`
- Pointer arithmetic
- Pointers to structs
- Array Decay
- Function pointers

---

## Basic Usage

### Declaration

```cm
// Basic pointer
int* p;

// Multiple pointers
int* a, *b, *c;

// Pointer to pointer
int** pp;

struct Point { int x; int y; }

// Pointer to struct
Point* ptr;
```

## Address Operator

Use `&` to get the memory address of a variable.

```cm
struct Point { int x; int y; }

int main() {
    int x = 42;
    int* p = &x;  // Address of x

    Point pt = Point(10, 20);
    Point* ptr = &pt;  // Address of pt
    return 0;
}
```

## Dereferencing

Use `*` to access the value at the address.

```cm
int main() {
    int x = 10;
    int* p = &x;

    // Read
    int value = *p;  // 10

    // Write
    *p = 20;
    // x becomes 20
    return 0;
}
```

## Pointer Arithmetic

```cm
int main() {
    int[5] arr = {1, 2, 3, 4, 5};
    int* p = arr;  // Points to arr[0]

    // Addition
    int first = *p;         // 1
    int second = *(p + 1);  // 2
    int third = *(p + 2);   // 3

    // Increment
    p++;
    int value = *p;  // 2

    // Iteration
    int* end = arr + 5;
    for (int* iter = arr; iter != end; iter++) {
        println("{}", *iter);
    }
    return 0;
}
```

## Struct Pointers

### Accessing Fields

```cm
struct Point {
    int x;
    int y;
}

int main() {
    Point pt = Point(10, 20);
    Point* ptr = &pt;

    // (*ptr).x
    (*ptr).x = 30;
    (*ptr).y = 40;

    // Automatic dereference via '.' (Supported)
    ptr.x = 30;
    ptr.y = 40;
    return 0;
}
```

### Method Calls via Pointer

```cm
interface Printable {
    void print();
}

struct Point { int x; int y; }

impl Point for Printable {
    void print() {
        println("({}, {})", self.x, self.y);
    }
}

int main() {
    Point pt = Point(5, 10);
    Point* ptr = &pt;
    
    ptr.print();  // Automatic dereference
    return 0;
}
```

## Arrays and Pointers

### Array Decay

Arrays implicitly convert to pointers.

```cm
int main() {
    int[5] arr = {1, 2, 3, 4, 5};

    // Implicit conversion
    int* p = arr;
    return 0;
}

// Pass to function
void process(int* data, int size) {
    for (int i = 0; i < size; i++) {
        data[i] *= 2;
    }
}

int main() {
    int[5] arr = {1, 2, 3, 4, 5};
    process(arr, 5);
    return 0;
}
```

### Equivalence

```cm
int main() {
    int[5] arr = {10, 20, 30, 40, 50};
    int* p = arr;

    // All equivalent
    int v1 = arr[2];   // 30
    int v2 = p[2];     // 30
    int v3 = *(p + 2); // 30
    int v4 = *(arr + 2);  // 30
    return 0;
}
```

## Function Pointers

### Declaration

```cm
// Type: ReturnType*(Args...)
int*(int, int) op;

int add(int a, int b) {
    return a + b;
}

int multiply(int a, int b) {
    return a * b;
}

int main() {
    op = add;
    int result1 = op(10, 20);  // 30
    
    op = multiply;
    int result2 = op(10, 20);  // 200
    
    return 0;
}
```

### Higher-Order Functions

```cm
// Function taking a function pointer
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
    int res = apply(max, 10, 5);  // 10
    return 0;
}
```

## Null Pointer

```cm
int main() {
    int* p = null;

    if (p == null) {
        println("Pointer is null");
    }

    // Dereferencing null is undefined behavior!
    // *p = 10;  // Crash
    return 0;
}
```

---

## Next Steps

✅ Can use pointers to manipulate memory  
✅ Understand relationship between arrays and pointers  
✅ Can use function pointers  
⏭️ Next, learn about [Modules](modules.html)

## Related Links

- [Arrays](arrays.html)
- [Memory Management](../../../guides/memory.html)

---

**Previous:** [Arrays](arrays.html)  
**Next:** [Modules](modules.html)
