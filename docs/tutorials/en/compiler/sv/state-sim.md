---
title: SV Backend - State Initialization and Simulation
parent: Tutorials
nav_order: 16
---

[日本語](../../../ja/compiler/sv/state-sim.html)

# SV Backend - State Initialization and Simulation

This is a detail page of the [SystemVerilog Backend](index.html). It covers register initial values, initial blocks, automatic testbench generation, and how to run the tests.

---

## Register Declaration Initial Values

The declared initial values of module-level variables are emitted as SV register declaration initial values:

```cm
uint state = 0;
uint counter = 42;
```
```systemverilog
logic [31:0] state = 32'd0;
logic [31:0] counter = 32'd42;
```

Effects:
- **FPGA synthesis**: treated as the register's power-on initial value (supported by Gowin/Xilinx/Intel)
- **Simulation**: prevents `X` propagation, so the output runs as-is in iverilog / Verilator

> **Note about older versions:** Previously initial values were not emitted, so in simulation
> all registers stayed `X` and FSMs never started (fixed in v0.15.1, 2026-07-04).
> Regression test: `tests/sv/advanced/reg_init`.

> **Limitation:** Initial values for arrays (BRAM) are not yet supported.

---

## initial Blocks

You can write an initialization block for simulation:

```cm
initial {
    counter = 0;
}
```
```systemverilog
initial begin
    counter = 0;
end
```

> **Supported statements:** assignments, variable declarations, and if statements.
> Display tasks (`$display`, etc.) are not yet supported.

---

## Automatic Testbench Generation

Writing a `//! test:` directive automatically generates a testbench (`*_tb.sv`).

### Testing Combinational Logic

```cm
//! platform: sv
//! test: a=255, b=15 -> band=15, bor=255

#[input]  int a = 0;
#[input]  int b = 0;
#[output] int band = 0;
#[output] int bor = 0;

void bitops() {
    band = a & b;
    bor = a | b;
}
```

Each `//! test:` line becomes one test case: the inputs are set and the outputs are verified.

### Testing Sequential Logic (with cycles)

```cm
//! test: cycles=1 -> sum=6
```

With `cycles=N`, the simulation advances N clock cycles before verifying the outputs.
The clock (`clk`) is generated automatically (10ns period), and if a reset (`rst`/`rst_n`)
exists, a reset sequence is inserted automatically as well.

> **Note:** Multiple `//! test:` cases run back-to-back within the same simulation.
> Register state is not reset between cases.

---

## Running the Tests

```bash
# Run SV tests only
make test-sv        # or make tsv

# SV tests (parallel)
make test-sv-parallel   # or make tsvp

# Run all tests (including SV)
make test
```

The test runner verifies in three stages:

1. **Compile**: `cm compile --target=sv` must succeed
2. **Lint**: `verilator --lint-only` (fallback: `iverilog -g2012`) must pass — `COMPILE_OK` in `.expect`
3. **Simulation**: run `iverilog + vvp` and compare `TEST k: name=val` lines against `.expect` — `SIM_OK` + `TEST` lines in `.expect`

For error tests, place `foo.cm` + `foo.error` (a description of the expected error)
to verify that compilation **fails**.

### x86_64 Debugging (for macOS developers)

```bash
make build-x86    # build the compiler for x86_64
make test-x86     # run tests on x86_64 (via Rosetta)
make debug-x86 FILE=tests/sv/basic/adder.cm
```

---

← [Data Structures](data.html) | [Semantic Guarantees](semantics.html) →

## Assertions (std::debug::assert)

`std::debug::assert` is emitted as an **immediate assertion** for the SV
target — checked in simulation and ignored by synthesis tools:

```systemverilog
always @(posedge clk) begin
    assert (value < 100) else $error("assertion failed: value out of range");
    out <= value;
end
```

On execution backends (JIT/native/WASM/JS) the standard library
implementation runs instead: it prints `assertion failed: <msg>` and
calls `exit(1)`. Only the SV target converts call sites to immediate
assertions, since hardware has no `exit` (the library function
definition itself is not emitted to SV).

Regression test: `tests/sv/simulation/assert_immediate`

## Cm testbench functions (#[sv::testbench], v0.16.0)

Sequential stimulus that single-shot `//! test:` vectors cannot express
can be written as a Cm function. A function marked `#[sv::testbench]`
is translated into the testbench's initial block:

```cm
import std::debug::assert;

#[sv::testbench]
void tb() {
    din = 5;
    step(1);                      // advance one clock
    assert(dout == 5, "first value latched");
    din = 7;
    step(2);
    assert(dout == 7, "second value latched");
}
```

- **`step(n)`**: wait n clocks (builtin available only in testbench functions)
- **`assert(cond, msg)`**: prints PASS, or prints FAIL and `$fatal`s
  (non-zero sim exit → detected by the test runner)
- **`println("...")`** → `$display` (string literals only)
- Assignments drive DUT inputs as blocking assigns
- A testbench function takes precedence over `//! test:` vectors
- In the test runner, use `SIM_OK` (expect completion) or
  `SIM_FAIL_EXPECTED` (expect a failing assertion) in `.expect`


### Testing real circuits (-D SIM)

Combine `-D` defines with `#ifdef` to swap the OSC/PLL clock for an
injected one only during simulation:

```cm
#ifdef SIM
#[input] posedge clk;            // simulation: injected clock
const uint DEBOUNCE_COUNT = 2;   // shortened timing
#end
#ifndef SIM
extern struct OSC { ... }        // hardware: built-in oscillator
bool clk = false;
OSC osc_inst;
const uint DEBOUNCE_COUNT = 525000;
#end
```

```bash
cm compile --target=sv -D SIM design.cm -o design.sv   # for tests
cm compile --target=sv design.cm -o design.sv          # for synthesis
```


---

<!-- nav -->
← Prev: [SV Backend - Board I/O (Pin Constraints, Tristate, CDC)](board-io.html) | [Contents](index.html) | Next: [SV Backend - Semantic Guarantees](semantics.html) →
