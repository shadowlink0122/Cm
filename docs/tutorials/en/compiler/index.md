---
title: Compiler
parent: Tutorials
has_children: true
nav_order: 5
---

[日本語](../../ja/compiler/)

# Compiler

Tutorials for using the Cm compiler and its backends. Estimated time: 3 hours.

Pages are organized by backend:
`common/` (shared tools), `native/` (LLVM native / UEFI), `wasm/`, `js/`,
and `sv/` (SystemVerilog / FPGA).

---

## Common (common/)

| Title | Level | Contents |
|-------|-------|----------|
| [Using the compiler](common/usage.html) | 🟢 Beginner | Commands and options |
| [Preprocessor](common/preprocessor.html) | 🟡 Intermediate | Conditional compilation |
| [Linter](common/linter.html) | 🟢 Beginner | Static analysis (cm lint) |
| [Formatter](common/formatter.html) | 🟢 Beginner | Code formatting (cm fmt) |
| [Optimization](common/optimization.html) | 🔴 Advanced | O0-O3, --funroll-loops, tail calls |

## Native (native/)

| Title | Level | Contents |
|-------|-------|----------|
| [LLVM backend](native/index.html) | 🟡 Intermediate | Native compilation |
| [UEFI bare-metal](native/uefi.html) | 🔴 Advanced | UEFI application development (no_std) |

## WebAssembly (wasm/)

| Title | Level | Contents |
|-------|-------|----------|
| [WASM backend](wasm/index.html) | 🟡 Intermediate | WebAssembly output |

## JavaScript (js/)

| Title | Level | Contents |
|-------|-------|----------|
| [JS backend](js/index.html) | 🟡 Intermediate | JavaScript output |

## SystemVerilog / FPGA (sv/)

| Title | Level | Contents |
|-------|-------|----------|
| [SV backend overview](sv/index.html) | 🟡 Intermediate | SV generation for FPGAs (overview, compile options) |
| [Types and ports](sv/types.html) | 🟡 Intermediate | Type mapping, ports, literals |
| [Processes and assignments](sv/processes.html) | 🟡 Intermediate | always_ff/comb, implicit conversions |
| [Control flow and loops](sv/control-flow.html) | 🟡 Intermediate | if/case, loop reconstruction, constant loop unrolling |
| [Data structures](sv/data.html) | 🟡 Intermediate | Concatenation, enum FSMs, arrays, strings |
| [Memory initialization](sv/memory.html) | 🟡 Intermediate | Array initializers, $readmemh, --emit-memfile |
| [Module hierarchy](sv/hierarchy.html) | 🟡 Intermediate | Submodules via //! sv: hierarchy |
| [Board I/O](sv/board-io.html) | 🟡 Intermediate | #[sv::pin], --emit-constraints, tristate, CDC sync |
| [State init and simulation](sv/state-sim.html) | 🟡 Intermediate | Initial values, initial blocks, testbenches, assertions |
| [Semantics guarantees](sv/semantics.html) | 🟡 Intermediate | Guaranteed Cm↔SV semantic correspondences |

---

<!-- nav -->
← Prev: [Macros](../advanced/macros.html) | [Contents](../index.html) | Next: [Compiler - Usage](common/usage.html) →
