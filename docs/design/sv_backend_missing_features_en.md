# SV Backend: Missing Features and Implementation Proposals

Created: 2026-07-04
Target version: v0.15.1
Language: English ([日本語](sv_backend_missing_features.html))

This document summarizes the features missing from the sv backend, together with implementation proposals, based on real-world use in CmCPU (CPU development targeting the Tang Console 138K) and the investigation on 2026-07-04. Items are ordered by "how much CPU development needs them".

For the fixes already implemented (precedence parentheses, register initial values, casts, arithmetic shift, enum widths, array ports, signed constants, while-loop reconstruction, initial blocks), see `docs/releases/v0.15.1.md` and `docs/archive/013_refactoring_sv_backend_and_cpp.md`.

---

## 1. Preserving Module Hierarchy (Most Important)

**Current state**: One compilation = one module. `import` flattens all symbols and expands them into a single module. Only `extern struct` (external primitives) can be instantiated. CmCPU's `hdmi_text_top` expands into a single module of roughly 6,300 lines, and even if the CPU is split into an ALU, decoder, and register file, the hierarchy is lost in the synthesis result.

**Proposal**:
1. Generate an `SVModule` per unit equivalent to `module X { ... }` (currently a file) and hold multiple modules in the `modules_` vector (the data structure already supports this)
2. Introduce instantiation syntax for Cm modules. The same generation path (named port connection) used for existing `extern struct` instantiation can be reused:
   ```cm
   import ./alu;           // use the alu module while keeping the hierarchy
   Alu alu_inst = Alu { .a = op_a, .b = op_b, .result = alu_out };
   ```
3. Make "flattening" vs "hierarchy" selectable at import resolution time (default to flattening for backward compatibility, switch to hierarchy with a `//! sv: hierarchy` directive)

**Difficulty**: High (requires removing analyzeMIR's single `default_mod` assumption)

## 2. Module Parameters (`module #(parameter ...)`)

**Current state**: `const` maps only to `localparam`. The generated module itself cannot be parameterized. `#[sv::param]` was declared deprecated in v0.15.1 but still remains in the code and tests, so the spec and implementation disagree.

**Proposal**: Either officially revive the optional attribute `#[sv::param]` that emits `export const` as a `parameter` (overridable), or remove it completely and standardize on `localparam`. Together with hierarchy support (proposal 1), generate `#(.WIDTH(8))` parameter overrides.

**Difficulty**: Medium (depends on proposal 1)

## 3. Memory Initialization (`$readmemh` / Array Initial Values)

**Current state**: Initial values for arrays (BRAM/LutRAM) are not emitted. Instruction ROMs and font ROMs must be written as huge lookup functions (case statements); CmCPU's `font_rom.cm` is a 2,174-line case statement.

**Proposal**:
1. Emit array literal initial values as `initial begin mem[0] = ...; end` (for small arrays)
2. A `#[sv::memfile("font.hex")]` attribute that emits `initial $readmemh("font.hex", mem);` (for large arrays)
3. An `--emit-memfile` option that writes the contents of const arrays to `.hex` files at compile time

**Difficulty**: Low to medium (one attribute plus initial-block emission)

## 4. Removing the Fixed 24-bit Width of `string` at Function Boundaries

**Current state**: `mapType`/`getBitWidth` fix `String → logic [23:0]` (3 characters). Const globals are emitted with their actual length, but function arguments, return values, and non-const variables are truncated beyond 3 characters.

**Proposal**:
1. Give the type system string lengths (`string<N>`), or reuse the HIR type's `array_size` and determine the length via constant propagation
2. Make string use at function boundaries a **compile error** when the length cannot be determined (currently it breaks silently, so even just turning it into an error is valuable)

**Difficulty**: Medium (extending frontend type information)

## 5. `while (true)` + `break` Style Loops (Unconditional Headers)

**Current state**: While-loop reconstruction only supports natural loops whose header contains a conditional branch. Loops with an unconditional header, such as `while (true)`, still produce incorrect output.

**Proposal**: When the header's terminator is a `Goto`, emit `while (1) begin ... end` and convert branches to the exit inside the loop into `break;` (the existing `loop_exit_stack_` mechanism can be used as-is).

**Difficulty**: Low (extension of an existing mechanism)

## 6. generate / genvar Equivalent Syntax

**Current state**: Repetitive generation of hardware structures (e.g. module instances for N bits) requires manual unrolling.

**Proposal**: Unroll `for` loops over const ranges at compile time (Cm already has compile-time constant folding, so static expansion at the MIR level is natural). Rather than emitting SV `generate`, expanding on the Cm side and then going through the normal emission path is simpler to implement.

**Difficulty**: Medium

## 7. Assertions (SVA)

**Current state**: None. Property verification is valuable for CPU verification.

**Proposal**: Emit `assert(expr);` as `assert property (@(posedge clk) expr);` or as an immediate assertion `assert (expr) else $error(...);`. Combine with simulation verification via `//! test:`.

**Difficulty**: Low (if immediate assertions only)

## 8. Tri-State (`'z`) and CDC Synchronization Primitives

**Current state**: Only the `inout` port direction is supported. Z-value literals and high-impedance control are not possible. Synchronization across clock domains (2FF synchronizer) must be hand-written.

**Proposal**:
- Allow Z-value literals of the form `4'bz` as an extension of SV-style literals
- A `#[sv::sync(2)]` attribute that automatically inserts a 2FF synchronization stage on signals crossing clock domains

**Difficulty**: Medium

## 9. Gradual Removal of lint_off

**Current state**: The generated SV blanket-disables `WIDTHTRUNC` / `WIDTHEXPAND` / `UNDRIVEN` / `UNUSED` via lint_off, hiding width-mismatch defects from itself. Many of the bugs uncovered in this investigation (ignored casts, signed constants, etc.) could have been detected early without lint_off.

**Proposal**: Since explicit cast emission (already implemented) makes suppressing WIDTHTRUNC/WIDTHEXPAND unnecessary in principle, remove the lint_off entries one by one in the test suite and fix the warnings. Eventually make `--sv-strict-lint` the default.

**Difficulty**: Low to medium (steady warning cleanup)

## 10. Unit Tests for sv codegen

**Current state**: There are no unit tests for the sv backend in `tests/unit/`. The string post-processing (temporary inline expansion, ternary conversion, always-kind inference) is text-based and fragile, but the only tests are integration tests (.cm→.sv→lint/sim).

**Proposal**: Add golden tests from MIR fragments to SV strings as `tests/unit/sv_codegen_test.cpp` (priorities: operator precedence, casts, loop structuring, signed constants). Define the target by reusing CMake's per-component source lists (`CM_MIR_SOURCES`, etc.).

**Difficulty**: Low

## 11. Migrating to Expression-Tree-Based SV Emission (Long Term)

**Current state**: The sv backend is designed to "emit SV text first, then fix it up with string manipulation" (temporary inline expansion, else-if normalization, ternary conversion, and always-kind inference are all string `find`/`replace`). The precedence-parentheses bug was an inevitable consequence of this design.

**Proposal**: Build an expression tree (or a small SV-specific AST) from MIR and emit everything through a precedence-aware pretty printer. Replace latch inference — currently a text heuristic that "counts `if (` occurrences per line" — with a MIR assignment-completeness analysis (whether every signal is assigned on every path).

**Difficulty**: High (a backend redesign, but it becomes the foundation for safely building up implementations 1-10)

---

## Appendix: Known Bugs in the Language Core (2026-07-04 Investigation)

Outside the sv backend, the same day's investigation confirmed the following serious bugs (see the investigation records for detailed reproduction code). These need to be addressed separately:

1. **Simple assignments to global variables from inside functions are ignored** (`g = 999` becomes a local shadow; `g += 5` works correctly)
2. **Stored results of operations are not truncated to the type width, so displayed values and compared values disagree** (`int w = INT_MAX + 1` prints `-2147483648` yet `w < 0` is false)
3. **Broken unsigned semantics** (`uint` comparison, division, and shifts execute as signed; `utiny 255 as int` becomes -1, etc.)
4. **Expressions inside string interpolation such as `{a + b}` output garbage values** (values also differ between JIT and native)
5. **`++`/`--` on array elements and struct fields has no effect**
6. **Integer division by zero does not trap and yields garbage values** (differs between JIT and native)
7. **Inconsistent argument-passing convention**: structs are passed to functions by reference while arrays are passed by value (the design decision needs to be documented)
