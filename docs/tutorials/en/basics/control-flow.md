---
title: Control Flow
parent: Tutorials
---

[日本語](../../ja/basics/control-flow.html)

# Basics - Control Flow

**Difficulty:** 🟢 Beginner  
**Time:** 30 minutes

## 📚 What you'll learn

- Conditional branching with `if`
- `while`/`for` loops
- `switch` statement
- `defer` statement
- `break`/`continue`

---

## if Statement

### Basic if

```cm
int score = 85;

if (score >= 90) {
    println("Grade: A");
} else if (score >= 80) {
    println("Grade: B");
} else if (score >= 70) {
    println("Grade: C");
} else if (score >= 60) {
    println("Grade: D");
} else {
    println("Grade: F");
}
```

### Conditions

```cm
int x = 10;

if (x > 0) {
    println("Positive");
}

if (x % 2 == 0) {
    println("Even");
}

if (x >= 5 && x <= 15) {
    println("Between 5 and 15");
}
```

### Nested if

```cm
int age = 25;
bool has_license = true;

if (age >= 18) {
    if (has_license) {
        println("Can drive");
    } else {
        println("Need license");
    }
} else {
    println("Too young");
}
```

---

## Ternary Operator

```cm
int a = 10, b = 20;
int max = (a > b) ? a : b;

string status = (age >= 20) ? "Adult" : "Minor";

// Nested (Use with caution for readability)
int sign = (x > 0) ? 1 : (x < 0) ? -1 : 0;
```

---

## while Loop

### Basic while

```cm
int i = 0;
while (i < 5) {
    println("{}", i);
    i++;
}
// Output: 0, 1, 2, 3, 4
```

### Infinite Loop

```cm
int count = 0;
while (true) {
    println("{}", count);
    count++;
    
    if (count >= 10) {
        break;  // Exit loop
    }
}
```

### Conditional Loop

```cm
int sum = 0;
int n = 1;

while (sum < 100) {
    sum += n;
    n++;
}

println("Sum: {}", sum);
```

---

## for Loop

### C-style for

```cm
// Basic
for (int i = 0; i < 5; i++) {
    println("{}", i);
}

// No initializer
int j = 0;
for (; j < 5; j++) {
    println("{}", j);
}

// Multiple variables
for (int i = 0, j = 10; i < 5; i++, j--) {
    println("i={}, j={}", i, j);
}
```

### Range-based for (for-in)

```cm
int[5] arr = [1, 2, 3, 4, 5];

// With type
for (int n in arr) {
    println("{}", n);
}

// Type inference (if supported)
for (n in arr) {
    println("{}", n);
}
```

### Looping over Struct Arrays

```cm
struct Point {
    int x;
    int y;
}

int main() {
    Point[3] points;
    points[0] = Point(10, 20);
    points[1] = Point(30, 40);
    points[2] = Point(50, 60);
    
    for (p in points) {
        println("({}, {})", p.x, p.y);
    }
    
    return 0;
}
```

---

## switch Statement

**Important:** Cm's `switch` uses `case()` syntax and breaks automatically.

### Basic switch

```cm
int day = 3;

switch (day) {
    case(1) {
        println("Monday");
    }
    case(2) {
        println("Tuesday");
    }
    case(3) {
        println("Wednesday");
    }
    case(4) {
        println("Thursday");
    }
    case(5) {
        println("Friday");
    }
    case(6 | 7) {
        println("Weekend");
    }
    else {
        println("Invalid day");
    }
}
```

**Syntax Features:**
- `case(value)` - Value enclosed in parentheses
- `{ }` - Block for each case
- `No break needed` - Automatically breaks (no fallthrough)
- `else` - Default case (instead of `default`)

### Enum and switch

```cm
enum Status {
    Ok = 0,
    Error = 1,
    Pending = 2
}

int main() {
    int status = Status::Ok;
    
    switch (status) {
        case(Status::Ok) {
            println("Success");
        }
        case(Status::Error) {
            println("Failed");
        }
        case(Status::Pending) {
            println("Waiting");
        }
    }
    
    return 0;
}
```

### OR Patterns

```cm
int value = 2;

switch (value) {
    case(1 | 2 | 3) {
        println("1, 2, or 3");
    }
    case(4 | 5) {
        println("4 or 5");
    }
    else {
        println("Other");
    }
}
```

### Range Patterns

```cm
int score = 85;

switch (score) {
    case(90...100) {
        println("Grade: A");
    }
    case(80...89) {
        println("Grade: B");
    }
    case(70...79) {
        println("Grade: C");
    }
    case(60...69) {
        println("Grade: D");
    }
    else {
        println("Grade: F");
    }
}
```

### Complex Patterns

```cm
int x = 10;

switch (x) {
    case(1...5 | 10 | 20...30) {
        // (1 <= x <= 5) || x == 10 || (20 <= x <= 30)
        println("Matched!");
    }
    else {
        println("Not matched");
    }
}
```

---

## break/continue

### break - Exit Loop

```cm
// while loop
int i = 0;
while (true) {
    if (i >= 5) {
        break;
    }
    println("{}", i);
    i++;
}

// for loop
for (int j = 0; j < 10; j++) {
    if (j == 5) {
        break;
    }
    println("{}", j);
}
```

### continue - Skip to Next Iteration

```cm
// Print only even numbers
for (int i = 0; i < 10; i++) {
    if (i % 2 != 0) {
        continue;  // Skip odd numbers
    }
    println("{}", i);
}

// while loop
int n = 0;
while (n < 10) {
    n++;
    if (n % 2 != 0) {
        continue;
    }
    println("{}", n);
}
```

---

## defer Statement

Registers a function call to be executed when the surrounding function returns.

### Basic Usage

```cm
void example() {
    defer println("3rd");
    defer println("2nd");
    defer println("1st");
    println("Start");
}
// Output: Start, 1st, 2nd, 3rd
```

### Resource Management

```cm
void process_file() {
    // Open file (hypothetical example)
    println("Opening file");
    defer println("Closing file");
    
    println("Processing...");
    
    // defer runs automatically at function exit
}
```

### Error Handling

```cm
int divide(int a, int b) {
    defer println("divide() finished");
    
    if (b == 0) {
        println("Error: division by zero");
        return -1;
    }
    
    return a / b;
}
```

---

## Nested Loops

### Double Loop

```cm
// Multiplication table
for (int i = 1; i <= 9; i++) {
    for (int j = 1; j <= 9; j++) {
        // Implementation needed for no-newline print
        println("{}", i * j);
    }
}
```

**Note:** Standard library currently lacks a `print()` function without newline.

### Breaking Nested Loops

```cm
bool found = false;
for (int i = 0; i < 10 && !found; i++) {
    for (int j = 0; j < 10; j++) {
        if (i * j == 42) {
            println("Found: {}*{} = 42", i, j);
            found = true;
            break;  // Breaks inner loop
        }
    }
}
```

---

## Common Mistakes

### ❌ Trying to use break in switch

**Cm does not require break!**

```cm
int x = 1;
switch (x) {
    case(1) {
        println("One");
        // break is unnecessary! No fallthrough.
    }
    case(2) {
        println("Two");
    }
}
// Output: "One" only
```

### ❌ Using C++ Syntax

```cm
// Wrong: C++ style
switch (x) {
    case 1:  // Error: must use case()
        println("One");
        break;
}

// Correct: Cm style
switch (x) {
    case(1) {
        println("One");
    }
}
```

### ❌ Using default

```cm
// Wrong
switch (x) {
    case(1) { println("One"); }
    default:  // Error: use else
        println("Other");
}

// Correct
switch (x) {
    case(1) { println("One"); }
    else {
        println("Other");
    }
}
```

### ❌ Infinite Loop Mistake

```cm
// Loop condition always true
int i = 0;
while (i < 10) {
    println("{}", i);
    // Forgot i++!
}
```

### ❌ Semicolon Placement

```cm
int i = 0;
while (i < 5);  // Semicolon here!
{
    println("{}", i);
    i++;
}
// Results in infinite loop
```

---

## Practice Problems

### Problem 1: FizzBuzz
Print numbers from 1 to 100. Print "Fizz" for multiples of 3, "Buzz" for multiples of 5, and "FizzBuzz" for multiples of both.

<details>
<summary>Example Answer</summary>

```cm
int main() {
    for (int i = 1; i <= 100; i++) {
        if (i % 15 == 0) {
            println("FizzBuzz");
        } else if (i % 3 == 0) {
            println("Fizz");
        } else if (i % 5 == 0) {
            println("Buzz");
        } else {
            println("{}", i);
        }
    }
    return 0;
}
```
</details>

### Problem 2: Factorial
Implement a function to calculate the factorial of `n`.

<details>
<summary>Example Answer</summary>

```cm
int factorial(int n) {
    int result = 1;
    for (int i = 1; i <= n; i++) {
        result *= i;
    }
    return result;
}

int main() {
    println("5! = {}", factorial(5));  // 120
    println("10! = {}", factorial(10)); // 3628800
    return 0;
}
```
</details>

### Problem 3: Prime Check
Determine if a number is prime.

<details>
<summary>Example Answer</summary>

```cm
bool is_prime(int n) {
    if (n < 2) {
        return false;
    }
    
    for (int i = 2; i * i <= n; i++) {
        if (n % i == 0) {
            return false;
        }
    }
    
    return true;
}

int main() {
    for (int i = 2; i <= 20; i++) {
        if (is_prime(i)) {
            println("{} is prime", i);
        }
    }
    return 0;
}
```
</details>

---

## Next Steps

✅ Can use if for branching  
✅ Can use while/for loops  
✅ Can use switch  
✅ Can use defer  
⏭️ Next, learn about [Functions](functions.html)

## Related Links

- [match Expression](../advanced/match.html) - Advanced pattern matching
- [Operators](operators.html) - Used in conditions

---

**Previous:** [Operators](operators.html)  
**Next:** [Functions](functions.html)

---

**Last Updated:** 2026-02-08
