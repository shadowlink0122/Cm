# SV Backend - Preserving Module Hierarchy

By default `import` flattens all symbols into a single module. With the `//! sv: hierarchy` directive, relative imports are kept as separate module instances instead.

## Usage

Submodule (`alu.cm`):

```cm
//! platform: sv

#[input] uint a;
#[input] uint b;
#[input] utiny op;
#[output] uint result = 0;

void alu_comb() {
    if (op == 0) {
        result = a + b;
    } else {
        result = a ^ b;
    }
}
```

Top module (`top.cm`):

```cm
//! platform: sv
//! sv: hierarchy

import ./alu;   // kept as a separate module, not flattened

#[input] posedge clk;
#[input] uint x;
#[input] uint y;
#[output] uint sum = 0;

uint alu_out = 0;
utiny op_add = 0;

// Connect ports with a struct literal (field name = submodule port name)
alu alu0 = alu { a: x, b: y, op: op_add, result: alu_out };

async void update(posedge clk) {
    sum = alu_out;
}
```

The generated `.sv` contains both modules, with a named port connection instance in `top`.

## Explicit struct declaration for module IO (IO instance, recommended)

A module's IO can be defined with a C/C++-style struct declaration and an instance of it.

```cm
// alu.cm
struct AluIo {
    #[input] uint a;
    #[input] uint b;
    #[output] uint result = 0;
};

AluIo io;

void alu_comb() {
    io.result = io.a + io.b;
}
```

- A global variable of a struct with `#[input]`/`#[output]` fields (an IO instance) expands its fields into module ports (port name = field name; individual port declarations become unnecessary)
- Module code accesses ports as `io.field`, which is flattened to the port names in the generated SV
- A default value on an `#[output]` field (`= 0`) becomes the port's power-on initial value
- A trailing semicolon after the struct declaration (`};`) is accepted (C/C++ compatible)
- Direct port declarations such as `#[input] posedge clk;` can be mixed with an IO instance
- The IO struct itself is not emitted as a data type (`typedef struct packed`)
- `#[test]` functions can also read and drive ports as `io.field`, flattened to port names in the generated testbench
- Parent-side instance connections accept `io.field` values (`alu { a: io.x, ... }` → `.a(x)`)
- `#[sv::pin]` attributes on IO fields are reflected in pin constraints (.cst/.xdc)

## Mechanics and constraints

- An extern struct is generated from the imported file's IO instance (struct fields) and direct port declarations (`#[input]`/`#[output]`/`#[inout]`); the instance type name is the file stem (`alu.cm` → `alu`)
- Imported files are compiled to SV individually and concatenated into the top-level `.sv`. Nested hierarchy imports and circular-import detection are supported
- Signals connected to instance outputs do not get declaration initializers (the instance drives them)
- Only simple relative imports (`import ./name;`) participate; selective imports (`::{...}`) and aliases are still flattened

Regression test: `tests/sv/hierarchy/hier_top`

## Module parameters (#[sv::parameter], v0.16.0)

A const marked `#[sv::parameter]` in the submodule becomes a `module #(parameter ...)`, and port/internal widths stay symbolic (`[WIDTH-1:0]`):

```cm
// shifter.cm
#[sv::parameter] const uint WIDTH = 8;

#[input] posedge clk;
#[input] bit[WIDTH] din;
#[output] bit[WIDTH] dout = 0;
```

When instantiating through the hierarchy, override parameters as struct-literal fields (defaults apply when omitted):

```cm
shifter sh0 = shifter { WIDTH: 16, clk: clk, din: data_in, dout: wide_out };
// → shifter #(.WIDTH(16)) sh0 (...);
```

> Parameter-dependent constant-loop unrolling and parameter-width
> memories (`bit[WIDTH][DEPTH]`) are not yet supported (roadmap A5/A6).


---

<!-- nav -->
← Prev: [SV Backend - Memory Initialization (ROM/RAM)](memory.html) | [Contents](index.html) | Next: [SV Backend - Board I/O (Pin Constraints, Tristate, CDC)](board-io.html) →
