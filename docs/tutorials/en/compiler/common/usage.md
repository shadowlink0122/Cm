---
title: Compiler Usage
parent: Tutorials
---

[日本語](../../../ja/compiler/common/usage.html)

# Compiler - Usage

**Difficulty:** 🟡 Intermediate  
**Time:** 25 minutes

## 📚 What you'll learn

- Basic usage of `cm` command
- Subcommands and options
- Debugging
- Output formats

---

## Basic Usage

### Interpreter Execution

```bash
# Basic
./build/bin/cm run program.cm

# Multiple files (Future)
./build/bin/cm run main.cm lib.cm

# From stdin
echo 'int main() { println("Hello"); return 0; }' | ./build/bin/cm run -
```

### Compilation

```bash
# Generate executable
./build/bin/cm compile program.cm -o program

# Default output (a.out)
./build/bin/cm compile program.cm

# Run
./program
```

---

## Subcommands

### run - Interpreter

```bash
cm run program.cm
```

**Pros/Cons:**
- ✅ Instant execution
- ✅ Easy debugging
- ❌ Slow execution speed

### compile - Compilation

```bash
cm compile program.cm -o output
```

**Pros/Cons:**
- ✅ Fast executable
- ✅ Optimization enabled
- ❌ Compilation time

### check - Syntax & Type Checking

```bash
cm check program.cm
```

**Pros/Cons:**
- ✅ No compilation needed
- ✅ Detailed error location
- ✅ No LLVM required

---

## Compiler Options

### Optimization Levels

```bash
# No optimization (Debug)
cm compile program.cm -O0

# Basic optimization
cm compile program.cm -O1

# Standard optimization (Recommended)
cm compile program.cm -O2

# Maximum optimization
cm compile program.cm -O3
```

### Target Specification

```bash
# Native code (Default)
cm compile program.cm -o program

# WebAssembly
cm compile program.cm --target=wasm -o program.wasm

# JavaScript
cm compile program.cm --target=js -o program.js
```

### Output Formats

```bash
# Executable (Default)
cm compile program.cm -o program

# LLVM IR
cm compile program.cm --emit-llvm -o program.ll

# Assembly
cm compile program.cm --emit-asm -o program.s
```

---

## Debugging Options

### Debug Mode

```bash
# Show runtime debug info
cm run program.cm --debug

# Short option
cm run program.cm -d
```

### Compile with Debug Info

```bash
# Include debug symbols
cm compile program.cm -g -o program_debug

# Debug with GDB
gdb ./program_debug
```

---

## Next Steps

✅ Understood `cm` command basics  
✅ Learned debugging methods  
✅ Learned optimization options  
✅ Understood targets (Native/WASM/JS)  
⏭️ Next, learn about [LLVM Backend](../native/index.html)

## Related Links

- [Environment Setup](../../basics/setup.html) - Make command reference
- [Linter](linter.html) - Static analysis
- [Formatter](formatter.html) - Code formatting
- [JS Backend](../js/index.html) - JavaScript output

---

**Previous:** [String Operations](../../advanced/strings.html)  

---

**Last Updated:** 2026-02-10

---

<!-- nav -->
← Prev: [Compiler](../index.html) | [Contents](../index.html) | Next: [Compiler - Preprocessor (Conditional Compilation)](preprocessor.html) →
