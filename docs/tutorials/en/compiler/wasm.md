---
title: WASM Backend
parent: Tutorials
---

[日本語](../../ja/compiler/wasm.html)

# Compiler - WASM Backend

**Difficulty:** 🟡 Intermediate  
**Time:** 20 minutes

## Features

- WebAssembly output
- Browser execution
- Serverless environment support

## Support Status

| Feature | Status |
|---------|--------|
| Basics | ✅ Complete |
| Arrays/Pointers | ✅ Complete |
| Generics | ✅ Complete |
| with | ✅ Complete |

## Auto Implementation with `with`

The following interfaces can be automatically implemented using the `with` keyword:

| Interface | Functionality | Generated Method |
|-----------|---------------|------------------|
| `Eq` | Equality | `operator ==(T other)` |
| `Ord` | Ordering | `operator <(T other)` |
| `Clone` | Copying | `clone()` |
| `Hash` | Hashing | `hash()` |
| `Debug` | Debug display | `debug()` |
| `Display` | Stringify | `toString()` |

```cm
// Auto-implement multiple interfaces
struct Point with Eq, Ord, Clone {
    int x;
    int y;
}

int main() {
    Point p1 = Point(10, 20);
    Point p2 = p1.clone();
    
    if (p1 == p2) {
        println("equal");  // Printed
    }
    return 0;
}
```

## Running WASM

```bash
# Compile to WASM
cm compile program.cm --target=wasm -o program.wasm

# Run with wasmtime
wasmtime program.wasm

# Run in Browser (Future)
# <script src="program.wasm"></script>
```

## Use Cases

1. **Browser Apps**
   - Web Applications
   - Game Development
   - Data Visualization

2. **Serverless**
   - Cloudflare Workers
   - Fastly Compute@Edge
   - AWS Lambda (Custom Runtime)

3. **Sandboxed Environments**
   - Secure Code Execution
   - Plugin Systems

---

**Previous:** [LLVM Backend](llvm.html)  
**Completion:** Tutorial Completed!

---

**Last Updated:** 2026-02-08
