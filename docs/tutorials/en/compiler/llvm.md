---
title: LLVM Backend
parent: Tutorials
---

[日本語](../../ja/compiler/llvm.html)

# Compiler - LLVM Backend

**Difficulty:** 🟡 Intermediate  
**Time:** 25 minutes

## Features

- Native code generation
- Multi-platform support
- Advanced optimization
- Debug information generation

## Support Status

| Feature | Status |
|---------|--------|
| Primitives | ✅ Complete |
| Structs | ✅ Complete |
| Arrays | ✅ Complete |
| Pointers | ✅ Complete |
| Generics | ✅ Complete |
| Interfaces | ✅ Complete |
| match | ✅ Complete |
| with | ✅ Complete |
| typedef pointers | ⚠️ Future implementation |

## Compilation Examples

```bash
# Basic compilation
cm compile hello.cm -o hello

# Optimization level
cm compile program.cm -O3 -o program

# Inspect LLVM IR
cm compile program.cm --emit-llvm -o program.ll
cat program.ll

# With debug info
cm compile program.cm -g -o program
```

## Optimization Levels

- `-O0`: No optimization (Debug)
- `-O1`: Basic optimization
- `-O2`: Standard optimization (Recommended)
- `-O3`: Maximum optimization

---

**Previous:** [Compiler Usage](usage.html)  
**Next:** [WASM Backend](wasm.html)
---

**Last Updated:** 2026-02-08
