# SV Backend - Preserving Module Hierarchy

By default `import` flattens all symbols into a single module. With the
`//! sv: hierarchy` directive, relative imports are kept as separate
module instances instead.

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

The generated `.sv` contains both modules, with a named port connection
instance in `top`.

## Mechanics and constraints

- An extern struct is auto-generated from the imported file's port
  declarations (`#[input]`/`#[output]`/`#[inout]`); the instance type
  name is the file stem (`alu.cm` → `alu`)
- Imported files are compiled to SV individually and concatenated into
  the top-level `.sv`. Nested hierarchy imports and circular-import
  detection are supported
- Signals connected to instance outputs do not get declaration
  initializers (the instance drives them)
- Only simple relative imports (`import ./name;`) participate;
  selective imports (`::{...}`) and aliases are still flattened

Regression test: `tests/sv/hierarchy/hier_top`

---

<!-- nav -->
← Prev: [SV Backend - Memory Initialization (ROM/RAM)](memory.html) | [Contents](index.html) | Next: [SV Backend - Board I/O (Pin Constraints, Tristate, CDC)](board-io.html) →
