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

Cm's `enum` is converted to SV's `typedef enum logic`. The bit width is automatically computed from the **maximum tag value** (explicit tag values are supported):

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

> Array **initial values** are emitted as initial blocks, and `#[sv::memfile]` /
> `--emit-memfile` provide `$readmemh` support (since v0.15.1).
> See [Memory initialization](memory.html).

---

## Strings

### const Strings (Recommended)

A const string becomes a packed vector constant (`localparam`), and index access is converted to a part select:

```cm
export const string TITLE = "HELLO CM";

utiny ch = TITLE[i] as utiny;
// → localparam logic [63:0] TITLE = "HELLO CM";
//   ch = TITLE[(7 - i) * 8 +: 8];   // first character on the MSB side
```

### Limitations

- **Non-const string variables, function arguments, and return values are fixed at `logic [23:0]` (3 characters)**. Passing a string longer than 3 characters truncates it. Avoid using strings outside of const constants (typed string lengths are being considered in the [v0.16.0 roadmap](../../../../archive/v0.16.0/roadmap.html)).

## Bit slices (v0.16.0)

Read and write sub-ranges of `bit[N]` / integer values using SV-style descending, inclusive ranges:

```cm
bit[16] word = 0xABCD;
bit[8] hi = word[15:8];      // 0xAB (constant range)
word[11:4] = 0xFF;           // partial assignment (read-modify-write)

uint i = 1;
bit[4] nib = word[i*4 +: 4]; // variable base + constant width
```

- Range/width must be **integer literals** (v0.16.0 restriction); the base of `+:` may be any integer expression
- Desugared to shifts+masks shared by all backends, so execution backends (JIT/native/WASM/JS) produce identical results
- Max width 64; the result type is `bit[w]` (interchangeable with integers)

## Reduction operators (v0.17.0)

Reduction operations that fold every bit of a vector into a single bit (`bool`) are provided as builtin functions. On the SV target they emit native reduction operators (`&x`, `|x`, `^x`, `~&x`, `~|x`, `~^x`):

```cm
#[input]  bit[8] flags = 0;
#[output] bool all_set = false;
#[output] bool parity = false;

void check() {
    all_set = reduce_and(flags);  // → all_set = &(flags);   AND of all bits
    parity  = reduce_xor(flags);  // → parity  = ^(flags);   parity
}
```

| Builtin | Meaning | SV output |
|---------|---------|-----------|
| `reduce_and(x)` | AND of all bits (true when every bit is 1) | `&x` |
| `reduce_or(x)` | OR of all bits (true when any bit is 1) | `\|x` |
| `reduce_xor(x)` | XOR of all bits (true when the number of 1s is odd = parity) | `^x` |
| `reduce_nand(x)` | NAND (negation of `reduce_and`) | `~&x` |
| `reduce_nor(x)` | NOR (negation of `reduce_or`) | `~\|x` |
| `reduce_xnor(x)` | XNOR (negation of `reduce_xor`) | `~^x` |

- The operand must be an integer or `bit[N]` type (non-integers are a compile error). The fold width is the operand's type width (`bit[8]`=8 bits, `uint`=32 bits)
- The result is `bool` (1 bit). To drive an SV output port, use a `bool` port
- On non-SV backends (JIT/native/WASM/JS) it desugars to mask comparisons / parity arithmetic, producing identical results across all backends
- `reduce_xor`/`reduce_xnor` evaluate the operand once per bit, so pass a variable or field rather than a side-effecting expression (such as a function call)


---

<!-- nav -->
← Prev: [SV Backend - Control Flow and Loops](control-flow.html) | [Contents](index.html) | Next: [SV Backend - Memory Initialization (ROM/RAM)](memory.html) →

---

## Interfaces and impl methods

Struct methods defined with `interface` / `impl` are synthesized as SV `function automatic`.
The compiler automatically converts the method's `self` to pass-by-value, so interfaces work on SV even though it has no pointers.

```cm
interface Summable {
    int total();
}

struct Pair {
    int x;
    int y;
}

impl Pair for Summable {
    int total() {
        return self.x + self.y;
    }
}

void compute() {
    Pair p;
    p.x = a;
    p.y = b;
    total = p.total();  // becomes a call to the SV function Pair__total(p)
}
```

Restrictions (reported as clear diagnostics):

- Methods that write to `self` fields are not supported (`error[SV010]`; pass-by-value would not propagate to the caller)
- Dynamic dispatch through interface-typed variables is not supported (`error[SV011]`; call through a concrete struct type so the target resolves statically)
- Escaping the `self` pointer value outside a method call is not supported (`error[SV012]`)
