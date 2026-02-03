---
title: must Keyword
parent: Advanced
nav_order: 8
---

# must Keyword

The `must` keyword ensures that code is protected from compiler optimizations (dead code elimination).

## Basic Usage

```cm
int main() {
    int x = 0;
    
    // Normal assignment (may be eliminated by optimization)
    x = 100;
    
    // Wrapped in must{} - won't be eliminated
    must { x = 200; }
    
    return 0;
}
```

## Use Cases

### 1. Benchmarks

Protect benchmark code where results aren't used:

```cm
import std::thread::sleep_ms;

int main() {
    int result = 0;
    
    // This loop may be optimized away
    for (int i = 0; i < 1000000; i++) {
        result = result + i;
    }
    
    // Wrapped in must{} - guaranteed to execute
    for (int i = 0; i < 1000000; i++) {
        must { result = result + i; }
    }
    
    return 0;
}
```

### 2. Thread Testing

Protect side-effect-free operations in thread timing tests:

```cm
import std::thread::spawn;
import std::thread::join;
import std::thread::sleep_ms;

void* worker(void* arg) {
    int count = 0;
    
    // Protect busy loop
    for (int i = 0; i < 100; i++) {
        must { count = i; }
        sleep_ms(10);
    }
    
    return count as void*;
}

int main() {
    ulong t = spawn(worker as void*);
    int result = join(t);
    return 0;
}
```

### 3. Security (Memory Clearing)

Prevent optimization from removing sensitive data clearing:

```cm
void clear_password(char* buffer, int size) {
    // May be optimized away
    // for (int i = 0; i < size; i++) { buffer[i] = 0; }
    
    // Guaranteed to clear with must{}
    for (int i = 0; i < size; i++) {
        must { buffer[i] = 0; }
    }
}
```

## Internal Implementation

Code wrapped in `must{}` is marked as `volatile` in LLVM IR:

```llvm
; Normal assignment
store i32 100, ptr %x

; Assignment inside must{}
store volatile i32 200, ptr %x
```

`volatile` tells the compiler "this operation is observable externally and must not be removed."

## Verification

Use `--lir-opt` to check optimized LLVM IR:

```bash
./cm compile --lir-opt src/main.cm
```

## Notes

- `must{}` may have performance implications
- Use only when truly necessary
- Not needed for normal code

## Related Topics

- [Threads](thread.md) - Multi-threaded programming
