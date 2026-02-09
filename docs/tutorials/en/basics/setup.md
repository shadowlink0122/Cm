---
title: Setup
parent: Tutorials
---

[日本語](../../ja/basics/setup.html)

# Environment Setup

**Difficulty:** 🟢 Beginner  
**Time:** 30 minutes

## 📚 What you'll learn

- Requirements
- Building the compiler
- Running tests
- Running your first program

---

## Requirements

### Mandatory

- **C++20 Compiler**
  - Clang 17+ (Recommended)
  - GCC 12+
  - MSVC 19.30+ (Windows)

- **CMake 3.20+**
  - Build system

- **Git**
  - For source code management

### Optional

- **LLVM 17**
  - Required for native compilation
  - Not required for interpreter-only usage

- **Emscripten**
  - Required for WASM compilation (Future)

- **wasmtime**
  - Required to run WASM binaries

---

## Installation Steps

### macOS

```bash
# Install via Homebrew
brew install cmake llvm@17

# Set Clang path
export PATH="/opt/homebrew/opt/llvm@17/bin:$PATH"
```

### Ubuntu/Debian

```bash
# Essential packages

# LLVM (Optional)
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
```

### Windows

```powershell
# Install via Chocolatey
choco install cmake git llvm

# Or install Visual Studio 2022
```

---

## Building the Compiler

### 1. Clone the Repository

```bash
cd Cm
```

### 2. Build without LLVM (Interpreter only)

```bash
cmake -B build
cmake --build build
```

### 3. Build with LLVM (Recommended)

```bash
cmake -B build -DCM_USE_LLVM=ON
cmake --build build

# Parallel build (Faster)
cmake --build build -j8
```

### 4. Verify Build

```bash
./build/bin/cm --version
# Cm Language Compiler v0.10.0
```

---

## Running Tests

### Run All Tests

```bash
cd build
ctest
```

### Run Specific Tests

```bash
# C++ Unit Tests
ctest -R test_lexer
ctest -R test_parser

# Cm Program Tests
./bin/cm run ../tests/test_programs/basic/hello.cm
```

### LLVM Tests

```bash
# LLVM Backend Tests
make test-llvm-all

# Specific features
make test-llvm-arrays
make test-llvm-pointers
```

---

## Your First Program

### 1. Create a File

```bash
cat > hello.cm << 'EOF'
    println("Hello, Cm Language!");
    return 0;
}
```

### 2. Run with Interpreter

```bash
./build/bin/cm run hello.cm
# Hello, Cm Language!
```

### 3. Compile with LLVM

```bash
# Compile
./build/bin/cm compile hello.cm -o hello

# Run
./hello
# Hello, Cm Language!
```

### 4. Compile to WASM

```bash
# Compile to WASM
./build/bin/cm compile hello.cm --target=wasm -o hello.wasm

# Run with wasmtime
wasmtime hello.wasm
# Hello, Cm Language!
```

---

## Troubleshooting

### Build Errors

#### CMake Not Found

```bash
# Check version
cmake --version

# Install latest version
# macOS
brew install cmake

# Ubuntu
```

#### LLVM Not Found

```bash
# Specify LLVM_DIR
cmake -B build -DCM_USE_LLVM=ON \
  -DLLVM_DIR=/usr/lib/llvm-17/lib/cmake/llvm
```

#### C++20 Compiler Error

```bash
# Use Clang
export CXX=clang++
export CC=clang

# Or GCC
export CXX=g++-12
export CC=gcc-12
```

### Runtime Errors

#### cm Command Not Found

```bash
# Add path
export PATH="$PWD/build/bin:$PATH"

# Or use absolute path
/path/to/Cm/build/bin/cm run hello.cm
```

#### Library Not Found (LLVM)

```bash
# macOS
export DYLD_LIBRARY_PATH="/opt/homebrew/opt/llvm@17/lib:$DYLD_LIBRARY_PATH"

# Linux
export LD_LIBRARY_PATH="/usr/lib/llvm-17/lib:$LD_LIBRARY_PATH"
```

---

## Editor Configuration

### VS Code

`.vscode/settings.json`:

```json
{
  "files.associations": {
    "*.cm": "cpp"
  },
  "editor.tabSize": 4,
  "editor.insertSpaces": true
}
```

### Vim

`.vimrc`:

```vim
au BufRead,BufNewFile *.cm set filetype=cpp
```

---

## FAQ

### Q1: Difference between Interpreter and LLVM?

**Interpreter**
- ✅ Fast build (No LLVM required)
- ✅ Easy debugging
- ❌ Slow execution

**LLVM**
- ✅ Fast execution
- ✅ Optimization
- ❌ Slow compilation

### Q2: How to use WASM?

Compiling to WebAssembly allows you to:
- Run in browsers
- Run in serverless environments
- Run in sandboxed environments

### Q3: Debug Mode?

```bash
# Build with debug info
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Runtime debug
./build/bin/cm run hello.cm --debug
```

---

## Next Steps

✅ Environment setup complete  
✅ Compiler built  
✅ Tests passed  
⏭️ Next, write your first program in [Hello, World!](hello-world.html)

## Related Links

- [Project Structure](../../PROJECT_STRUCTURE.html)
- [Compiler Usage](../compiler/usage.html)

---

**Previous:** [Introduction](introduction.html)  
**Next:** [Hello, World!](hello-world.html)
---

**Last Updated:** 2026-02-08
