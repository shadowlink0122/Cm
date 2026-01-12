---
title: match Expression
parent: Tutorials
---

[日本語](../../ja/advanced/match.html)

# Advanced - match Expression

**Difficulty:** 🔴 Advanced  
**Time:** 35 minutes

## 📚 What you'll learn

- Basic syntax of `match`
- Pattern matching
- Pattern guards
- Exhaustiveness checking
- Matching with Enums

---

## Basic match

### Literal Patterns

```cm
int value = 2;

match (value) {
    0 => println("zero"),
    1 => println("one"),
    2 => println("two"),
    3 => println("three"),
    _ => println("other"),
}
```

**Syntax:**
- `match (expression) { Pattern => Body, ... }`
- `_` is a wildcard (matches anything)
- Patterns are separated by `,`

### Block Body

```cm
match (value) {
    0 => {
        println("This is zero");
        println("Nothing here");
    },
    1 => {
        println("This is one");
        println("First number");
    },
    _ => {
        println("Other number");
    },
}
```

---

## Enum Patterns

### Basic Usage

```cm
enum Status {
    Ok = 0,
    Error = 1,
    Pending = 2
}

void handle_status(int s) {
    match (s) {
        Status::Ok => println("Success!"),
        Status::Error => println("Failed!"),
        Status::Pending => println("Waiting..."),
    }
}

int main() {
    int status = Status::Ok;
    handle_status(status);
    return 0;
}
```

### Enum with Wildcard

```cm
enum Color {
    Red = 0,
    Green = 1,
    Blue = 2,
    Yellow = 3,
    Cyan = 4
}

void classify_color(int c) {
    match (c) {
        Color::Red => println("Primary: Red"),
        Color::Green => println("Primary: Green"),
        Color::Blue => println("Primary: Blue"),
        _ => println("Secondary color"),
    }
}
```

---

## Pattern Guards

### Basic Guards

```cm
int classify(int n) {
    match (n) {
        n if n < 0 => println("Negative"),
        n if n == 0 => println("Zero"),
        n if n > 0 => println("Positive"),
    }
    return 0;
}
```

**Syntax:** `Pattern if Condition => Body`

### Complex Conditions

```cm
int analyze(int x) {
    match (x) {
        x if x % 2 == 0 && x > 0 => println("Positive even"),
        x if x % 2 == 0 && x < 0 => println("Negative even"),
        x if x % 2 != 0 && x > 0 => println("Positive odd"),
        x if x % 2 != 0 && x < 0 => println("Negative odd"),
        _ => println("Zero"),
    }
    return 0;
}
```

### Range Checks

```cm
void check_score(int score) {
    match (score) {
        s if s >= 90 => println("Grade: A"),
        s if s >= 80 => println("Grade: B"),
        s if s >= 70 => println("Grade: C"),
        s if s >= 60 => println("Grade: D"),
        _ => println("Grade: F"),
    }
}
```

---

## Variable Binding Patterns

### Reusing Variables

```cm
int describe(int n) {
    match (n) {
        x if x % 2 == 0 => println("{} is even", x),
        x if x % 2 == 1 => println("{} is odd", x),
    }
    return 0;
}
```

### Calculations

```cm
void fizzbuzz(int n) {
    match (n) {
        x if x % 15 == 0 => println("FizzBuzz"),
        x if x % 3 == 0 => println("Fizz"),
        x if x % 5 == 0 => println("Buzz"),
        x => println("{}", x),
    }
}

int main() {
    for (int i = 1; i <= 15; i++) {
        fizzbuzz(i);
    }
    return 0;
}
```

---

## Exhaustiveness Checking

### Enum Exhaustiveness

```cm
enum TrafficLight {
    Red = 0,
    Yellow = 1,
    Green = 2
}

void handle_light(int light) {
    match (light) {
        TrafficLight::Red => println("Stop"),
        TrafficLight::Yellow => println("Caution"),
        TrafficLight::Green => println("Go"),
        // Covers all cases
    }
}
```

### Exhaustiveness Error

```cm
// Compile Error: Not all cases are covered
/*
void incomplete(int light) {
    match (light) {
        TrafficLight::Red => println("Stop"),
        TrafficLight::Yellow => println("Caution"),
        // TrafficLight::Green is missing!
    }
}
*/
```

### Solving with Wildcard

```cm
void with_default(int light) {
    match (light) {
        TrafficLight::Red => println("Stop"),
        TrafficLight::Yellow => println("Caution"),
        _ => println("Go or other"),
        // Wildcard covers the rest
    }
}
```

---

## match vs switch

### switch

```cm
int value = 2;

switch (value) {
    case(0) {
        println("zero");
    }
    case(1) {
        println("one");
    }
    case(2) {
        println("two");
    }
    else {
        println("other");
    }
}
```

### match

```cm
int value = 2;

match (value) {
    0 => println("zero"),
    1 => println("one"),
    2 => println("two"),
    _ => println("other"),
}
```

**Benefits of match:**
- ✅ No break needed
- ✅ Exhaustiveness checking
- ✅ Pattern guards
- ✅ Variable binding

---

## Practical Examples

### HTTP Status Codes

```cm
enum HttpStatus {
    Ok = 200,
    NotFound = 404,
    InternalError = 500
}

void handle_response(int status) {
    match (status) {
        HttpStatus::Ok => println("Success"),
        HttpStatus::NotFound => println("Page not found"),
        HttpStatus::InternalError => println("Server error"),
        s if s >= 400 && s < 500 => println("Client error: {}", s),
        s if s >= 500 => println("Server error: {}", s),
        _ => println("Other status: {}", status),
    }
}
```

### Game State Management

```cm
enum GameState {
    Menu = 0,
    Playing = 1,
    Paused = 2,
    GameOver = 3
}

void update_game(int state) {
    match (state) {
        GameState::Menu => println("Show menu"),
        GameState::Playing => println("Update game logic"),
        GameState::Paused => println("Show pause screen"),
        GameState::GameOver => println("Show game over"),
    }
}
```

---

## Common Mistakes

### ❌ Wildcard Order

```cm
// Wrong: Wildcard matches first, making others unreachable
match (value) {
    _ => println("Any"),  // Matches everything
    0 => println("Zero"), // Unreachable
    1 => println("One"),  // Unreachable
}
```

### ❌ Incomplete Patterns

```cm
enum Status { Ok = 0, Error = 1 }

// Error: Not exhaustive
/*
match (status) {
    Status::Ok => println("OK"),
    // Status::Error is missing
}
*/
```

### ❌ Missing Comma

```cm
match (value) {
    0 => println("zero"),
    1 => println("one")  // Missing comma!
    2 => println("two"),
}
```

---

## Practice Problems

### Problem 1: Day of Week
Convert a number (1-7) to a day name.

<details>
<summary>Example Answer</summary>

```cm
void print_day(int day) {
    match (day) {
        1 => println("Monday"),
        2 => println("Tuesday"),
        3 => println("Wednesday"),
        4 => println("Thursday"),
        5 => println("Friday"),
        6 => println("Saturday"),
        7 => println("Sunday"),
        _ => println("Invalid day"),
    }
}

int main() {
    for (int i = 1; i <= 7; i++) {
        print_day(i);
    }
    return 0;
}
```
</details>

---

## Next Steps

✅ Understood match syntax  
✅ Can use pattern guards  
✅ Understood exhaustiveness checking  
⏭️ Next, learn about [Auto Implementation](with-keyword.html)

## Related Links

- [Enums](../types/enums.html) - Combined with match
- [switch](../basics/control-flow.html) - Traditional branching
- [Operators](../basics/operators.html) - Used in guards

---

**Previous:** [Type Constraints](../types/constraints.html)  
**Next:** [Auto Implementation](with-keyword.html)
