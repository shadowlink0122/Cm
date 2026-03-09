---
title: SystemVerilog Backend
parent: Tutorials
nav_order: 11
---

[日本語](../../ja/compiler/sv.html)

# Compiler - SystemVerilog Backend

**Difficulty:** 🟡 Intermediate  
**Time:** 30 min

Cm can generate SystemVerilog (SV) code to run as hardware on FPGAs. Compatible with Tang Console (Gowin), Xilinx, Intel, and more.

## Basic Usage

```bash
# Generate SV
cm compile --target=sv program.cm -o output.sv

# A testbench is also auto-generated
# output_tb.sv is created alongside
```

## Port Declaration

Use `#[input]` / `#[output]` attributes to declare I/O ports.

```cm
//! platform: sv

#[input]  int a = 0;
#[input]  int b = 0;
#[output] int sum = 0;

void adder() {
    sum = a + b;
}
```

Generated SV:

```systemverilog
module adder (
    input logic signed [31:0] a,
    input logic signed [31:0] b,
    output logic signed [31:0] sum
);

    // adder
    always_comb begin
        sum = a + b;
    end

endmodule
```

## Combinational and Sequential Logic

### Combinational Logic (always_comb)

Regular functions are generated as combinational logic (`always_comb`).

```cm
//! platform: sv

#[input]  int a = 0;
#[input]  int b = 0;
#[input]  int c = 0;
#[output] int out = 0;

void max3() {
    if (a > b) {
        if (a > c) { out = a; } else { out = c; }
    } else {
        if (b > c) { out = b; } else { out = c; }
    }
}
```

### Sequential Logic (always_ff)

Using `posedge` / `negedge` type parameters generates `always_ff` blocks.

```cm
//! platform: sv

#[output] uint counter = 0;
#[output] bool led = false;

void blink(posedge clk, bool rst) {
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

Generated SV:

```systemverilog
always_ff @(posedge clk) begin
    if (rst) begin
        counter <= 32'd0;
        led <= 1'b0;
    end else begin
        if (counter == 32'd49999999) begin
            counter <= 32'd0;
            led <= !led;
        end else begin
            counter <= counter + 32'd1;
        end
    end
end
```

## SV-Specific Types

| Cm Type | Description | Generated SV |
|---------|-------------|-------------|
| `posedge` | Rising edge | `always_ff @(posedge ...)` |
| `negedge` | Falling edge | `always_ff @(negedge ...)` |
| `wire` | Wire | `wire` |
| `reg` | Register | `reg` |

## SV Width-Qualified Literals

SystemVerilog-style width-qualified literals can be written directly and are preserved in the SV output.

```cm
//! platform: sv

#[input]  utiny sel = 0;
#[output] utiny out = 0;

void literal_test() {
    if (sel == 0) {
        out = 3'b101;     // Binary: 3-bit width
    } else if (sel == 1) {
        out = 8'hFF;      // Hexadecimal: 8-bit width
    } else {
        out = 8'd170;     // Decimal: 8-bit width
    }
}
```

| Cm Source | SV Output |
|-----------|-----------|
| `3'b101` | `3'b101` |
| `8'hFF` | `8'hFF` |
| `8'd170` | `8'd170` |

## Cm to SV Type Mapping

| Cm Type | SV Bit Width | SV Type |
|---------|-------------|---------|
| `bool` | 1 | `logic` |
| `utiny` | 8 | `logic [7:0]` |
| `tiny` | 8 | `logic signed [7:0]` |
| `ushort` | 16 | `logic [15:0]` |
| `short` | 16 | `logic signed [15:0]` |
| `uint` | 32 | `logic [31:0]` |
| `int` | 32 | `logic signed [31:0]` |

## BRAM Inference

Arrays are inferred as Block RAM (BRAM).

```cm
//! platform: sv

int memory[256];
#[input]  utiny addr = 0;
#[input]  int   wdata = 0;
#[input]  bool  we = false;
#[output] int   rdata = 0;

void bram_access(posedge clk) {
    if (we) {
        memory[addr] = wdata;
    }
    rdata = memory[addr];
}
```

## Auto-Generated Testbench

Running `cm compile --target=sv` also generates a `_tb.sv` testbench. Verify with iverilog:

```bash
# Compile and generate testbench
cm compile --target=sv program.cm -o output.sv

# Run simulation
iverilog -g2012 -o sim output.sv output_tb.sv
vvp sim
```

## Target FPGAs

| Board | Chip | Tool |
|-------|------|------|
| Tang Console | Gowin | Gowin EDA |
| Tang Nano 9K | Gowin GW1NR-9 | Gowin EDA |
| Arty A7 | Xilinx Artix-7 | Vivado |
| DE10-Lite | Intel MAX 10 | Quartus |

> **Note:** In Gowin EDA, enable SystemVerilog via Project → Configuration → Synthesis → Verilog Language.

---

**Previous:** [WASM Backend](wasm.html)  
**Next:** [Formatter](formatter.html)

---

**Last updated:** 2026-03-09
