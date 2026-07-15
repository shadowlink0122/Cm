---
title: SV Backend - Types and Ports
parent: Tutorials
nav_order: 12
---

[日本語](../../../ja/compiler/sv/types.html)

# SV Backend - Types and Ports

This is a detail page of the [SystemVerilog Backend](index.html). It covers type mapping, port declarations, literals, constants, and SV attributes.

---

## Type System

### Basic Types

| Cm type | SV output | Bit width | Purpose |
|---------|-----------|-----------|---------|
| `bool` | `logic` | 1 | Flags, control signals |
| `utiny` | `logic [7:0]` | 8 | Small counters, states |
| `ushort` | `logic [15:0]` | 16 | Addresses |
| `uint` | `logic [31:0]` | 32 | Counters, data |
| `ulong` | `logic [63:0]` | 64 | Timestamps |
| `tiny` | `logic signed [7:0]` | 8 | Small signed values |
| `short` | `logic signed [15:0]` | 16 | Medium signed values |
| `int` | `logic signed [31:0]` | 32 | Signed data |
| `long` | `logic signed [63:0]` | 64 | Large signed data |

### SV-Specific Types

| Cm type | Purpose | SV output |
|---------|---------|-----------|
| `posedge` | Clock rising-edge signal | `logic` (1-bit) |
| `negedge` | Clock/reset falling-edge signal | `logic` (1-bit) |
| `wire<T>` | Wire qualifier (combinational output) | Follows the mapping of `T` |
| `reg<T>` | Register qualifier (sequential output) | Follows the mapping of `T` |

### Custom Bit Widths

```cm
#[output] bit[4] nibble;      // → output logic [3:0] nibble
#[output] bit[12] address;    // → output logic [11:0] address
bit[26] counter;              // → logic [25:0] counter
```

### Non-Synthesizable Types (Compile Errors)

Pointer types (`*T`) cause a **compile error** (`error[SV002]`) in the SV backend. `float`/`double` also cause a **compile error** (`error[SV004]`, changed from a warning in v0.16.0; an IP core is required for synthesis). `string` is only practical as a const constant (non-const strings longer than 3 characters raise `error[SV005]`; see [Data Structures](data.html#strings)).

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
```

### Array-Typed Ports

Array-typed ports are emitted with their unpacked dimensions preserved:

```cm
#[output] uint[4] data;   // → output logic [31:0] data [0:3]
```

> **Note:** In older versions the dimensions were not preserved, so `data[idx]` was
> interpreted as a bit select (fixed in v0.15.1).

---

## Constant Literals and Bit Widths

Literals are **automatically annotated with a bit width** based on the type from context:

| Cm literal | Context type | SV output |
|------------|--------------|-----------|
| `true` | `bool` | `1'b1` |
| `false` | `bool` | `1'b0` |
| `42` | `uint` (32-bit) | `32'd42` |
| `42` | `utiny` (8-bit) | `8'd42` |
| `-5` | `int` (signed 32-bit) | `-32'sd5` |
| `0` | Comparison with `int` | `32'sd0` |

### Signed Constants Are Emitted with `'sd`

In SV, if one side of a comparison is unsigned, the **entire comparison becomes unsigned**. Cm emits constants as signed (`'sd`) according to their type, so negative checks like `s < 0` work correctly:

```cm
int s;
if (s < 0) { ... }   // → if ((s < 32'sd0))  Note: with 32'd0 this would always be false
```

### SV-Style Literals

```cm
utiny mask = 8'b10101010;     // → 8'b10101010
ushort addr = 16'hFF00;       // → 16'hFF00
```

---

## Constants and localparam

```cm
const uint CLK_FREQ = 27_000_000;
const uint CNT_MAX = CLK_FREQ / 2 - 1;
```
```systemverilog
localparam logic [31:0] CLK_FREQ = 32'd27000000;
localparam logic [31:0] CNT_MAX = CLK_FREQ / 2 - 32'd1;
```

> **Note:** A plain `const` always maps to `localparam`.
> To generate a module parameter (`module name #(parameter ...)`),
> declare it with `#[sv::parameter] const` (since v0.16.0; see [Module hierarchy](hierarchy.html)).

---

## SV Attributes

| Attribute | Effect | Example |
|-----------|--------|---------|
| `#[input]` | Input port | `#[input] posedge clk;` |
| `#[output]` | Output port | `#[output] utiny led = 0xFF;` |
| `#[inout]` | Bidirectional port | `#[inout] ushort bus;` |
| `#[sv::bram]` | `(* ram_style = "block" *)` | `#[sv::bram] utiny mem[1024];` |
| `#[sv::lutram]` | `(* ram_style = "distributed" *)` | `#[sv::lutram] utiny lut[16];` |
| `#[sv::clock_domain("name")]` | Clock selection for `async func` | `#[sv::clock_domain("fast")]` |
| `#[sv::pipeline]` | Pipeline hint | |
| `#[sv::share]` | Resource sharing hint | |
| `#[sv::pin("XX")]` | Pin assignment (XDC/CST) | `#[sv::pin("H11")]` |
| `#[sv::iostandard("YY")]` | IO voltage standard | `#[sv::iostandard("LVCMOS33")]` |

---

<!-- nav -->
← Prev: [Compiler - SystemVerilog Backend](index.html) | [Contents](index.html) | Next: [SV Backend - Processes and Assignments](processes.html) →
