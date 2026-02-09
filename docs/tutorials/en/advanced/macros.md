---
title: Macros
parent: Advanced
nav_order: 2
---

# Macros

Cm's macro system provides compile-time code expansion. Similar to C's `#define`, but type-safe.

## Constant Macros

### Basic Usage

```cm
// Define constant macros
macro int VERSION = 13;
macro string APP_NAME = "MyApp";
macro bool DEBUG = true;

int main() {
    println("Version: {VERSION}");    // Version: 13
    println("App: {APP_NAME}");       // App: MyApp
    
    if (DEBUG) {
        println("Debug mode");
    }
    
    return 0;
}
```

### Supported Types

| Type | Example |
|------|---------|
| `int` | `macro int SIZE = 100;` |
| `string` | `macro string NAME = "Hello";` |
| `bool` | `macro bool ENABLED = true;` |

## Function Macros

### No-argument Function Macro

```cm
macro int VERSION = 13;

// No-argument function macro
macro int*() get_version = () => VERSION;

int main() {
    int v = get_version();  // Expands: VERSION -> 13
    println(v);             // 13
    return 0;
}
```

### Function Macro with Arguments

```cm
// Function macros with arguments
macro int*(int, int) add = (int a, int b) => a + b;
macro int*(int, int) mul = (int a, int b) => a * b;

int main() {
    int sum = add(3, 5);       // Expands: 3 + 5 -> 8
    int product = mul(4, 6);   // Expands: 4 * 6 -> 24
    
    println("Sum: {sum}");         // Sum: 8
    println("Product: {product}"); // Product: 24
    
    // Combining macros
    int combined = add(mul(2, 3), 4);  // (2*3) + 4 = 10
    println("Combined: {combined}");    // Combined: 10
    
    return 0;
}
```

### Function Macro Syntax

```cm
macro ReturnType*(ArgType1, ArgType2, ...) macro_name = (arg1, arg2, ...) => expression;
```

## Macros vs const

| Feature | macro | const |
|---------|-------|-------|
| Expansion | Before compile | At compile time |
| Function form | ✅ Supported | ❌ Not supported |
| Type checking | ✅ Typed | ✅ Typed |
| Use case | Conditional compile, DSL | Constant values |

## Example: Debug Macro

```cm
macro bool DEBUG = true;

macro void*(string) debug_log = (string msg) => {
    if (DEBUG) {
        println("[DEBUG] {msg}");
    }
};

int main() {
    debug_log("Starting...");  // Outputs only if DEBUG is true
    
    // Main processing
    int result = 42;
    
    debug_log("Result: {result}");
    
    return 0;
}
```

## Notes

- Macros can only be defined at global scope
- Macros cannot be used before their definition
- Circular references are not allowed

## Related Topics

- [Constants](const.md) - const keyword
---

**Last Updated:** 2026-02-08
