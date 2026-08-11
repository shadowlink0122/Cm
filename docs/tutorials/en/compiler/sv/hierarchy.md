# SV Backend - Preserving Module Hierarchy

By default `import` flattens all symbols into a single module. When the imported file declares an **exported IO struct** (an `export struct` with direction-attributed fields), that relative import is kept as a separate module instance instead.

## Usage

The submodule (`alu.cm`) declares its public interface contract as an exported IO struct:

```cm
//! platform: sv

export struct AluIo {
    #[input] uint a;
    #[input] uint b;
    #[input] utiny op;
    #[output] uint result = 0;
};

AluIo io;

void alu_comb() {
    if (io.op == 0) {
        io.result = io.a + io.b;
    } else {
        io.result = io.a ^ io.b;
    }
}
```

The top module (`top.cm`) instantiates it with the qualified name `<module>::<IoStruct>`:

```cm
//! platform: sv

import ./alu;   // kept as a separate module because it declares an exported IO struct

#[input] posedge clk;
#[input] uint x;
#[input] uint y;
#[output] uint sum = 0;

uint alu_out = 0;
utiny op_add = 0;

// Connect ports with a struct literal (field name = submodule port name)
alu::AluIo alu0 = alu::AluIo { a: x, b: y, op: op_add, result: alu_out };

async void update(posedge clk) {
    sum = alu_out;
}
```

The generated `.sv` contains both modules, with a named port connection instance in `top`.

Because the type resolves through the regular module system, `cm check` and `cm lint` pass without any special handling.

> The `//! sv: hierarchy` directive was removed in v0.16.2. It is now ignored as a plain comment, and relative imports whose target has no exported IO struct are flattened as before.

## Explicit struct declaration for module IO (IO instance)

A module's IO is defined with a C/C++-style struct declaration and an instance of it.

```cm
// alu.cm
export struct AluIo {
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
- Direct port declarations such as `#[input] posedge clk;` can be mixed with an IO instance. In modules that participate in hierarchy, declare the clock as an IO struct field too (`#[input] bool clk;`); `async void f(posedge clk)` triggers keep working
- The IO struct itself is not emitted as a data type (`typedef struct packed`)
- `#[test]` functions can also read and drive ports as `io.field`, flattened to port names in the generated testbench
- Parent-side instance connections accept `io.field` values (`alu::AluIo { a: io.x, ... }` → `.a(x)`)
- `#[sv::pin]` attributes on IO fields are reflected in pin constraints (.cst/.xdc)

## Mechanics and constraints

- An extern struct is generated from the imported file's exported IO struct and replaces the import line (the instance type name is the file stem: `alu.cm` → `alu`; qualified names like `alu::AluIo` in the parent source are rewritten to `alu`)
- Imported files are compiled to SV individually and concatenated into the top-level `.sv`. Nested hierarchy imports and circular-import detection are supported
- Signals connected to instance outputs do not get declaration initializers (the instance drives them)
- Only simple relative imports (`import ./name;`) participate; selective imports (`::{...}`) and aliases are still flattened

Regression test: `tests/sv/hierarchy/hier_top`

## Module parameters (#[sv::parameter], #[sv::param])

A const marked `#[sv::parameter]` in the submodule becomes a `module #(parameter ...)`, and port/internal widths stay symbolic (`[WIDTH-1:0]`). As part of the interface contract, declare a `#[sv::param]` field in the IO struct so parameter overrides are type-checked at the instantiation site:

```cm
// shifter.cm
#[sv::parameter] const uint WIDTH = 8;

export struct ShifterIo {
    #[sv::param] uint WIDTH = 8;
    #[input] bool clk;
    #[input] bit[WIDTH] din;
    #[output] bit[WIDTH] dout = 0;
};

ShifterIo io;

async void shift(posedge clk) {
    io.dout = io.din;
}
```

When instantiating through the hierarchy, override parameters as struct-literal fields (defaults apply when omitted):

```cm
import ./shifter;

shifter::ShifterIo sh0 = shifter::ShifterIo { WIDTH: 16, clk: clk, din: data_in, dout: wide_out };
// → shifter #(.WIDTH(16)) sh0 (...);
```

Loops bounded by a parameter (`for (uint i = 0; i < N; i = i + 1)` with `N` declared `#[sv::parameter]`) are emitted as synthesizable SV for statements (v0.17.0; while loops are rejected by synthesis tools, so counted loops are reconstructed into for form):

```cm
#[sv::parameter] const uint N = 4;

async void update(posedge clk) {
    uint acc = 0;
    for (uint i = 0; i < N; i = i + 1) {
        acc = acc ^ (din >> i);
    }
    out = acc;
}
// → for (i = 32'sd0; i < N; i = i + 32'sd1) begin ... end
```

Parameter-width memories (`bit[WIDTH][DEPTH]`) are emitted as `logic [WIDTH-1:0] mem [0:DEPTH-1];` and can be indexed directly (v0.17.0):

```cm
#[sv::parameter] const uint WIDTH = 8;
#[sv::parameter] const uint DEPTH = 4;

bit[WIDTH][DEPTH] mem;

async void update(posedge clk) {
    mem[waddr] = din;
    dout = mem[0];
}
```


---

<!-- nav -->
← Prev: [SV Backend - Memory Initialization (ROM/RAM)](memory.html) | [Contents](index.html) | Next: [SV Backend - Board I/O (Pin Constraints, Tristate, CDC)](board-io.html) →
