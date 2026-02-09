---
title: Preprocessor
parent: Compiler
nav_order: 7
---

[日本語](../../ja/compiler/preprocessor.html)

# Compiler - Preprocessor (Conditional Compilation)

**Difficulty:** 🟡 Intermediate  
**Duration:** 15 min

## 📚 What You'll Learn

- Conditional compilation basics (`#ifdef` / `#ifndef` / `#else` / `#end`)
- Built-in constants (architecture, OS, compiler info)
- Practical multi-platform code examples

---

## What is Conditional Compilation?

Cm's preprocessor **filters source code before compilation**.
You can switch code based on the target architecture or operating system.

```cm
import std::io::println;

int main() {
    #ifdef __x86_64__
        println("Running on x86_64");
    #end

    #ifdef __arm64__
        println("Running on ARM64");
    #end

    return 0;
}
```

---

## Directives

| Directive | Description |
|-----------|-------------|
| `#ifdef SYMBOL` | Include following code if symbol is defined |
| `#ifndef SYMBOL` | Include following code if symbol is NOT defined |
| `#else` | Alternative branch for preceding `#ifdef` / `#ifndef` |
| `#end` | End a conditional block |

> ⚠️ Note: Cm uses `#end`, not `#endif` as in C/C++.

---

## Basic Usage

### ifdef / end

```cm
#ifdef __macos__
    // Code only executed on macOS
    println("macOS detected");
#end
```

### ifdef / else / end

```cm
#ifdef __x86_64__
    println("x86_64 architecture");
#else
    println("Other architecture");
#end
```

### ifndef

```cm
#ifndef __DEBUG__
    // Only in release builds
    println("Release mode");
#end
```

### Nesting

```cm
#ifdef __macos__
    #ifdef __arm64__
        println("Apple Silicon Mac");
    #else
        println("Intel Mac");
    #end
#end
```

---

## Built-in Constants

### Architecture

| Constant | Description |
|----------|-------------|
| `__x86_64__` | x86_64 (64-bit Intel/AMD) |
| `__x86__` | x86 (includes 32-bit compat) |
| `__arm64__` | ARM64 (Apple Silicon, etc.) |
| `__aarch64__` | AArch64 (alias for `__arm64__`) |
| `__i386__` | i386 (32-bit x86) |
| `__riscv__` | RISC-V |

### Operating System

| Constant | Description |
|----------|-------------|
| `__macos__` | macOS |
| `__apple__` | Apple platforms |
| `__linux__` | Linux |
| `__windows__` | Windows |
| `__freebsd__` | FreeBSD |
| `__unix__` | Unix-like (macOS/Linux/FreeBSD) |

### Compiler & Build Info

| Constant | Description |
|----------|-------------|
| `__CM__` | Cm compiler (always defined) |
| `__DEBUG__` | Defined in debug builds |
| `__64BIT__` | 64-bit environment |
| `__32BIT__` | 32-bit environment |

---

## Practical Examples

### Multi-Architecture Inline Assembly

```cm
import std::io::println;

int main() {
    int x = 50;

    #ifdef __x86_64__
        // x86_64: AT&T syntax
        __asm__("addl $$10, ${+r:x}");
    #else
        // ARM64: AArch64 syntax
        __asm__("add ${+r:x}, ${+r:x}, #10");
    #end

    println("Result: {x}");  // 60
    return 0;
}
```

### OS-Specific Code

```cm
int main() {
    #ifdef __macos__
        println("macOS specific code");
    #end

    #ifdef __linux__
        println("Linux specific code");
    #end

    #ifdef __windows__
        println("Windows specific code");
    #end

    return 0;
}
```

### Debug-Only Code

```cm
import std::io::println;

void process_data(int value) {
    #ifdef __DEBUG__
        println("Debug: process_data({value})");
    #end

    // ... main logic ...
}
```

---

## How It Works

The preprocessor operates **before the lexer** at the source code level:

```
Source Code → ImportPreprocessor → ConditionalPreprocessor → Lexer → Parser
```

- Directive lines are **replaced with blank lines**, preserving line numbers
- Error messages correctly reference original source file line numbers

---

## Important Notes

- `#ifdef` / `#end` must always be **paired**. Missing `#end` causes errors
- Directives should be at the **beginning of a line** (leading whitespace is OK)
- Built-in constants are detected from the **host compiler's environment**

---

## Next Steps

✅ Understood conditional compilation basics  
✅ Know how to use built-in constants  
✅ Learned multi-platform coding patterns  
⏭️ Next, learn about [Optimization](optimization.html)

---

**Previous:** [Wasm](wasm.html)  
**Next:** [Optimization](optimization.html)

## Related Topics

- [must keyword](../advanced/must.html) - Preventing dead code elimination
- [Macros](../advanced/macros.html) - Compile-time constants

---

**Last Updated:** 2026-02-08
