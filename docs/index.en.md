---
layout: default
title: Home (EN)
nav_order: 1
---

# Cm Programming Language

**A modern systems programming language with C++ syntax and Rust-inspired features**

> **Supported Platforms**: macOS (ARM64) / Ubuntu (x86_64) | [Details](QUICKSTART.html)

---

## 🚀 Quick Links

- [📚 Getting Started](QUICKSTART.html)
- [📖 Tutorials](tutorials/en/index.html)
- [📋 Release Notes](releases/)
- [🏗️ Project Structure](PROJECT_STRUCTURE.html)

---

## 📖 Documentation

### For Users

- **[Tutorials](tutorials/en/index.html)** - Step-by-step learning guide
  - [Basics](tutorials/en/basics/introduction.html) - Variables, functions, control flow
  - [Types](tutorials/en/types/structs.html) - Structs, enums, interfaces
  - [Advanced](tutorials/en/advanced/match.html) - Generics, macros, match
  - [Compiler](tutorials/en/compiler/usage.html) - LLVM backend, optimization
  - [Standard Library](tutorials/ja/stdlib/) - HTTP, Networking, Threading, GPU

- **[Quick Start Guide](QUICKSTART.html)** - Get started in 5 minutes

### For Developers

- **[Project Structure](PROJECT_STRUCTURE.html)** - Codebase organization
- **[Development Guide](DEVELOPMENT.html)** - Environment setup
- **[Contribution Guide](CONTRIBUTING.html)** - Guidelines and procedures
- **[Design Documents](design/)** - Language specifications and architecture

### Release Information

- **[Release Notes](releases/)** - Version history and changelogs
- **[Feature Reference](FEATURES.html)** - Current implementation status

---

## 🎯 Language Features

### ✅ Language Core (v0.13.1)

- **C++ Style Syntax** - Familiar and readable
- **Strong Type System** - Compile-time safety
- **Generics** - Type-safe generic programming
- **Interfaces** - Trait-based polymorphism
- **Pattern Matching** - Powerful match expressions with guards
- **Inline Assembly** - `__asm__` for hardware access
- **Conditional Compilation** - `#ifdef`/`#ifndef` directives

### ✅ Backends

- **LLVM Native** - ARM64/x86_64 native code generation
- **WASM** - WebAssembly backend

### ✅ Standard Library (Native)

- **Collections** - `Vector<T>`, `Queue<T>`, `HashMap<K,V>`
- **Threading** - `std::thread`, `Mutex`, `Channel`
- **Networking** - `std::http` (HTTP/HTTPS, OpenSSL integration)
- **GPU** - `std::gpu` (Apple Metal backend)

### 🔄 In Progress

- **Package Management** - `cm pkg init/add`
- **Ownership System** - Borrow checker improvements
- **JS Backend** - JavaScript code generation (planned for v0.14.0)

---

## 💡 Example Code

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
    void draw() { println("Circle({})", self.radius); }
}
```

---

## 🛠️ Building from Source

```bash
# Clone the repository
git clone https://github.com/shadowlink0122/Cm.git
cd Cm

# Build with LLVM backend
cmake -B build -DCM_USE_LLVM=ON
cmake --build build

# Run tests
ctest --test-dir build
```

---

## 📊 Project Stats

| Component | Status | Coverage |
|-----------|--------|----------|
| Lexer/Parser | ✅ Complete | 90%+ |
| Type System | ✅ Complete | 85%+ |
| HIR/MIR | ✅ Complete | 80%+ |
| LLVM Backend | ✅ Complete | 85%+ |
| WASM Backend | ✅ Complete | 80%+ |
| Standard Library | 🔄 In Progress | 30%+ |

---

## 🤝 Contributing

We welcome contributions! Please see our [Contribution Guide](CONTRIBUTING.html) for details.

---

## 📄 License

This project is licensed under the MIT License - see the LICENSE file for details.

---

## 🔗 Links

- [GitHub Repository](https://github.com/shadowlink0122/Cm)
- [Issue Tracker](https://github.com/shadowlink0122/Cm/issues)

---

**Last Updated:** v0.13.1 (February 2026)

© 2025-2026 Cm Language Project

---
[日本語版はこちら](../index.html)