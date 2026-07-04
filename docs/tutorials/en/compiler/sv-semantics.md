---
title: SV Backend - Semantic Guarantees
parent: Tutorials
nav_order: 17
---

[日本語](../../ja/compiler/sv-semantics.html)

# SV Backend - Semantic Guarantees

Cm's SV backend is designed to guarantee that "logic written in Cm behaves with the same meaning in the generated SystemVerilog". This page explains the semantic correspondences strengthened in v0.15.1 (updated 2026-07-04) and the conversion rules you should know.

For basic usage, see the [SystemVerilog Backend](sv.html).

---

## 1. Operator Precedence Follows the Cm Source Structure

In SystemVerilog, `==` binds tighter than `&`, so `a & 256 == 0` without parentheses is
interpreted as `a & (256 == 0)`. The Cm compiler preserves the expression structure and always emits the necessary parentheses.

```cm
if ((r_qm & 256) == 0) { ... }
```

```systemverilog
// Generated SV: parentheses are preserved
if (((r_qm & 32'd256) == 32'd0)) begin ... end
```

Evaluation order matches exactly what you wrote, so even bit-manipulation-heavy logic such as a TMDS encoder can be written with confidence.

## 2. Signed Arithmetic Has the Same Semantics as Cm / LLVM

### Arithmetic Right Shift

Cm's `>>` is an arithmetic shift for signed types (same as the LLVM backend's `ashr`).
SV's `>>` is always a logical shift, so `>>>` is emitted for signed operands.

```cm
int s = -8;
int r = s >> 2;   // -2 (arithmetic shift)
```

```systemverilog
shifted <= s >>> 32'sd2;  // arithmetic shift
```

### Signed Constants

In SV, if one side of a comparison is unsigned, the **entire comparison becomes unsigned**.
Cm emits constants according to their type, so negative checks like `s < 0` work correctly.

```cm
if (s < 0) { neg = 1; }   // int s
```

```systemverilog
if ((s < 32'sd0)) begin ... end  // 'sd = signed decimal
```

## 3. `as` Casts Are Emitted as Size Casts

A narrowing cast in the middle of an expression is emitted explicitly as an SV size cast `N'(expr)`.
When the sign changes, `$signed()` / `$unsigned()` is used as well.

```cm
wide = ((a + 300) as utiny) + 1000;  // truncate to 8 bits, then add
```

```systemverilog
wide <= 8'((a + 32'd300)) + 32'd1000;  // if a=0, then 44 + 1000 = 1044
```

## 4. Variable Initial Values Become Power-On Initial Values

The declared initial values of module-level variables are emitted as SV register declaration initial values.
FPGA synthesis treats them as initial values, and in simulation they prevent X propagation.

```cm
uint state = 0;
uint counter = 42;
```

```systemverilog
logic [31:0] state = 32'd0;
logic [31:0] counter = 32'd42;
```

As a result, the generated SV can be **simulated as-is** with iverilog / Verilator.

## 5. Enum Widths Are Computed from Explicit Tag Values

```cm
enum Status {
    IDLE = 0,
    ERROR = 100
}
```

```systemverilog
typedef enum logic [6:0] {  // 7 bits, wide enough to represent 100
    IDLE = 7'd0,
    ERROR = 7'd100
} Status;
```

## 6. Array-Typed Ports Preserve Unpacked Dimensions

```cm
#[output] uint[4] data;
```

```systemverilog
output logic [31:0] data [0:3]  // dimensions are preserved
```

---

## Conversion Rule Quick Reference

| Cm | Generated SV | Notes |
|----|--------------|-------|
| `bool` | `logic` | |
| `int` / `uint` | `logic signed [31:0]` / `logic [31:0]` | tiny/short/long map to the corresponding widths |
| `s >> n` (signed) | `s >>> n` | Arithmetic shift |
| `x as utiny` (in expression) | `8'(x)` | Size cast |
| Sign changes like `int as uint` | `$unsigned(...)` / `$signed(...)` | |
| Signed constants | `32'sd5` etc. | Prevents unsigned comparison |
| `uint x = 42;` | `logic [31:0] x = 32'd42;` | Power-on initial value |
| `uint[N]` port | `logic [31:0] name [0:N-1]` | |
| `async void f(posedge clk)` | `always @(posedge clk)` | |
| `string` constant + index | packed vector + part select | `TITLE[(L-1-i)*8 +: 8]` |

## Guaranteed by Tests

These semantics are continuously verified by simulation-backed regression tests in `tests/sv/`
(value verification with iverilog + vvp):

- `basic/precedence_mask` — precedence parentheses preservation
- `basic/cast_truncate` — narrowing casts in expressions
- `control/signed_shift` / `control/signed_const_cmp` — signed shift and comparison
- `control/for_loop` / `control/loop_break` / `control/nested_loop` — while-loop reconstruction, break, nesting
- `advanced/enum_explicit` / `advanced/reg_init` — explicit enum values and initial values
- `memory/array_port` — array ports

---

← [State Initialization and Simulation](sv-state-sim.html) | [Back to Overview](sv.html) →
