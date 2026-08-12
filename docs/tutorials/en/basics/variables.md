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

// Digit separator underscores (v0.17.0): usable in every base and fraction part
int million = 1_000_000;
int mask = 0b1010_1010;
long color = 0xFF_FF_FF as long;
double pi_ish = 3.141_592;
```

### Floating Point Types

```cm
float f = 3.14;       // 32bit single precision
double d = 3.14159;   // 64bit double precision

// Added in v0.10.0: Non-negative floating point
ufloat uf = 2.5;      // 32bit non-negative
udouble ud = 10.0;    // 64bit non-negative
```

Assigning a negative literal to `ufloat`/`udouble` produces a warning (an error with `--strict`). Operations that become negative at runtime are not checked, so validate values where a non-negative guarantee is required.

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
int x = 1;
int y = 2;
int z = 3;
```

**Type inference (auto / var):** Use `auto` (or its alias `var`) to infer the type from the initializer. Explicit type annotations remain fully supported.

```cm
auto x = 42;        // inferred as int
var name = "Cm";    // inferred as string (var is an alias of auto)
auto pi = 3.14;     // inferred as double
```

---

## const Modifier (Required in v0.11.0+)

**Important Change (v0.11.0):** The `const` keyword is now **mandatory** for all immutable variables. Variables without `const` are mutable by default.

```cm
// v0.11.0+: Must explicitly use const for immutables
const int MAX_SIZE = 100;
const double PI = 3.14159;
const string GREETING = "Hello";

// MAX_SIZE = 200;  // Error: const cannot be modified
```

### const vs Mutable Variables

```cm
// Mutable variable (default)
int counter = 10;
counter = 20;      // OK: can reassign

// Immutable variable (must use const)
const int constant = 10;
// constant = 20; // Error: Cannot modify const

// The compiler will warn if a variable could be const
int value = 42;
// If 'value' is never modified, compiler suggests: "Consider making 'value' const"
```

---

## static Variables

Static variables maintain state within a function.

```cm
void counter() {
    static int count = 0;
    count++;
    println("Count: {count}");
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

Value-preserving widening conversions (`int→long`, `short→int`, `int→double`, `float→double`, ...) happen implicitly.

```cm
int i = 10;
double d = i;    // int -> double (OK: widening)
long l = i;      // int -> long (OK: widening)
```

Narrowing conversions that may lose information (`double→int`, `long→int`, `int→short`, ...) and sign-changing conversions (`int→uint`, ...) produce a warning (an error under `check/lint --strict`).
The diagnostic applies uniformly to variable declarations, assignments, and returns, as well as function arguments, array literal elements, and struct field initializers.
Literals that fit the declared type (`short s = 5;`, `float f = 2.5;`) and `uint`/`usize`→`int` (for the `len()`/`sizeof` result idiom) are exempt.

```cm
double pi = 3.14;
// int x = pi;   // double -> int: narrowing warning (suggests adding 'as')
```

### Explicit Conversion (Cast)

Use `as` to state the narrowing intent explicitly.

```cm
double pi = 3.14159;
int truncated = pi as int;  // 3
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
println("{x}");  // Warning: Potentially uninitialized
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
    println("Count: {count}");
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

---

**Last Updated:** 2026-02-08

---

<!-- nav -->
← Prev: [Basics - Hello, World!](hello-world.html) | [Contents](index.html) | Next: [Basics - Operators](operators.html) →
