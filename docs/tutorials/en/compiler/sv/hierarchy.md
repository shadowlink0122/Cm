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

## Mechanics and constraints

- An extern struct is auto-generated from the imported file's port declarations (`#[input]`/`#[output]`/`#[inout]`); the instance type name is the file stem (`alu.cm` → `alu`)
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
