---
title: SV Backend - Processes and Assignments
parent: Tutorials
nav_order: 13
---

[日本語](../../ja/compiler/sv-processes.html)

# SV Backend - Processes and Assignments

This is a detail page of the [SystemVerilog Backend](sv.html). It covers the rules for generating always blocks, and the rules for assignments and implicit conversions.

---

## Logic Blocks

### Sequential Logic (always_ff)

#### Pattern A: `always` + edge parameter (recommended)

```cm
always void counter_tick(posedge clk) {
    count = count + 1;
}
// → always @(posedge clk) begin
//        count <= count + 32'd1;
//    end
```

#### Pattern B: Asynchronous reset (multiple edges)

```cm
always void process(posedge clk, negedge rst_n) {
    if (rst_n == false) {
        count = 0;
    } else {
        count = count + 1;
    }
}
// → always @(posedge clk or negedge rst_n) begin ...
```

#### Pattern C: `void f(posedge clk)` (backward compat)

```cm
void blink(posedge clk) {
    led = !led;
}
// → always @(posedge clk) begin led <= ~led; end
```

#### Pattern D: `async func` (backward compat)

```cm
async func tick() {
    counter = counter + 1;
}
// → always @(posedge clk) begin counter <= counter + 32'd1; end
```

> **Note:** `async func` implicitly references the `clk` variable.
> If `clk` is not declared, `input logic clk` is added automatically.

### Combinational Logic (always_comb)

A void function without edge parameters:

```cm
always void decode() {
    out = 0;
    if (sel) { out = a; }
    else { out = b; }
}
// → always_comb begin ... end
```

Backward compat: `void f()` / `func f()` are also converted to `always_comb`.

### function

A function that takes arguments (no edge parameters) and is **non-void (has a return value)** is automatically converted to an SV `function automatic`:

```cm
uint max_val(uint x, uint y) {
    if (x > y) { return x; }
    return y;
}
// → function automatic logic [31:0] max_val(...); ... endfunction
```

---

## Automatic Assignment Conversion Rules

| Block kind | Written in Cm | SV output |
|------------|---------------|-----------|
| `always_ff` (sequential) | `x = expr;` | `x <= expr;` (non-blocking) |
| `always_comb` (combinational) | `x = expr;` | `x = expr;` (blocking) |

In Cm you always write `=`, and the compiler selects the appropriate assignment style based on context.

---

## Implicit Conversions

The SV backend performs a number of implicit conversions to automatically generate correct SV code.

### Logical Negation Conversion

| Cm | SV | Reason |
|----|----|--------|
| `!flag` | `~flag` | Unified with `~`, which is safe for multi-bit signals (`!` is restricted to bool by type checking) |

### Literal Bit-Width Annotation

| Cm | Target type | SV |
|----|-------------|-----|
| `counter = 0;` | `uint` | `counter <= 32'd0;` |
| `flag = true;` | `bool` | `flag <= 1'b1;` |

### Automatic Clock/Reset Insertion

| Condition | Behavior |
|-----------|----------|
| `async func` present & `clk` undeclared | `input logic clk` is added automatically |
| `async func` present & `rst` undeclared | `input logic rst` is added automatically |

### Inline Expansion of MIR Temporaries

MIR `_tXXXX` temporaries are inlined back into their original expressions.
During expansion, **parentheses are inserted with operator precedence taken into account**:

```
MIR:  _t1000 = a & 256; _t1001 = _t1000 == 0;
SV:   if (((a & 32'd256) == 32'd0))   // parentheses are preserved
```

> Temporaries that are assigned multiple times, such as a while-loop condition,
> are not expanded and remain as registers (see [Control Flow and Loops](sv-control-flow.html)).

### Others

- **`self.` prefix removal**: `self.counter` → `counter`
- **`else if` normalization**: nested `else { if ... }` is flattened to `else if`
- **Redundant ternary removal**: `cond ? x : x` → `x`

---

← [Types and Ports](sv-types.html) | [Control Flow and Loops](sv-control-flow.html) →
