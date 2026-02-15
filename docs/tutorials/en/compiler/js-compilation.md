---
title: JS Compilation
parent: Compiler
---

# Compiling Cm to JavaScript

The Cm compiler provides a backend that converts Cm source code to JavaScript.
Generated JS code can be executed with Node.js or in browsers.

## Basic Usage

### Compile

```bash
# Default output (output.js)
./cm compile --target=js hello.cm

# Specify output file name
./cm compile --target=js hello.cm -o app.js
```

### Run

```bash
node output.js
```

## Supported Features

The Cm JS backend supports the following features:

| Category | Status | Example |
|---------|---------|-----|
| Basic types | ✅ | int, float, double, string, bool, char |
| Arrays / Slices | ✅ | `int[3]`, `int[]` |
| Structs | ✅ | struct, impl, methods |
| Enum / Match | ✅ | Associated Data, pattern matching |
| Generics | ✅ | `Vector<T>`, `Pair<A, B>` |
| Interfaces | ✅ | interface, Display, Debug, etc. |
| Closures / Lambda | ✅ | Closures with capture |
| String interpolation | ✅ | `"x={x}"` |
| Iterators | ✅ | for-in, range, map, filter |
| Modules | ✅ | import, namespace |
| Ownership / Move | ✅ | Move semantics |

## Unsupported Features

The following features are not supported in the JS target (they are skipped):

| Category | Reason |
|---------|------|
| Pointers (`int*`, `void*`) | No pointer concept in JS |
| Inline assembly (`__asm__`) | Cannot run in JS |
| Threads (`spawn`, `join`) | Single-threaded model |
| Mutex / Channel / Atomic | Same as above |
| GPU computation (Metal API) | Native only |
| File I/O (`std::io`) | Integration with Node.js API is future work |
| TCP/HTTP (`std::net`) | Same as above |
| Low-level memory (`malloc`, `free`) | GC managed |

## Examples

### Hello World

```cm
import std::io::println;

int main() {
    println("Hello from Cm → JavaScript!");
    return 0;
}
```

```bash
./cm compile --target=js hello.cm
node output.js
# Output: Hello from Cm → JavaScript!
```

### Enum and Match

```cm
import std::io::println;

enum Shape {
    Circle(int),
    Rectangle(int, int)
}

int main() {
    Shape s = Shape::Circle(5);
    match (s) {
        Shape::Circle(r) => {
            println("Circle: radius={r}");
        }
        Shape::Rectangle(w, h) => {
            println("Rectangle: {w}x{h}");
        }
    }
    return 0;
}
```

### Generics

```cm
import std::io::println;

struct Pair<A, B> {
    A first;
    B second;
}

int main() {
    Pair<int, string> p = Pair<int, string> { first: 42, second: "hello" };
    println("first={p.first}, second={p.second}");
    return 0;
}
```

### Interfaces

```cm
import std::io::println;

interface Greet {
    void greet();
}

struct Dog {
    string name;
}

impl Greet for Dog {
    void greet() {
        println("Woof! I'm {this.name}");
    }
}

int main() {
    Dog d = Dog { name: "Buddy" };
    d.greet();
    return 0;
}
```

## Testing

JS backend tests can be run with `make tjp`:

```bash
# Run all JS tests (parallel)
make tjp

# Test a specific file
./cm compile --target=js tests/test_programs/enum/associated_data.cm
node output.js
```

### v0.14.0 Test Results

| Item | Value |
|------|-------|
| Total tests | 372 |
| Pass | 285 (77%) |
| Fail | 0 |
| Skipped | 87 |

## Generated JS Structure

The Cm JS backend transforms each Cm function into a JavaScript function:

```javascript
// Cm's main() → JS main()
function main() {
    cm_println_string("Hello!");
    return 0;
}

// Auto-generated runtime helpers
function cm_println_string(s) { console.log(s); }
function cm_println_int(n) { console.log(n); }

// Entry point
main();
```

Structs are converted to JavaScript objects, and enums to tagged objects.

## Notes

- Generated JS code works with Node.js v16 or later
- For browser execution, an environment with `console.log` is required
- Performance is lower than native compilation, but portability is improved
- All numeric types are converted to JavaScript's Number type (64-bit float precision limitations)
