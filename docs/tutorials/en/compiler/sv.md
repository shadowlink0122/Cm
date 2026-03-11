---
title: SystemVerilog Backend
parent: Tutorials
nav_order: 11
---

[日本語](../../ja/compiler/sv.html)

# Compiler - SystemVerilog Backend

**Difficulty:** 🟡 Intermediate  
**Time:** 45 min

Cm can generate synthesizable SystemVerilog (SV) code for FPGAs. Compatible with Tang Console (Gowin), Xilinx, Intel, and more.

---

## Table of Contents

1. [Your First Circuit](#your-first-circuit)
2. [Platform Directive](#platform-directive)
3. [Type System](#type-system)
4. [Port Declarations](#port-declarations)
5. [Logic Blocks](#logic-blocks)
6. [Operators](#operators)
7. [Literals and Bit Widths](#literals-and-bit-widths)
8. [Constants and localparam](#constants-and-localparam)
9. [Control Flow](#control-flow)
10. [Concatenation and Replication](#concatenation-and-replication)
11. [Enums (FSM)](#enums-fsm)
12. [SV Attributes](#sv-attributes)
13. [Implicit Conversions](#implicit-conversions)
14. [Compilation and Verification](#compilation-and-verification)
15. [Complete Example](#complete-example)
16. [Token Reference](#token-reference)

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
    logic [31:0] counter;

    always_ff @(posedge clk) begin
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

> **Key Points:** Cm's `=` is automatically converted to SV's `<=` (non-blocking assignment),
> and `!led` is converted to `~led` (bitwise inversion).

---

## Platform Directive

Every Cm file targeting SV **must** start with:

```cm
//! platform: sv
```

This enables:
- SV-specific keywords (`posedge`, `negedge`, `wire`, `reg`, `always`, `assign`)
- Non-synthesizable type validation (`float`, `string`, pointers → compile error)
- Implicit SV transformations (assignment style, literal bit widths, etc.)

---

## Type System

### Basic Types

| Cm Type | SV Output | Bits | Usage |
|---------|-----------|------|-------|
| `bool` | `logic` | 1 | Flags, control signals |
| `utiny` | `logic [7:0]` | 8 | Small counters, state |
| `ushort` | `logic [15:0]` | 16 | Addresses |
| `uint` | `logic [31:0]` | 32 | Counters, data |
| `ulong` | `logic [63:0]` | 64 | Timestamps |
| `tiny` | `logic signed [7:0]` | 8 | Signed small values |
| `short` | `logic signed [15:0]` | 16 | Signed medium values |
| `int` | `logic signed [31:0]` | 32 | Signed data |
| `long` | `logic signed [63:0]` | 64 | Signed large values |

### SV-Specific Types

| Cm Type | Purpose | SV Output |
|---------|---------|-----------|
| `posedge` | Rising edge signal | `logic` (1-bit) |
| `negedge` | Falling edge signal | `logic` (1-bit) |
| `wire<T>` | Wire qualifier | `T` mapping |
| `reg<T>` | Register qualifier | `T` mapping |

### Custom Bit Widths

```cm
#[output] bit[4] nibble;      // → output logic [3:0] nibble
#[output] bit[12] address;    // → output logic [11:0] address
bit[26] counter;              // → logic [25:0] counter
```

### Non-Synthesizable Types (Compile Error)

`float`, `double`, `string`, `cstring`, `*T` (pointers), `&T` (references) are **rejected** by the SV backend.

---

## Port Declarations

```cm
// Input ports
#[input]  posedge clk;              // → input logic clk
#[input]  bool rst = false;         // → input logic rst
#[input]  utiny data_in;            // → input logic [7:0] data_in

// Output ports
#[output] bool led = false;         // → output logic led
#[output] utiny led_array = 0xFF;   // → output logic [7:0] led_array

// Bidirectional ports
#[inout]  ushort bus;               // → inout logic [15:0] bus

// Parameters (overridable)
#[sv::param] uint WIDTH = 8;        // → parameter WIDTH = 32'd8;
```

---

## Logic Blocks

### Sequential Logic (always_ff)

#### Pattern A: `always` + Edge Parameter (Recommended)

```cm
always void counter_tick(posedge clk) {
    count = count + 1;
}
// → always_ff @(posedge clk) begin
//        count <= count + 32'd1;
//    end
```

#### Pattern B: Async Reset (Multiple Edges)

```cm
always void process(posedge clk, negedge rst_n) {
    if (rst_n == false) {
        count = 0;
    } else {
        count = count + 1;
    }
}
// → always_ff @(posedge clk or negedge rst_n) begin ...
```

#### Pattern C: `void f(posedge clk)` (Legacy)

```cm
void blink(posedge clk) {
    led = !led;
}
// → always_ff @(posedge clk) begin led <= ~led; end
```

#### Pattern D: `async func` (Legacy)

```cm
async func tick() {
    counter = counter + 1;
}
// → always_ff @(posedge clk) begin counter <= counter + 32'd1; end
```

> **Note:** `async func` implicitly references the `clk` variable.
> If `clk` is undeclared, `input logic clk` is automatically added.

### Combinational Logic (always_comb)

Functions without edge parameters:

```cm
always void decode() {
    out = 0;
    if (sel) { out = a; }
    else { out = b; }
}
// → always_comb begin ... end
```

Legacy: `void f()` / `func f()` also map to `always_comb`.

### Assignment Rules

| Block Type | Cm Source | SV Output |
|-----------|----------|-----------|
| `always_ff` (sequential) | `x = expr;` | `x <= expr;` (non-blocking) |
| `always_comb` (combinational) | `x = expr;` | `x = expr;` (blocking) |

Always write `=` in Cm — the compiler chooses the correct assignment style.

---

## Operators

### Arithmetic & Bitwise

| Cm | SV | Notes |
|----|----|-------|
| `+` `-` `*` `/` `%` | Same | Arithmetic |
| `&` `\|` `^` `~` | Same | Bitwise |
| `<<` `>>` | Same | Shift |
| `==` `!=` `<` `<=` `>` `>=` | Same | Comparison |
| `&&` `\|\|` | Same | Logical |
| `!x` | `~x` | **Implicit conversion**: logical NOT → bitwise NOT |

> **Important:** Cm's `!` (logical NOT) maps to SV's `~` (bitwise NOT) for multi-bit safety.

---

## Literals and Bit Widths

Literals are **automatically given bit widths** based on context:

| Cm Literal | Context Type | SV Output |
|-----------|-------------|-----------|
| `true` | `bool` | `1'b1` |
| `false` | `bool` | `1'b0` |
| `42` | `uint` (32-bit) | `32'd42` |
| `42` | `utiny` (8-bit) | `8'd42` |
| `-5` | `int` (signed 32-bit) | `-32'sd5` |

### SV-Style Literals

```cm
utiny mask = 8'b10101010;     // → 8'b10101010
ushort addr = 16'hFF00;       // → 16'hFF00
```

### Numeric Separators

```cm
const uint CLK_FREQ = 50_000_000;   // → localparam CLK_FREQ = 32'd50000000;
```

---

## Constants and localparam

### `const` → `localparam`

```cm
const uint CLK_FREQ = 27_000_000;
const uint CNT_MAX = CLK_FREQ / 2 - 1;
```
```systemverilog
localparam CLK_FREQ = 32'd27000000;
localparam CNT_MAX = CLK_FREQ / 2 - 32'd1;
```

### `#[sv::param]` + non-`const` → `parameter`

```cm
#[sv::param] uint WIDTH = 8;
// → parameter WIDTH = 32'd8;
```

> **Note:** `const` always maps to `localparam` regardless of attributes.
> To get an overridable `parameter`, use `#[sv::param]` **without** `const`.

---

## Control Flow

### if / else if / else

```cm
if (rst) {
    counter = 0;
} else if (enable) {
    counter = counter + 1;
} else {
    // idle
}
```
```systemverilog
if (rst) begin
    counter <= 32'd0;
end else if (enable) begin
    counter <= counter + 32'd1;
end else begin
end
```

### switch → case

```cm
switch (state) {
    case 0: { next_state = 1; }
    case 1: { next_state = 2; }
    default: { next_state = 0; }
}
```
```systemverilog
case (state)
    32'd0: begin next_state <= 32'd1; end
    32'd1: begin next_state <= 32'd2; end
    default: begin next_state <= 32'd0; end
endcase
```

### Functions and Tasks

Functions with arguments (no edge params, no `always`/`async`) are automatically mapped based on return type:

```cm
// Non-void → SV function
uint max_val(uint x, uint y) {
    if (x > y) { return x; }
    return y;
}
// → function automatic logic [31:0] max_val(...); ... endfunction

// Void with args → SV task
void send_byte(utiny data) {
    tx_valid = true;
    tx_data = data;
}
// → task automatic send_byte(...); ... endtask
```

> **Note:** No `#[sv::function]` / `#[sv::task]` attributes needed — the compiler
> determines the mapping from the return type. Argument-less `void f()` still maps
> to `always_comb` for backward compatibility.

```cm
result = {a, b};         // → {a, b}
replicated = {3{a}};     // → {3{a}}
```

Built-in functions (when `{...}` is ambiguous with blocks):

```cm
result = concat(a, b);       // → {a, b}
wide = replicate(nibble, 3); // → {3{nibble}}
```

---

## Enums (FSM)

Cm `enum` maps to SV `typedef enum logic`. Bit width is auto-calculated:

```cm
enum State { IDLE, RUN, DONE, ERROR }
```
```systemverilog
typedef enum logic [1:0] {
    IDLE = 2'd0, RUN = 2'd1, DONE = 2'd2, ERROR = 2'd3
} State;
```

---

## SV Attributes

| Attribute | Effect | Example |
|-----------|--------|---------|
| `#[input]` | Input port | `#[input] posedge clk;` |
| `#[output]` | Output port | `#[output] utiny led = 0xFF;` |
| `#[inout]` | Bidirectional port | `#[inout] ushort bus;` |
| `#[sv::param]` | `parameter` declaration | `#[sv::param] uint WIDTH = 8;` |
| `#[sv::bram]` | `(* ram_style = "block" *)` | `#[sv::bram] utiny mem[1024];` |
| `#[sv::lutram]` | `(* ram_style = "distributed" *)` | `#[sv::lutram] utiny lut[16];` |
| `#[sv::clock_domain("name")]` | Clock for `async func` | `#[sv::clock_domain("fast")]` |
| `#[sv::pipeline]` | Pipeline hint | |
| `#[sv::share]` | Resource sharing hint | |
| `#[sv::pin("XX")]` | Pin assignment (XDC/CST) | `#[sv::pin("H11")]` |
| `#[sv::iostandard("YY")]` | IO standard | `#[sv::iostandard("LVCMOS33")]` |

---

## Implicit Conversions

The SV backend performs many automatic conversions so you can write natural Cm code:

### Assignment Style

| Context | Cm | SV |
|---------|----|----|
| `always_ff` | `x = expr;` | `x <= expr;` |
| `always_comb` | `x = expr;` | `x = expr;` |

### Logical NOT → Bitwise NOT

| Cm | SV | Reason |
|----|----|----|
| `!flag` | `~flag` | Unified to `~` for multi-bit safety |

### Literal Bit Width Inference

| Cm | Target Type | SV |
|----|------------|-----|
| `counter = 0;` | `uint` | `counter <= 32'd0;` |
| `flag = true;` | `bool` | `flag <= 1'b1;` |

### Auto Port Addition

| Condition | Action |
|-----------|--------|
| `async func` exists & `clk` undeclared | `input logic clk` auto-added |
| `async func` exists & `rst` undeclared | `input logic rst` auto-added |

### MIR Temporary Inlining

MIR temporaries (`_tXXXX`) are inlined back into expressions:

```
MIR:  _t1000 = counter + 1; result = _t1000;
SV:   result <= counter + 32'd1;
```

### `self.` Prefix Removal

`self.counter` → `counter` (SV has no `self`)

### `else if` Normalization

Nested `else { if ... }` patterns are flattened to `else if`.

### Redundant Ternary Pruning

`cond ? x : x` is simplified to `x`.

---

## Compilation and Verification

```bash
# Generate SV
cm compile --target=sv blink.cm -o blink.sv

# Lint-only check with Verilator
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

const uint CLK_FREQ = 27_000_000;
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
| `KwWire` | `wire` | Wire qualifier |
| `KwReg` | `reg` | Register qualifier |
| `KwAlways` | `always` | Logic block modifier |
| `KwAssign` | `assign` | Continuous assignment |
| `KwInitial` | `initial` | Simulation initialization |
| `KwBit` | `bit` | Custom bit-width type |

### Existing Tokens with SV Meaning

| Token | Normal (LLVM) | SV Meaning |
|-------|--------------|------------|
| `async` | JS async function | `always_ff` (legacy) |
| `func` | Function declaration | `always_comb` |
| `void` | No return value | Block generation |
| `=` | Variable assignment | ff: `<=`, comb: `=` |
| `!` | Logical NOT | `~` (bitwise NOT) |
| `const` | Constant | `localparam` |
| `switch/case` | Pattern match | `case/endcase` |
| `enum` | Enumeration | `typedef enum logic` |

---

**Previous:** [WASM Backend](wasm.html)  
**Next:** [Formatter](formatter.html)

---

**Last updated:** 2026-03-11
