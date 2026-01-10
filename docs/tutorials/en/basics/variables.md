---
title: Variables and Types
parent: Tutorials
---

[日本語](../../ja/basics/variables.html)

# Basics - Variables and Types

**Difficulty:** 🟢 Beginner  
**Time:** 20 minutes

## 📚 What you'll learn

- Type system in Cm
- Primitive types
- Variable declaration and initialization
- const/static modifiers

---

## Primitive Types

### Integer Types

```cm
// Signed integers
tiny t = 127;         // 8bit: -128 to 127
short s = 32767;      // 16bit: -32768 to 32767
int i = 42;           // 32bit: -2^31 to 2^31-1
long l = 1000000;     // 64bit: -2^63 to 2^63-1

// Unsigned integers
utiny ut = 255;       // 8bit: 0 to 255
ushort us = 65535;    // 16bit: 0 to 65535
uint ui = 100;        // 32bit: 0 to 2^32-1
ulong ul = 2000000;   // 64bit: 0 to 2^64-1
```

### Floating Point Types

```cm
float f = 3.14;       // 32bit single precision
double d = 3.14159;   // 64bit double precision

// Added in v0.10.0: Non-negative floating point
ufloat uf = 2.5;      // 32bit non-negative
udouble ud = 10.0;    // 64bit non-negative
```

### Other Types

```cm
bool b = true;        // Boolean: true/false
char c = 'A';         // Character
string str = "Hello"; // String
```

---

## Variable Declaration

### Basic Declaration

```cm
int x;           // Declaration only
int y = 10;      // Declaration and initialization
```

### Multiple Declarations

```cm
int a, b, c;
int x = 1, y = 2, z = 3;
```

**Note:** Cm does not support type inference (e.g., `var` keyword). Types must always be explicitly specified.

---

## const Modifier

Defines immutable constants.

```cm
const int MAX_SIZE = 100;
const double PI = 3.14159;
const string GREETING = "Hello";

// MAX_SIZE = 200;  // Error: const cannot be modified
```

### const vs Normal Variables

```cm
int normal = 10;
normal = 20;      // OK

const int constant = 10;
// constant = 20; // Error: Cannot modify
```

---

## static Variables

Static variables maintain state within a function.

```cm
void counter() {
    static int count = 0;
    count++;
    println("Count: {}", count);
}

int main() {
    counter();  // "Count: 1"
    counter();  // "Count: 2"
    counter();  // "Count: 3"
    return 0;
}
```

### Features of static Variables

1. **Initialized once** - Initialized only the first time execution reaches them
2. **Value persistence** - Value persists across function calls
3. **Function scope** - Not accessible outside the function

---

## Type Conversion

### Implicit Conversion

```cm
int i = 10;
double d = i;    // int -> double (OK)

double pi = 3.14;
// int x = pi;   // double -> int (Precision loss)
```

### Explicit Conversion (Cast)

```cm
double pi = 3.14159;
int truncated = (int)pi;  // 3
```

---

## Type Sizes

| Type | Size | Range |
|----|--------|------|
| `tiny` | 8bit | -128 to 127 |
| `utiny` | 8bit | 0 to 255 |
| `short` | 16bit | -32768 to 32767 |
| `ushort` | 16bit | 0 to 65535 |
| `int` | 32bit | -2^31 to 2^31-1 |
| `uint` | 32bit | 0 to 2^32-1 |
| `long` | 64bit | -2^63 to 2^63-1 |
| `ulong` | 64bit | 0 to 2^64-1 |
| `float` | 32bit | IEEE 754 Single |
| `double` | 64bit | IEEE 754 Double |
| `ufloat` | 32bit | Non-negative Single |
| `udouble` | 64bit | Non-negative Double |
| `bool` | 8bit | true/false |
| `char` | 8bit | ASCII Character |

---

## Common Mistakes

### ❌ Using Uninitialized Variables

```cm
int x;
println("{}", x);  // Warning: Potentially uninitialized
```

### ❌ Type Mismatch

```cm
int x = "Hello";  // Error: Type mismatch
```

### ❌ Modifying const

```cm
const int MAX = 100;
MAX = 200;  // Error: Cannot modify const variable
```

---

## Practice Problems

### Problem 1: Choosing Types
Choose the best type for the following values:

1. Age (0-150)
2. World Population (approx. 8 billion)
3. Pi
4. Login Flag (Success/Failure)

<details>
<summary>Example Answer</summary>

```cm
utiny age = 25;           // 0-255 covers it
ulong population = 8000000000;  // Large number
double pi = 3.14159;      // High precision
bool login_success = true;  // Boolean
```
</details>

### Problem 2: Counter
Implement a `counter()` function that increments a count each time it is called.

<details>
<summary>Example Answer</summary>

```cm
void counter() {
    static int count = 0;
    count++;
    println("Count: {}", count);
}

int main() {
    counter();  // 1
    counter();  // 2
    counter();  // 3
    return 0;
}
```
</details>

---

## Next Steps

✅ Understood primitive types  
✅ Can declare and initialize variables  
✅ Know how to use const/static  
⏭️ Next, learn about [Operators](operators.html)

## Related Links

- [Structs](../types/structs.html)
- [typedef](../types/typedef.html)
- [Generics](../types/generics.html)

---

**Previous:** [Hello, World!](hello-world.html)  
**Next:** [Operators](operators.html)
