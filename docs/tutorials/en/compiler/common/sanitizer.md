---
title: Sanitizers
parent: Compiler
---

[日本語](../../../ja/compiler/common/sanitizer.html)

# Sanitizers (runtime memory checks)

**Goal:** Learn how to detect memory bugs such as out-of-bounds accesses at runtime with the `--sanitize` option.
**Time:** 15 minutes
**Level:** 🟡 Intermediate

---

## Overview

Sanitizers instrument your program with checks at compile time and detect memory bugs at runtime.
Cm uses LLVM's sanitizer infrastructure, the standard in the C/C++/Rust ecosystem; just pass `--sanitize=<kind>` to `cm compile` / `cm run`.

```bash
cm compile --sanitize=bounds -O0 main.cm -o main    # native build with bounds checks
cm compile --sanitize=address -O0 main.cm -o main   # build with AddressSanitizer
cm run --sanitize=bounds -O0 main.cm                # JIT execution with bounds checks
cm compile --target=wasm --sanitize=bounds -O0 main.cm -o main.wasm
```

---

## Supported sanitizers

| Kind | Detects | native | wasm | jit (cm run) |
|------|---------|--------|------|--------------|
| `bounds` | Out-of-bounds access to objects whose size is known at compile time | ○ | ○ | ○ |
| `undefined` | Division/modulo by zero and null pointer dereference (Cm-specific MIR-level checks) | ○ | ○ | ○ |
| `address` | Heap/stack/global out-of-bounds, use-after-free, double free | ○ | × | × |
| `thread` | Data races (ThreadSanitizer) | ○ | × | × |
| `memory` | Reads of uninitialized memory (MemorySanitizer, Linux only) | ○ | × | × |

Multiple kinds are comma separated: `--sanitize=address,bounds`

Project defaults can also be set in `.cmconfig.yml` (the CLI `--sanitize=` takes precedence):

```yaml
compile:
  sanitize: bounds,undefined
```

---

## bounds

`bounds` uses LLVM's `BoundsCheckingPass` (the equivalent of clang's `-fsanitize=local-bounds`) to instrument accesses to memory whose size is statically known, such as fixed-size arrays.
On violation the program stops immediately with a trap instruction (non-zero exit code; e.g. 133 on macOS, 134 under wasmtime).
No runtime library is required, so it works on native, wasm, and the JIT.

```cm
int main() {
    int[4] arr;
    int n = 4;
    for (int i = 0; i <= n; i++) {  // out-of-bounds write at i == 4
        arr[i] = i;
    }
    println("done {arr[0]}");
    return 0;
}
```

```bash
$ cm compile --sanitize=bounds -O0 oob.cm -o oob
$ ./oob
zsh: trap: illegal hardware instruction  ./oob   # stops on the out-of-bounds write

$ cm compile --target=wasm --sanitize=bounds -O0 oob.cm -o oob.wasm
$ wasmtime oob.wasm
Error: wasm trap: wasm `unreachable` instruction executed
```

Only accesses whose object size LLVM can determine statically are checked (partial coverage). Use `address` for heap accesses and accesses across function boundaries.

---

## undefined: Cm-specific runtime checks

`undefined` is a Cm-specific sanitizer that inserts checks at the MIR (mid-level IR) stage.
Because the compiler itself instruments the code rather than an LLVM pass, detection behaves identically on native, wasm, and the JIT, and stops with a descriptive panic.

- **Division/modulo by zero**: integer `/` and `%` whose divisor is zero at runtime (floating point is excluded; IEEE 754 defines it)
- **Null pointer dereference**: reads/writes through a raw pointer (`T*`) that is null

```bash
$ cm run --sanitize=undefined -O0 divzero.cm
panic: runtime error: division by zero

$ cm compile --sanitize=undefined -O0 nullderef.cm -o nd && ./nd
panic: runtime error: null pointer dereference
```

Without the sanitizer the same programs exhibit undefined behavior such as a SEGV or garbage values.

---

## address: AddressSanitizer

`address` instruments memory accesses with LLVM's `AddressSanitizerPass` and links the ASan runtime.
It additionally detects use-after-free and double free, and prints a detailed report on violation.

```bash
$ cm compile --sanitize=address -O0 oob.cm -o oob
$ ./oob
==12345==ERROR: AddressSanitizer: stack-buffer-overflow on address 0x...
```

Because it needs the ASan runtime, it is limited to `cm compile --target=native`.
It is unavailable on wasm (no wasm32-wasi runtime exists) and the JIT (the runtime cannot be loaded into the running cm process). To try it from a single command, use `cm compile --sanitize=address --run main.cm`.
On macOS the runtime is linked from Homebrew LLVM. Old LLVM 17-era runtimes do not work on recent macOS (26.x); install a newer LLVM with `brew install llvm` (discovery prefers the newest automatically).

---

## thread / memory: TSan and MSan

`thread` (ThreadSanitizer) detects data races between threads. `memory` (MemorySanitizer) detects reads of uninitialized memory.
Both are limited to `cm compile --target=native`, and `memory` is Linux-only due to runtime availability.

```bash
cm compile --sanitize=thread -O0 main.cm -o main    # data race detection
cm compile --sanitize=memory -O0 main.cm -o main    # uninitialized reads (Linux only)
```

Known limitation: the Cm runtime (implemented in C) is not instrumented, so `memory` may report false positives for values that originate in the runtime.

---

## Interaction with optimization levels

Sanitizer instrumentation runs **after** optimization, so at higher optimization levels an out-of-bounds access may be optimized away (as undefined behavior) before it can be detected.
For bug hunting, combine sanitizers with `-O0` or `-O1` (the same guidance as for clang's sanitizers).

---

## Error messages

Unsupported combinations and unknown values produce clear errors:

```bash
$ cm compile --sanitize=foo main.cm
error: unknown sanitizer 'foo'
valid sanitizers: address (native only), bounds (native/wasm/jit)

$ cm run --sanitize=address main.cm
error: sanitizer 'address' is not supported on target 'jit'
```

---

<!-- nav -->
← Prev: [MIR Optimization Passes](optimization.html) | [Contents](../index.html) | Next: [Compiler - LLVM Backend](../native/index.html) →
