---
layout: default
title: Home (EN)
nav_order: 1
---

[日本語](./)

# Cm Programming Language

**A modern systems programming language with C++ syntax and Rust-inspired features**

---

## 🚀 Quick Links

- [📚 Getting Started](QUICKSTART.html)
- [📖 Tutorials](tutorials/en/)
- [📋 Release Notes](releases/)
- [🏗️ Project Structure](PROJECT_STRUCTURE.html)
- [🎯 Roadmap](../../ROADMAP.html)

---

## 📖 Documentation

### For Users

- **[Tutorials](tutorials/en/)** - Step-by-step learning guide
  - [Basics](tutorials/en/basics/) - Variables, functions, control flow
  - [Types](tutorials/en/types/) - Structs, enums, interfaces
  - [Advanced](tutorials/en/advanced/) - Generics, macros, match
  - [Compiler](tutorials/en/compiler/) - LLVM backend, optimization

- **[Quick Start Guide](QUICKSTART.html)** - Get started in 5 minutes

### For Developers

- **[Project Structure](PROJECT_STRUCTURE.html)** - Codebase organization
- **[Development Guide](DEVELOPMENT.html)** - Environment setup
- **[Contribution Guide](CONTRIBUTING.html)** - Guidelines and procedures
- **[Design Documents](design/)** - Language specifications and architecture

### Release Information

- **[Release Notes](releases/)** - Version history and changelogs
- **[Feature Reference](features/)** - Current implementation status
- **[Roadmap](../../ROADMAP.html)** - Future plans

---

## 🎯 Language Features

### ✅ Implemented (v0.10.0)

- **C++ Style Syntax** - Familiar and readable
- **Strong Type System** - Compile-time safety
- **Generics** - Type-safe generic programming
- **Interfaces** - Trait-based polymorphism
- **LLVM Backend** - Native code generation
- **WASM Support** - WebAssembly backend
- **Pattern Matching** - Powerful match expressions
- **Zero-cost Abstractions** - Performance without overhead

### 🔄 In Progress

- **Standard Library** - Core utilities and data structures
- **Module System** - Enhanced package management
- **Ownership System** - Memory safety without GC (v0.11.0+)

---

## 💡 Example Code

```cm
// Hello World
int main() {
    println("Hello, Cm!");
    return 0;
}

// Generic function
<T: Ord> T max(T a, T b) {
    return a > b ? a : b;
}

// Interface implementation
interface Drawable {
    void draw();
}

struct Circle {
    int radius;
}

impl Circle for Drawable {
    void draw() {
        println("Drawing circle with radius {}", self.radius);
    }
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
- [Discussions](https://github.com/shadowlink0122/Cm/discussions)

---

**Last Updated:** v0.10.0 (January 2025)

© 2025-2025 Cm Language Project

---
[日本語版はこちら](./)
