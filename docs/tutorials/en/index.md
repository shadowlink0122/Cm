---
layout: default
title: Tutorials
nav_order: 2
has_children: true
---

[日本語](../ja/)

# Cm Language Tutorials v0.14.0

**Target Version:** v0.14.0  
**Last Updated:** 2026-02-10

A comprehensive collection of tutorials to learn all features of the Cm language step-by-step.

---

## 📚 Learning Paths

### Path 1: Learning the Basics (For Beginners)

Estimated Time: 3-4 hours

1. **[Basics](basics/introduction.html)** - Language foundations (10 tutorials)
   - [Introduction](basics/introduction.html) - Features and design philosophy
   - [Setup](basics/setup.html) - Compiler build and setup
   - [Hello, World!](basics/hello-world.html) - Your first program
   - [Variables and Types](basics/variables.html) - Primitive types, const/static
   - [Operators](basics/operators.html) - Arithmetic, comparison, logical operations
   - [Control Flow](basics/control-flow.html) - if/while/for/switch/defer
   - [Functions](basics/functions.html) - Definition, overloading, default arguments
   - [Arrays](basics/arrays.html) - Declaration, methods, for-in
   - [Pointers](basics/pointers.html) - Addresses, dereference, Array Decay
   - [Modules](basics/modules.html) - Module system and imports

### Path 2: Learning the Type System (For Intermediate Users)

Estimated Time: 4-5 hours

2. **[Type System](types/structs.html)** - Advanced type features
   - [Structs](types/structs.html) - Definition, constructors, nesting
   - [Enums](types/enums.html) - Enumerations, Tagged Unions, match decomposition
   - [typedef](types/typedef.html) - Type aliases and literal types
   - [Generics](types/generics.html) - Type parameters, inference, monomorphization
   - [Interfaces](types/interfaces.html) - interface/impl/self
   - [Constraints](types/constraints.html) - AND/OR boundaries, where clause
   - [Ownership](types/ownership.html) - Move semantics, borrowing
   - [Lifetimes](types/lifetimes.html) - Reference validity

### Path 3: Learning Advanced Features (For Advanced Users)

Estimated Time: 5-6 hours

3. **[Advanced Features](advanced/match.html)** - Powerful language features
   - [match Expression](advanced/match.html) - Pattern matching, guards, exhaustiveness
   - [Auto Implementation](advanced/with-keyword.html) - with Eq/Ord/Clone/Hash
   - [Operator Overloading](advanced/operators.html) - Custom operators
   - [Function Pointers](advanced/function-pointers.html) - Higher-order functions
   - [Lambda](advanced/lambda.html) - Closures
   - [String Operations](advanced/strings.html) - Methods and slicing
   - [Slices](advanced/slices.html) - Dynamic arrays
   - [FFI](advanced/ffi.html) - C interop
   - [Threads](advanced/thread.html) - Parallel processing
   - [const](advanced/const.html) - Compile-time constants
   - [must keyword](advanced/must.html) - Enforced return value usage
   - [Macros](advanced/macros.html) - Conditional compilation

### Path 4: Learning Compilation

Estimated Time: 3 hours

4. **[Compiler](compiler/usage.html)** - Build and backends
   - [Usage](compiler/usage.html) - Commands and options
   - [LLVM Backend](compiler/llvm.html) - Native compilation
   - [WASM Backend](compiler/wasm.html) - WebAssembly output
   - [JS Backend](compiler/js-compilation.html) - JavaScript output
   - [Preprocessor](compiler/preprocessor.html) - Conditional compilation
   - [Linter](compiler/linter.html) - Static analysis (cm lint)
   - [Formatter](compiler/formatter.html) - Code formatting (cm fmt)
   - [Optimization](compiler/optimization.html) - O0-O3, tail call optimization

### Path 5: Learning Internals (For Developers)

Estimated Time: 3 hours

5. **[Internals](internals/architecture.html)** - How the compiler works
   - [Architecture](internals/architecture.html) - Overall structure and pipeline
   - [Algorithms](internals/algorithms.html) - Analysis and optimization algorithms
   - [Optimization](internals/optimization.html) - MIR/LLVM level optimizations

---

## 🎯 Guide by Difficulty

### 🟢 Beginner - For those with programming experience

- Hello, World!
- Variables and Types
- Operators
- Control Flow
- Functions

### 🟡 Intermediate - Understand the basics of Cm

- Arrays and Pointers
- Structs and Enums
- Interfaces
- Threads

### 🔴 Advanced - Requires deep understanding of type systems and memory management

- Generics and Constraints
- match Expression and Pattern Guards
- FFI and inline assembly

---

## 🔗 Related Links

- [Language Specification](../../design/CANONICAL_SPEC.html) - Full language spec
- [Design Documents](../../design/) - Architecture and design docs
- [Test Cases](https://github.com/shadowlink0122/Cm/tree/main/tests/test_programs/) - 368 files

---

**Total Tutorials:** 42 files  
**Estimated Time:** 18-22 hours  
**Target Version:** v0.14.0

---

**Last Updated:** 2026-02-10  
**Author:** Cm Language Development Team

---
[日本語](../ja/)
