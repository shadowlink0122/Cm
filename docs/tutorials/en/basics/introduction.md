---
title: Introduction
parent: Tutorials
---

[日本語](../../ja/basics/introduction.html)

# Basics - Introduction

**Difficulty:** 🟢 Beginner  
**Time:** 10 minutes

## What is Cm?

Cm is a modern systems programming language designed to combine the **familiar syntax of C++** with the **safety and powerful features of Rust**.

### Design Goals

1.  **Readability**: Familiar syntax for C++/Java/C# developers.
2.  **Safety**: Strong type system and modern memory management.
3.  **Performance**: High performance through LLVM backend, comparable to C++.
4.  **Modern Features**: First-class support for Pattern Matching, Generics, and Interfaces.

---

## Core Features

### 1. Familiar Syntax
Based on C++ syntax, it eliminates unnecessary complexity while maintaining power.

### 2. High Portability
Supports multiple backends:
- **Native**: Executables via LLVM (x86_64, ARM64).
- **WebAssembly**: For browser-based high-performance applications.
- **JavaScript**: JS code generation for Node.js/browsers.
- **JIT**: For quick testing and scripting.

### 3. Strong Type System
Strict type checking prevents bugs at compile time.

### 4. Zero-cost Abstractions
Features like Generics and Interfaces are implemented without runtime overhead.

---

## Example Code

```cm
int main() {
    println("Hello, Cm!");
    return 0;
}
```

---

**Next Chapter:** [Environment Setup](setup.html)
---

**Last Updated:** 2026-02-10
