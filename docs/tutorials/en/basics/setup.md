---
title: Setup
parent: Tutorials
---

[日本語](../../ja/basics/setup.html)

# Environment Setup

**Difficulty:** 🟢 Beginner  
**Time:** 30 minutes

## 📚 What you'll learn

- Required dependencies
- Building the compiler
- Using make commands
- Running tests
- Running your first program

---

## Supported Platforms

| OS | Architecture | Status |
|----|-------------|--------|
| **macOS 14+** | ARM64 (Apple Silicon) | ✅ Full support |
| **macOS 14+** | x86_64 (Intel) | ✅ Full support |
| **Ubuntu 22.04** | x86_64 | ✅ Full support |
| Windows | - | ❌ Not supported |

---

## Required Dependencies

### 1. C++20 Compiler

The Cm compiler itself is written in C++20.

| Compiler | Minimum | Recommended |
|----------|---------|-------------|
| **Clang** | 17+ | 17 |
| **GCC** | 12+ | 13 |
| **AppleClang** | 15+ | Auto (with Xcode) |

### 2. CMake 3.20+

Build system for configuring and generating build files.

### 3. Make

Used for build and test commands.

### 4. Git

For source code management.

### 5. LLVM 17

Required for native compilation (`cm compile`) and JIT execution (`cm run`).

> **Note**: Not required for interpreter-only usage, but most features need LLVM.

### 6. Objective-C++ (macOS only)

Required for GPU computation (Apple Metal). Automatically available with Xcode.

```bash
# Install Xcode Command Line Tools (if not already)
xcode-select --install
```

> `std/gpu/gpu_runtime.mm` is compiled as Objective-C++.

---

## Optional Dependencies

### wasmtime (WASM execution)

Required to run WASM binaries.

```bash
# macOS
brew install wasmtime

# Linux
curl https://wasmtime.dev/install.sh -sSf | bash
```

### Node.js (JS execution)

Required to run JS backend output.

```bash
# Node.js v16+ required
node --version
```

---

## Installation Steps

### macOS (ARM64 / Apple Silicon)

```bash
# Xcode Command Line Tools (C++, Objective-C++, Make)
xcode-select --install

# Install via Homebrew
brew install cmake llvm@17

# Set LLVM path (add to .zshrc)
export PATH="/opt/homebrew/opt/llvm@17/bin:$PATH"
export LDFLAGS="-L/opt/homebrew/opt/llvm@17/lib"
export CPPFLAGS="-I/opt/homebrew/opt/llvm@17/include"

# Optional: WASM/JS
brew install wasmtime node
```

### macOS (Intel / x86_64)

```bash
xcode-select --install
brew install cmake llvm@17

# Intel uses /usr/local
export PATH="/usr/local/opt/llvm@17/bin:$PATH"

brew install wasmtime node
```

> **Tip**: Use `uname -m` to check your architecture.
> `arm64` → Apple Silicon, `x86_64` → Intel

### Ubuntu (x86_64)

```bash
# Essential packages
sudo apt-get update
sudo apt-get install -y cmake build-essential git make

# LLVM 17
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 17
sudo apt-get install -y llvm-17-dev clang-17

# Optional
curl https://wasmtime.dev/install.sh -sSf | bash
sudo apt-get install -y nodejs
```

---

## Verify Installation

```bash
clang++ --version      # Clang 17+
cmake --version        # 3.20+
make --version
llvm-config --version  # 17.x
git --version

# Optional
wasmtime --version     # For WASM
node --version         # For JS (v16+)
```

---

## Building the Compiler

### Using make (Recommended)

```bash
git clone https://github.com/shadowlink0122/Cm.git
cd Cm

# Debug build (LLVM enabled)
make build

# Release build (optimized)
make release
```

### Architecture-Specific Builds

Use the `ARCH` variable to specify the target architecture:

```bash
# ARM64 (Apple Silicon)
make build ARCH=arm64

# x86_64 (Intel / Linux)
make build ARCH=x86_64
```

> Default: auto-detected from `llvm-config --host-target`.

### Using CMake Directly

```bash
# Without LLVM (interpreter only)
cmake -B build
cmake --build build

# With LLVM (recommended)
cmake -B build -DCM_USE_LLVM=ON
cmake --build build -j8
```

---

## Make Command Reference

### Build Commands

| Command | Alias | Description |
|---------|-------|-------------|
| `make build` | `make b` | Debug build (LLVM enabled) |
| `make build-all` | - | Build with tests |
| `make release` | - | Release build (optimized) |
| `make clean` | - | Remove build directory |
| `make rebuild` | - | Clean + build |

### Test Commands

| Command | Alias | Description |
|---------|-------|-------------|
| `make test` | `make t` | C++ unit tests (ctest) |
| `make test-jit-parallel` | `make tip` | All JIT tests (parallel) |
| `make test-llvm-parallel` | `make tlp` | All LLVM tests (parallel) |
| `make test-llvm-wasm-parallel` | `make tlwp` | All WASM tests (parallel) |
| `make test-js-parallel` | `make tjp` | All JS tests (parallel) |
| `make test-lint` | `make tli` | Lint tests |

### Optimization-Level Tests

```bash
# JIT tests (O0-O3)
make tip0 / make tip1 / make tip2 / make tip3

# LLVM tests (O0-O3)
make tlp0 / make tlp1 / make tlp2 / make tlp3

# WASM tests (O0-O3)
make tlwp0 / make tlwp1 / make tlwp2 / make tlwp3

# JS tests (O0-O3)
make tjp0 / make tjp1 / make tjp2 / make tjp3
```

### Other Commands

| Command | Description |
|---------|-------------|
| `make format` | Run code formatter |
| `make format-check` | Check formatting (CI) |
| `make help` | Show all commands |

---

## Code Quality Tools

Cm includes three built-in code quality tools:

### cm check - Syntax & Type Checking

Check syntax and types without compiling.

```bash
./cm check program.cm
```

**Output (no errors):**
```
✅ No errors found.
```

**Output (errors):**
```
error: type mismatch: expected 'int', got 'string'
  --> program.cm:5:16
  |
5 |     int x = "hello";
  |              ^^^^^^^
```

### cm lint - Static Analysis

Detect code quality issues and provide suggestions.

```bash
./cm lint program.cm
./cm lint src/           # Recursive
```

**Output:**
```
warning: variable 'myValue' should use snake_case naming [L200]
  --> program.cm:5:9
  |
5 |     int myValue = 10;
  |         ^^^^^^^
  = help: consider renaming to 'my_value'
```

### cm fmt - Code Formatter

Auto-format to a consistent code style.

```bash
./cm fmt program.cm         # Preview
./cm fmt -w program.cm      # Write in-place
./cm fmt -w src/            # Recursive
./cm fmt --check src/       # CI check (fail if unformatted)
```

---

## Your First Program

```bash
cat > hello.cm << 'EOF'
int main() {
    println("Hello, Cm Language!");
    return 0;
}
EOF

# Check syntax
./cm check hello.cm

# Run with JIT
./cm run hello.cm

# Compile to native
./cm compile hello.cm -o hello && ./hello

# Compile to WASM
./cm compile hello.cm --target=wasm -o hello.wasm && wasmtime hello.wasm

# Compile to JS
./cm compile --target=js hello.cm -o hello.js && node hello.js
```

---

## Troubleshooting

### LLVM Not Found

```bash
cmake -B build -DCM_USE_LLVM=ON \
  -DLLVM_DIR=/usr/lib/llvm-17/lib/cmake/llvm
```

### C++20 Compiler Error

```bash
export CXX=clang++ CC=clang
# or
export CXX=g++-12 CC=gcc-12
```

### Library Not Found (Runtime)

```bash
# macOS
export DYLD_LIBRARY_PATH="/opt/homebrew/opt/llvm@17/lib:$DYLD_LIBRARY_PATH"
# Linux
export LD_LIBRARY_PATH="/usr/lib/llvm-17/lib:$LD_LIBRARY_PATH"
```

---

## FAQ

### Q1: Is LLVM mandatory?

Most features require LLVM. `cm check`, `cm lint`, and `cm fmt` work without LLVM.

### Q2: What's needed for GPU support?

macOS only: **Xcode** (includes Objective-C++) and **Metal Framework**. CMake auto-compiles `std/gpu/gpu_runtime.mm`. Not available on Linux.

### Q3: When to use the ARCH option?

For cross-compilation or explicit architecture targeting. Usually auto-detection works fine.

---

## Next Steps

✅ Environment setup complete  
✅ Compiler built  
✅ Make commands understood  
✅ Code quality tools tested  
⏭️ Next, write your first program in [Hello, World!](hello-world.html)

## Related Links

- [Compiler Usage](../compiler/usage.html)
- [Linter](../compiler/linter.html) - Static analysis
- [Formatter](../compiler/formatter.html) - Code formatting
- [JS Backend](../compiler/js-compilation.html) - JavaScript output

---

**Previous:** [Introduction](introduction.html)  
**Next:** [Hello, World!](hello-world.html)
---

**Last Updated:** 2026-02-10
