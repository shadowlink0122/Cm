---
layout: default
title: Home
nav_order: 1
---

# Cm Programming Language

**A modern systems programming language with C++ syntax and Rust-inspired features**

---

## 🚀 Quick Links

- [📚 Getting Started](QUICKSTART.md)
- [📖 Tutorials](tutorials/)
- [📋 Release Notes](releases/)
- [🏗️ Project Structure](PROJECT_STRUCTURE.md)
- [🎯 Roadmap](../ROADMAP.md)

---

## 📖 Documentation

### For Users

- **[Tutorials](tutorials/)** - Step-by-step learning guide
  - [Basics](tutorials/basics/) - Variables, functions, control flow
  - [Types](tutorials/types/) - Structs, enums, interfaces
  - [Advanced](tutorials/advanced/) - Generics, macros, match
  - [Compiler](tutorials/compiler/) - LLVM backend, optimization

- **[Quick Start Guide](QUICKSTART.md)** - Get started in 5 minutes

### For Developers

- **[Project Structure](PROJECT_STRUCTURE.md)** - Codebase organization
- **[Development Guide](DEVELOPMENT.md)** - Contributing guidelines
- **[Design Documents](design/)** - Language specifications and architecture

### Release Information

- **[Release Notes](releases/)** - Version history and changelogs
- **[Project Status](PROJECT_STATUS.md)** - Current implementation status

---

## 🎯 Language Features

### ✅ Implemented (v0.9.0)

- **C++ Style Syntax** - Familiar and readable
- **Strong Type System** - Compile-time safety
- **Generics** - Type-safe generic programming
- **Interfaces** - Trait-based polymorphism
- **LLVM Backend** - Native code generation
- **Pattern Matching** - Powerful match expressions
- **Zero-cost Abstractions** - Performance without overhead

### 🔄 In Progress

- **WASM Support** - WebAssembly backend
- **Module System** - Package and dependency management
- **Standard Library** - Core utilities and data structures

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
git clone https://github.com/yourusername/Cm.git
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
| LLVM Backend | 🔄 In Progress | 70%+ |
| Standard Library | 🔄 In Progress | 30%+ |

---

## 🤝 Contributing

We welcome contributions! Please see our [Development Guide](DEVELOPMENT.md) for details.

---

## 📄 License

This project is licensed under the MIT License - see the LICENSE file for details.

---

## 🔗 Links

- [GitHub Repository](https://github.com/yourusername/Cm)
- [Issue Tracker](https://github.com/yourusername/Cm/issues)
- [Discussions](https://github.com/yourusername/Cm/discussions)

---

**Last Updated:** v0.10.1 (January 2025)

© 2024-2025 Cm Language Project
