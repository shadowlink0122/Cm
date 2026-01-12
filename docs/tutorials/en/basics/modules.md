---
title: Modules
parent: Tutorials
---

[日本語](../../ja/basics/modules.html)

# Basics - Modules

**Difficulty:** 🟢 Beginner to 🟡 Intermediate  
**Time:** 30 minutes

## 📚 What you'll learn

- Importing modules
- Exporting symbols
- Module aliases
- File structure

---

## Basic Imports

### Standard Library

```cm
// Import specific function
import std::io::println;

int main() {
    println("Hello, World!");
    return 0;
}
```

### Multiple Imports

```cm
// Import multiple symbols
import std::io::println;
import std::io::print;

int main() {
    print("Hello, ");
    println("World!");
    return 0;
}
```

---

## Wildcard Imports

Import everything from a module using `*`.

```cm
// Import everything from std::io
import std::io::*;

int main() {
    println("All symbols available");
    return 0;
}
```

### Relative Imports

```cm
// Import everything from local module 'utils'
import ./utils::*;

int main() {
    helper_function();  // Function from utils
    return 0;
}
```

---

## Alias Imports

Rename modules or symbols to avoid conflicts or shorten names.

```cm
// Import std::io as io
import std::io as io;

int main() {
    io::println("Called via alias");
    return 0;
}
```

---

## Selective Imports

Import specific symbols in a block.

```cm
// Import only println and print
import std::io::{println, print};

int main() {
    print("Selective ");
    println("Import");
    return 0;
}
```

---

## Exporting

Use `export` to make functions or structs available to other modules.

### Functions

```cm
// utils.cm
export int add(int a, int b) {
    return a + b;
}

// Private function (not exported)
int internal_helper() {
    return 42;
}
```

### Structs

```cm
// models.cm
export struct Point {
    int x;
    int y;
}

export Point new_point(int x, int y) {
    return Point { x: x, y: y };
}
```

### Re-exporting

```cm
// lib.cm
import ./utils;
import ./models;

// Re-export modules
export { utils, models };
```

---

## Project Structure Example

```
my_project/
├── main.cm          # Entry point
├── utils/
│   ├── math.cm      # Math helpers
│   └── strings.cm   # String helpers
└── models/
    └── user.cm      # User struct
```

```cm
// main.cm
import ./utils/math::*;
import ./models/user::User;

int main() {
    int result = add(1, 2);
    User u = User { name: "Alice", age: 30 };
    return 0;
}
```

---

## Circular Dependencies

Cm detects circular dependencies (A imports B, B imports A) and reports an error.
Refactor common code into a third module to resolve this.

---

## Common Mistakes

### ❌ Incorrect Path Syntax

```cm
// Wrong: Use :: for standard lib
// import std/io::println;

// Correct
import std::io::println;
```

### ❌ Using Unexported Symbols

```cm
// Error: internal_helper is not exported
import ./utils::internal_helper;
```

---

## Next Steps

✅ Can organize code with modules  
✅ Understand import/export syntax  
⏭️ Next, explore the [Type System](../types/structs.html)

## Related Links

- [Project Structure](../../PROJECT_STRUCTURE.html)
- FFI (Japanese only)

---

**Previous:** [Pointers](pointers.html)  
**Next:** [Type System](../types/structs.html)