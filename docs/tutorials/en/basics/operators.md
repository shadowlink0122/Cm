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

```

### Floating Point Operations

```cm
double x = 10.0, y = 3.0;

double sum = x + y;   // 13.0
double quot = x / y;  // 3.333...
// double rem = x % y;  // Error: % is only for integers
```

### Unary Operators

```cm
```

---

## Comparison Operators

```cm

bool eq = (a == b);   // false (Equal)
bool ne = (a != b);   // true  (Not equal)
bool lt = (a < b);    // true  (Less than)
bool le = (a <= b);   // true  (Less than or equal)
bool gt = (a > b);    // false (Greater than)
bool ge = (a >= b);   // false (Greater than or equal)
```

### String Comparison

```cm

bool same = (s1 == s2);     // true
bool diff = (s1 != s3);     // true
```

---

## Logical Operators

### AND (&&)

```cm
bool a = true, b = false;

bool result = a && b;  // false
// True only if both are true

    println("x is between 1 and 9");
}
```

### OR (||)

```cm
bool result = a || b;  // true
// True if either is true

    println("Out of range");
}
```

### NOT (!)

```cm
bool flag = true;
bool negated = !flag;  // false

    println("Not empty");
}
```

### Short-circuit Evaluation

```cm
// &&: If left is false, right is not evaluated
bool result = false && expensive_function();  // Not called

// ||: If left is true, right is not evaluated
bool result = true || expensive_function();   // Not called
```

---

## Compound Assignment Operators

```cm

x += 5;   // x = x + 5;  -> 15
x -= 3;   // x = x - 3;  -> 12
x *= 2;   // x = x * 2;  -> 24
x /= 4;   // x = x / 4;  -> 6
x %= 4;   // x = x % 4;  -> 2
```

---

## Increment/Decrement

### Postfix

```cm
```

### Prefix

```cm
```

### Usage in Loops

```cm
// Postfix (Common)
for (int i = 0; i < 10; i++) {
    println("{}", i);
}

// Prefix (Same result)
for (int i = 0; i < 10; ++i) {
    println("{}", i);
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

bool b = 5 > 3 && 10 < 20;  // true
bool b = 5 > 3 || false && true;  // true (&& first)
```

---

## Ternary Operator

`condition ? true_value : false_value`

```cm

// Nested (Not recommended for readability)
```

---

## Common Mistakes

### ❌ Confusing Assignment and Comparison

```cm
    // x becomes 5
}

// Correct
    // ...
}
```

### ❌ Integer Division Precision

```cm

// Correct
double result = 5.0 / 2.0;  // 2.5
```

### ❌ Logical vs Bitwise Operators

```cm
bool result = true & false;  // Error: & is bitwise
bool result = true && false; // OK: Logical AND
```

---

## Practice Problems

### Problem 1: Calculator
Write a function that performs basic arithmetic operations on two numbers.

<details>
<summary>Example Answer</summary>

```cm
    
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
- Type Conversions

---

**Previous:** [Variables and Types](variables.html)  
**Next:** [Control Flow](control-flow.html)