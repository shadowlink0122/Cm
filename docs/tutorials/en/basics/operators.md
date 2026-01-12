---
title: Operators
parent: Tutorials
---

[日本語](../../ja/basics/operators.html)

# Basics - Operators

**Difficulty:** 🟢 Beginner  
**Time:** 15 minutes

## 📚 What you'll learn

- Arithmetic operators
- Comparison operators
- Logical operators
- Compound assignment operators
- Increment/Decrement

---

## Arithmetic Operators

### Basic Operations

```cm
int main() {
    int a = 10, b = 3;

    int sum = a + b;      // 13 (Addition)
    int diff = a - b;     // 7  (Subtraction)
    int prod = a * b;     // 30 (Multiplication)
    int quot = a / b;     // 3  (Division)
    int rem = a % b;      // 1  (Remainder)
    return 0;
}
```

### Floating Point Operations

```cm
int main() {
    double x = 10.0, y = 3.0;

    double sum = x + y;   // 13.0
    double quot = x / y;  // 3.333...
    // double rem = x % y;  // Error: % is only for integers
    return 0;
}
```

### Unary Operators

```cm
int main() {
    int x = 10;
    int neg = -x;         // -10 (Negation)
    int pos = +x;         // 10  (Identity)
    return 0;
}
```

---

## Comparison Operators

```cm
int main() {
    int a = 10, b = 20;

    bool eq = (a == b);   // false (Equal)
    bool ne = (a != b);   // true  (Not equal)
    bool lt = (a < b);    // true  (Less than)
    bool le = (a <= b);   // true  (Less than or equal)
    bool gt = (a > b);    // false (Greater than)
    bool ge = (a >= b);   // false (Greater than or equal)
    return 0;
}
```

### String Comparison

```cm
int main() {
    string s1 = "abc";
    string s2 = "abc";
    string s3 = "def";

    bool same = (s1 == s2);     // true
    bool diff = (s1 != s3);     // true
    return 0;
}
```

---

## Logical Operators

### AND (&&)

```cm
int main() {
    bool a = true, b = false;
    bool result = a && b;  // false
    // True only if both are true

    int x = 5;
    if (x > 0 && x < 10) {
        println("x is between 1 and 9");
    }
    return 0;
}
```

### OR (||)

```cm
int main() {
    bool a = true, b = false;
    bool result = a || b;  // true
    // True if either is true

    int x = 150;
    if (x < 0 || x > 100) {
        println("Out of range");
    }
    return 0;
}
```

### NOT (!)

```cm
int main() {
    bool flag = true;
    bool negated = !flag;  // false

    bool is_empty = false;
    if (!is_empty) {
        println("Not empty");
    }
    return 0;
}
```

---

## Compound Assignment Operators

```cm
int main() {
    int x = 10;

    x += 5;   // x = x + 5;  -> 15
    x -= 3;   // x = x - 3;  -> 12
    x *= 2;   // x = x * 2;  -> 24
    x /= 4;   // x = x / 4;  -> 6
    x %= 4;   // x = x % 4;  -> 2
    return 0;
}
```

---

## Increment/Decrement

### Postfix

```cm
int main() {
    int i = 5;
    int a = i++;  // a = 5, i = 6 (Increment after use)
    int b = i--;  // b = 6, i = 5 (Decrement after use)
    return 0;
}
```

### Prefix

```cm
int main() {
    int i = 5;
    int a = ++i;  // a = 6, i = 6 (Increment before use)
    int b = --i;  // b = 5, i = 5 (Decrement before use)
    return 0;
}
```

### Usage in Loops

```cm
int main() {
    // Postfix (Common)
    for (int i = 0; i < 10; i++) {
        println("{}", i);
    }

    // Prefix (Same result)
    for (int i = 0; i < 10; ++i) {
        println("{}", i);
    }
    return 0;
}
```

---

## Operator Precedence

Precedence (High -> Low):

1. `()` - Parentheses
2. `++`, `--`, `!`, `+`(unary), `-`(unary) - Unary
3. `*`, `/`, `%` - Multiplication/Division
4. `+`, `-` - Addition/Subtraction
5. `<`, `<=`, `>`, `>=` - Comparison
6. `==`, `!=` - Equality
7. `&&` - Logical AND
8. `||` - Logical OR
9. `=`, `+=`, `-=`, etc. - Assignment

### Example

```cm
int main() {
    int result = 2 + 3 * 4;     // 14 (3*4 first)
    int result2 = (2 + 3) * 4;   // 20 (Parentheses first)

    bool b = 5 > 3 && 10 < 20;  // true
    bool b2 = 5 > 3 || false && true;  // true (&& first)
    return 0;
}
```

---

## Ternary Operator

`condition ? true_value : false_value`

```cm
int main() {
    int a = 10, b = 20;
    int max = (a > b) ? a : b;

    int age = 20;
    string status = (age >= 20) ? "Adult" : "Minor";

    // Nested (Not recommended for readability)
    int x = 5;
    int sign = (x > 0) ? 1 : (x < 0) ? -1 : 0;
    return 0;
}
```

---

## Common Mistakes

### ❌ Confusing Assignment and Comparison

```cm
int main() {
    int x = 10;
    // if (x = 5) {  // Warning: Assignment in condition
        // x becomes 5
    // }

    // Correct
    if (x == 5) {  // Comparison
        // ...
    }
    return 0;
}
```

### ❌ Integer Division Precision

```cm
int main() {
    int result = 5 / 2;  // 2 (Integer division)

    // Correct
    double d_result = 5.0 / 2.0;  // 2.5
    return 0;
}
```

---

## Practice Problems

### Problem 1: Calculator
Write a function that performs basic arithmetic operations on two numbers.

<details>
<summary>Example Answer</summary>

```cm
int main() {
    int a = 15, b = 4;
    
    println("{} + {} = {}", a, b, a + b);
    println("{} - {} = {}", a, b, a - b);
    println("{} * {} = {}", a, b, a * b);
    println("{} / {} = {}", a, b, a / b);
    println("{} % {} = {}", a, b, a % b);
    
    return 0;
}
```
</details>

### Problem 2: Leap Year Check
Check if a year is a leap year.

<details>
<summary>Example Answer</summary>

```cm
bool is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int main() {
    int year = 2025;
    if (is_leap_year(year)) {
        println("{} is a leap year", year);
    } else {
        println("{} is not a leap year", year);
    }
    return 0;
}
```
</details>

---

## Next Steps

✅ Understood operator types  
✅ Learned operator precedence  
⏭️ Next, learn about [Control Flow](control-flow.html)

## Related Links

- [Operator Overloading](../advanced/operators.html)
- [Type Conversions](../types/conversions.html)

---

**Previous:** [Variables and Types](variables.html)  
**Next:** [Control Flow](control-flow.html)
