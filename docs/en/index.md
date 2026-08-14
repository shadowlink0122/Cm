---
layout: default
title: Home (English)
nav_exclude: true
---

[日本語](../index.html)

# The Cm Programming Language

**A modern systems programming language combining C++-like syntax with Rust-inspired features**

> **Supported environments**: macOS (ARM64) / Ubuntu (x86_64) | [Details](../QUICKSTART.html)

---

## 🚀 Quick links

- [📚 Getting started](../QUICKSTART.html)
- [📖 Tutorials](../tutorials/en/index.html)
- [📋 Release notes](../releases/)
- [🏗️ Project structure](../PROJECT_STRUCTURE.html)

---

## 🎯 Language features

### ✅ Language core

- **C++-like syntax** — familiar and readable
- **Strong type system** — compile-time safety
- **Generics** — type-safe generic programming
- **Interfaces** — trait-based polymorphism
- **Pattern matching** — powerful match expressions with guards
- **Operator overloading** — defined directly via `impl T { operator ... }`
- **Inline assembly** — hardware access via `__asm__`
- **Conditional compilation** — `#ifdef`/`#ifndef` directives

### ✅ Backends

- **LLVM Native** — ARM64/x86_64 native code generation
- **WASM** — WebAssembly backend
- **JavaScript** — JS generation for Node.js/browsers
- **SystemVerilog** — RTL generation for FPGAs (with synthesis/simulation verification)

### ✅ Standard library (Native)

- **Collections** — `Vector<T>`, `Queue<T>`, `HashMap<K,V>`
- **Threads** — `native::thread`, `Mutex`, `Channel`
- **Networking** — `native::http` (HTTP/HTTPS, OpenSSL)
- **GPU** — `native::gpu` (Apple Metal backend)

---

## 💡 Example

```cm
import std::io::println;

// Hello World
int main() { println("Hello, Cm!"); return 0; }

// Generic function
<T: Ord> T max(T a, T b) { return a > b ? a : b; }

// Interface implementation
interface Drawable { void draw(); }
struct Circle { int radius; }
impl Circle for Drawable {
    void draw() { println("Circle({self.radius})"); }
}
```

---

## 🔗 Links

- [GitHub repository](https://github.com/shadowlink0122/Cm)
- [Issue tracker](https://github.com/shadowlink0122/Cm/issues)

---

**Last Updated:** v0.17.1 (2026-08-14)

© 2025-2026 Cm Language Project
