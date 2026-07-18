---
title: SystemVerilog Backend
parent: Tutorials
nav_order: 11
has_children: false
---

[日本語](../../../ja/compiler/sv/index.html)

# Compiler - SystemVerilog Backend

**Difficulty:** 🟡 Intermediate **Time:** 45 minutes (about 2 hours to read all pages)

Cm can generate SystemVerilog (SV) and run as hardware on FPGAs. Tang Console (Gowin), Xilinx, Intel, and other FPGAs are supported.

---

## Detail Pages

The SV backend documentation is split into topic-specific pages:

| Page | Contents |
|------|----------|
| [Types and Ports](types.html) | Type mapping, port declarations, array ports, literals, localparam, SV attributes |
| [Processes and Assignments](processes.html) | always_ff/comb/latch, automatic assignment conversion, implicit conversions |
| [Control Flow and Loops](control-flow.html) | if/case, while-loop reconstruction, break, operators and precedence guarantees |
| [Data Structures](data.html) | Concatenation/replication, enum FSMs, arrays and BRAM, strings |
| [State Initialization and Simulation](state-sim.html) | Register initial values, initial blocks, automatic testbench generation, running tests |
| [Memory initialization](memory.html) | Array initializers, #[sv::memfile]/$readmemh, --emit-memfile |
| [Module hierarchy](hierarchy.html) | Submodule instantiation via exported IO structs |
| [Board I/O](board-io.html) | Pin constraint emission, tristate, CDC sync (v0.16.0) |
| [Semantic Guarantees](semantics.html) | Summary of guaranteed Cm↔SV semantic correspondence (casts, signed arithmetic, etc.) |

---

## Your First Circuit

```cm
//! platform: sv

#[input]  posedge clk;
#[input]  bool rst = false;
#[output] bool led = false;

uint counter = 0;

void blink(posedge clk) {
    if (rst) {
        counter = 0;
        led = false;
    } else {
        if (counter == 49999999) {
            counter = 0;
            led = !led;
        } else {
            counter = counter + 1;
        }
    }
}
```

Compile:
```bash
cm compile --target=sv blink.cm -o blink.sv
```

Generated SV:
```systemverilog
`timescale 1ns / 1ps

module blink (
    input logic clk,
    input logic rst,
    output logic led
);
    logic [31:0] counter = 32'd0;

    always @(posedge clk) begin
        if (rst) begin
            counter <= 32'd0;
            led <= 1'b0;
        end else begin
            if (counter == 32'd49999999) begin
                counter <= 32'd0;
                led <= ~led;
            end else begin
                counter <= counter + 32'd1;
            end
        end
    end
endmodule
```

> **Key points:** Cm's `=` is automatically converted to SV's `<=` (non-blocking assignment).
> `!led` is also converted to SV's `~led` (bitwise negation).
> A variable's declared initial value (`uint counter = 0;`) is emitted as its power-on initial value.

### State-variable assignments take effect next cycle (important)

Assignments to module-level state variables inside a posedge function are converted to SV non-blocking assignments (`<=`). This means **every read inside the function sees the previous cycle's value, and the assignment takes effect at the start of the next cycle** — unlike sequential software execution:

```cm
void count(posedge clk) {
    btn_prev = btn_state;             // "scheduled" for the next cycle
    if (btn_prev && !btn_state) {     // reads the PREVIOUS btn_prev
        presses = presses + 1;        // → works as edge detection
    }
}
```

Under sequential semantics the condition above would always be false, but in hardware it works as intended. Conversely, code that expects to use the newly assigned value within the same cycle produces a different circuit than intended — use **local variables** (immediate assignment) for same-cycle values. Compile with `--sv-warn-nba` to list every read of an already-assigned state variable.

When a state variable is assigned multiple times, the last executed assignment wins (same rule as SV non-blocking assignment).

### Generated-code inspection options (v0.16.0)

| Option | Effect |
|---|---|
| `--sv-warn-nba` | Warn on reads of already-assigned state variables (previous-cycle values) |
| `--sv-strict-lint` | Do not emit `lint_off` suppressions; expose Verilator width warnings |
| `--sv-always-ff` | Keep `always_ff`/`always_comb` (default converts to Gowin-compatible `always @`) |

### Top module name (v0.16.0)

A `module NAME;` declaration determines the SV top module name. Without it, the name is inferred from the source file name:

```cm
//! platform: sv
module hdmi_colorbar;  // → module hdmi_colorbar ( ... );
```

Variable and function names that collide with SystemVerilog reserved words (`program`, `priority`, `bind`, etc.) are rejected with a compile error, since they would produce invalid SV. Rename the identifier.

---

## Platform Directive

To use the SV backend, this directive is **required** at the top of the file:

```cm
//! platform: sv
```

It enables:
- SV-specific keywords (`posedge`, `negedge`, `wire`, `reg`, `always`, `assign`)
- Validation of non-synthesizable types (pointers → compile error)
- Implicit SV conversions (assignment style, literal bit-width annotation, etc.)

---

## SV target compile options

| Option | Description |
|---|---|
| `--emit-memfile` | Write array literal initializers out as `.hex` files |
| `--sv-strict-lint` | Do not emit lint_off suppressions (surface width warnings) |

## Compilation and Verification

```bash
# Generate SV code
cm compile --target=sv blink.cm -o blink.sv

# Syntax check with Verilator
verilator --sv --lint-only blink.sv

# Simulate with Icarus Verilog
iverilog -g2012 -o sim blink.sv blink_tb.sv
vvp sim

# FPGA build (Gowin EDA)
gw_sh gowin_build.tcl
```

### Target FPGAs

| Board | Chip | Tool |
|-------|------|------|
| Tang Console 138K | Gowin GW5AST | Gowin EDA |
| Tang Nano 9K | Gowin GW1NR-9 | Gowin EDA |
| Arty A7 | Xilinx Artix-7 | Vivado |
| DE10-Lite | Intel MAX 10 | Quartus |

---

## Complete Example

```cm
//! platform: sv

#[input]  posedge clk;
#[input]  negedge rst_n;
#[output] bool led = false;

const uint CLK_FREQ = 27000000;
const uint CNT_MAX = CLK_FREQ / 2 - 1;

uint counter = 0;

always void blink(posedge clk, negedge rst_n) {
    if (rst_n == false) {
        counter = 0;
        led = false;
    } else {
        if (counter == CNT_MAX) {
            counter = 0;
            led = !led;
        } else {
            counter = counter + 1;
        }
    }
}
```

---

## Token Reference

### SV-Specific Tokens

| Token | Keyword | Purpose |
|-------|---------|---------|
| `KwPosedge` | `posedge` | Rising edge |
| `KwNegedge` | `negedge` | Falling edge |
| `KwWire` | `wire` | Wire-qualified type |
| `KwReg` | `reg` | Register-qualified type |
| `KwAlways` | `always` | Logic block modifier (auto-detected) |
| `KwAlwaysFF` | `always_ff` | Sequential logic (explicit) |
| `KwAlwaysComb` | `always_comb` | Combinational logic (explicit) |
| `KwAlwaysLatch` | `always_latch` | Latch (explicit) |
| `KwAssign` | `assign` | Continuous assignment |
| `KwInitial` | `initial` | Simulation initialization block |
| `KwBit` | `bit` | Arbitrary-width type `bit[N]` |

### SV Meaning of Existing Tokens

| Token | Normal (LLVM) meaning | SV meaning |
|-------|----------------------|------------|
| `async` | JS async function | `always_ff` (backward compat) |
| `func` | Function declaration | `always_comb` |
| `void` | Function with no return value | Block generation |
| `=` | Variable assignment | ff: `<=`, comb: `=` |
| `!` | Logical negation | `~` (unified with bitwise negation) |
| `const` | Constant declaration | `localparam` |
| `switch/case` | Pattern matching | `case/endcase` |
| `enum` | Enumeration | `typedef enum logic` |

---

**Previous:** [WASM Backend](../wasm/index.html)

---

**Last Updated:** 2026-07-04

---

<!-- nav -->← Prev: [Compiling Cm to JavaScript](../js/index.html) | [Contents](../index.html) | Next: [SV Backend - Types and Ports](types.html) →
