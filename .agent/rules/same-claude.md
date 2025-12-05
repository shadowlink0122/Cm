---
trigger: always_on
---

# CLAUDE.md - Cm Language Project

## Project Overview

Cm (シーマイナー) is a next-generation programming language designed for both low-level systems (OS, embedded) and web frontend development. It is a complete redesign of the [Cb language](https://github.com/shadowlink0122/Cb).

**Goals:**
- Write once, compile to native (via Rust transpilation) or web (WASM/TypeScript)
- Modern type system with generics, async/await, pattern matching
- Integrated package manager (Cargo-style)

## Development Language

- **C++17** (required)
- Compilers: Clang 5+, GCC 7+, MSVC 2017+

## Directory Structure

```
Cm/
├── src/
│   ├── frontend/       # Lexer, Parser, AST
│   ├── middle/         # HIR, Type checking
│   ├── backend/        # Rust/WASM/TS code generation
│   └── common/         # Shared utilities, diagnostics
├── docs/
│   ├── design/         # Architecture, IR designs
│   └── spec/           # Language specification
├── tests/              # Test suites
└── examples/           # Sample Cm programs
```

## Compilation Pipeline

```
Phase 1 (Current):
  Source → Lexer → Parser → AST → TypeCheck → HIR → [Rust/WASM/TS]
                                                         ↓
                                               [rustc] → Native

Phase 2 (Future):
  HIR → MIR (SSA) → Optimization → Backends

Phase 3 (Future):
  MIR → LIR → LLVM/Cranelift → Native (direct)
```

## Build Commands

```bash
# Configure
cmake -B build -DCMAKE_CXX_STANDARD=17

# Build
cmake --build build

# Test
ctest --test-dir build
```

## Coding Conventions

- **Language**: C++17
- **Naming**: `snake_case` for functions/variables, `PascalCase` for types
- **Smart Pointers**: `std::unique_ptr` for AST/IR node ownership
- **Variant**: `std::variant` for IR node types
- **Optional**: `std::optional` for nullable values

## Key Design Decisions

1. **Transpiler First**: HIR → Rust/WASM/TS (Phase 1)
2. **C++17**: Wide compiler support, sufficient modern features
3. **Multi-Target HIR**: Designed for Rust ∩ TypeScript common subset
4. **Future Native**: Cranelift or LLVM in Phase 3

## Important Files

- `docs/design/architecture.md` - Overall compiler architecture
- `docs/design/hir.md` - HIR design (multi-target optimized)
- `docs/design/backends.md` - Rust/WASM/TS backends
- `docs/design/package_manager.md` - Package management system

## Related Projects

- [Cb Language](https://github.com/shadowlink0122/Cb) - Predecessor project
