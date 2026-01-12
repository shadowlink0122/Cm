---
title: Arrays
parent: Tutorials
---

[日本語](../../ja/basics/arrays.html)

# Basics - Arrays

**Difficulty:** 🟡 Intermediate  
**Time:** 25 minutes

## 📚 What you'll learn

- Array declaration and initialization
- Element access
- Array methods
- Arrays of structs
- Pointer conversion (Array Decay)
- `for-in` loop

---

## Basic Usage

### Array Declaration

```cm
// Basic array declaration
int[5] numbers;

// Declaration and initialization
int[3] values = [1, 2, 3];

// Size inference
int[] auto_size = {10, 20, 30};  // Inferred size: 3
```

## Initialization

```cm
// Zero initialization
int[10] zeros;  // All 0

// Literal initialization
int[5] primes = {2, 3, 5, 7, 11};

// Partial initialization
int[5] partial = [1, 2];  // {1, 2, 0, 0, 0}
```

## Element Access

```cm
int main() {
    int[5] numbers;
    int first = numbers[0];
    numbers[1] = 10;
    return 0;
}
```

> **Note (v0.11.0):** Bounds checking has been introduced. Accessing out-of-bounds indices will cause the program to panic safely.

---

## Array Methods

### Getting Size

```cm
int[5] arr;

int size1 = arr.size();     // 5
int size2 = arr.len();      // 5
int size3 = arr.length();   // 5 (All equivalent)
```

### Searching Elements

```cm
int[5] numbers = [1, 2, 3, 4, 5];

// indexOf - Find element position
int pos = numbers.indexOf(3);  // 2
int not_found = numbers.indexOf(10);  // -1

// includes - Check existence
bool has_3 = numbers.includes(3);  // true
bool has_10 = numbers.includes(10);  // false

// contains (Alias for includes)
bool has_5 = numbers.contains(5);  // true
```

### Higher-Order Methods

```cm
int[5] numbers = [1, 2, 3, 4, 5];

// some - Check if any element matches condition
bool has_even = numbers.some(|x| x % 2 == 0);  // true

// every - Check if all elements match condition
bool all_positive = numbers.every(|x| x > 0);  // true

// findIndex - Find index of first matching element
int idx = numbers.findIndex(|x| x > 3);  // 3 (index of value 4)
```

---

## Arrays of Structs

```cm
struct Point {
    int x;
    int y;
}

int main() {
    // Declaration
    Point[3] points;

    // Initialization
    points[0].x = 10;
    points[0].y = 20;

    // Initialization List
    Point[2] pts = {
        Point(1, 2),
        Point(3, 4)
    };
    return 0;
}
```

---

## Array Decay (Pointer Conversion)

Arrays automatically convert to pointers when passed to functions or assigned to pointers.

```cm
int main() {
    int[5] arr = [1, 2, 3, 4, 5];

    // Array to Pointer conversion
    int* p = arr;  // Address of arr[0]

    // Access via pointer
    int first = *p;         // 1
    int second = *(p + 1);  // 2
    return 0;
}
```

### Passing Arrays to Functions

```cm
void print_array(int* data, int size) {
    for (int i = 0; i < size; i++) {
        println("{}", data[i]);
    }
}

int main() {
    int[5] numbers = [1, 2, 3, 4, 5];
    print_array(numbers, 5);  // Decays to pointer
    return 0;
}
```

---

## for-in Loop

Iterate over arrays easily with range-based for loops.

```cm
struct Point { int x; int y; }

int main() {
    int[5] numbers = [1, 2, 3, 4, 5];

    // With type
    for (int n in numbers) {
        println("{}", n);
    }

    // Type inference
    for (n in numbers) {
        println("{}", n);
    }

    // Struct array
    Point[3] points = {Point(1, 2), Point(3, 4), Point(5, 6)};

    for (Point p in points) {
        println("({}, {})", p.x, p.y);
    }
    return 0;
}
```

---

## Multidimensional Arrays

```cm
// 2D Array
int[3][4] matrix;

// Initialization
matrix[0][0] = 1;
matrix[0][1] = 2;

// Access via loop
for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 4; j++) {
        matrix[i][j] = i * 4 + j;
    }
}
```

### Performance Optimization (v0.11.0+)

**Automatic Array Flattening:** Cm v0.11.0 automatically optimizes multidimensional arrays for better cache performance.

```cm
// Your code (unchanged)
int[500][500] matrix;
matrix[i][j] = value;

// Internal optimization (automatic):
// - Converts to flat 1D array for better cache locality
// - Up to 250x faster for large matrices
// - Completely transparent to the user
```

This optimization provides:
- **200-250x speedup** for matrix operations
- **90% reduction** in cache misses
- No code changes required

---

## Next Steps

✅ Can declare and initialize arrays  
✅ Know how to use array methods  
✅ Understand array-pointer relationship  
⏭️ Next, learn about [Pointers](pointers.html)

## Related Links

- [Pointers](pointers.html)
- [Control Flow](control-flow.html)

---

**Previous:** [Functions](functions.html)  
**Next:** [Pointers](pointers.html)
