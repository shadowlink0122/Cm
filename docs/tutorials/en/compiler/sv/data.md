---
title: SV Backend - Data Structures
parent: Tutorials
nav_order: 15
---

[日本語](../../../ja/compiler/sv/data.html)

# SV Backend - Data Structures

This is a detail page of the [SystemVerilog Backend](index.html). It covers concatenation/replication, enums, arrays, and strings.

---

## Concatenation and Replication

### Basic Syntax

```cm
result = {a, b};         // → {a, b}
replicated = {3{a}};     // → {3{a}}
```

### Type Inference

Concatenation and replication automatically compute bit widths for `bit[N]` types:

```cm
#[input]  bit[4] a = 0;
#[input]  bit[4] b = 0;
#[output] bit[8] result = 0;      // {a, b} → 4+4=8 bits
#[output] bit[12] replicated = 0; // {3{a}} → 4*3=12 bits

always_comb void compute() {
    result = {a, b};
    replicated = {3{a}};
}
```

### Builtin Functions

When `{...}` is ambiguous with a block, explicit functions can be used:

```cm
result = concat(a, b);       // → {a, b}
wide = replicate(nibble, 3); // → {3{nibble}}
```

---

## Enums (FSM)

Cm's `enum` is converted to SV's `typedef enum logic`.
The bit width is automatically computed from the **maximum tag value** (explicit tag values are supported):

```cm
enum State { IDLE, RUN, DONE, ERROR }
// → typedef enum logic [1:0] { IDLE = 2'd0, RUN = 2'd1, DONE = 2'd2, ERROR = 2'd3 } State;

enum Status { OK = 0, NOT_FOUND = 404, SERVER_ERROR = 503 }
// → typedef enum logic [9:0] { OK = 10'd0, NOT_FOUND = 10'd404, SERVER_ERROR = 10'd503 } Status;
```

> **Note about older versions:** Previously the width was computed from the member count,
> so `ERROR = 100` could produce an invalid literal like `1'd100` (now fixed).

### enum + switch (FSM)

```cm
State current = State::IDLE;

void fsm(posedge clk) {
    switch (current) {
        case(State::IDLE) { current = State::RUN; }
        case(State::RUN) { current = State::DONE; }
        else { current = State::IDLE; }
    }
}
```

---

## Arrays and Memory

### Internal Arrays (Registers/RAM)

```cm
utiny buffer[16];                    // → logic [7:0] buffer [0:15];
#[sv::bram] utiny mem[1024];         // → (* ram_style = "block" *) logic [7:0] mem [0:1023];
#[sv::lutram] utiny lut[16];         // → (* ram_style = "distributed" *) logic [7:0] lut [0:15];
```

### Array-Typed Ports

```cm
#[output] uint[4] data;   // → output logic [31:0] data [0:3]
```

> Array **initial values** (e.g. `$readmemh`) are not yet supported. Write font ROMs and
> similar as const functions (lookup tables)
> (see the [implementation proposals](../../../../design/sv_backend_missing_features_en.html)).

---

## Strings

### const Strings (Recommended)

A const string becomes a packed vector constant (`localparam`),
and index access is converted to a part select:

```cm
export const string TITLE = "HELLO CM";

utiny ch = TITLE[i] as utiny;
// → localparam logic [63:0] TITLE = "HELLO CM";
//   ch = TITLE[(7 - i) * 8 +: 8];   // first character on the MSB side
```

### Limitations

- **Non-const string variables, function arguments, and return values are fixed at `logic [23:0]` (3 characters)**.
  Passing a string longer than 3 characters truncates it. Avoid using strings outside of const constants
  (an extension is being considered in the [implementation proposals](../../../../design/sv_backend_missing_features_en.html)).

---

← [Control Flow and Loops](control-flow.html) | [State Initialization and Simulation](state-sim.html) →
