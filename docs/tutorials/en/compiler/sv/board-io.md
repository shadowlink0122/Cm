# SV Backend - Board I/O (Pin Constraints, Tristate, CDC)

Features added in v0.16.0 for wiring designs to real boards and handling
asynchronous inputs.

## Pin constraints and project script emission (--emit-constraints)

Physical pin assignments live next to the port declarations via the
`#[sv::pin]` attribute, and the compiler can emit Gowin `.cst` (pin
constraints) and `.tcl` (project script) files. This structurally
prevents the manual-sync errors that happen when port names change.

```cm
//! platform: sv
//! sv: device: GW5AST-LV138PG484AC1/I0 C
//! sv: option: use_ready_as_gpio

#[input]
#[sv::pin("T10")]
posedge clk;

#[output]
#[sv::pin("U12", io_type: "LVCMOS33", drive: 8)]
bool led_ready = false;
```

```bash
cm compile --target=sv blink.cm -o blink.sv --emit-constraints
# → blink.sv / blink.cst / blink_build.tcl
gw_sh blink_build.tcl
```

- The first argument of `#[sv::pin]` is the physical pin (required).
  Remaining `key: value` pairs map `io_type` / `drive` / `pull` / `slew`
  to official attribute names (IO_TYPE etc.); unknown keys are
  upper-cased and passed through (open to tool-specific attributes)
- Without `//! sv: device:` only the `.cst` is generated
- With `--emit-constraints`, ports lacking `#[sv::pin]` produce warnings
- Emission is opt-in; existing hand-written .cst/.tcl workflows keep working

## Tristate (#[sv::tri])

Bidirectional open-drain buses (e.g. I2C) are declared with an
output-enable/output pair. oe=1 drives, oe=0 releases to high impedance
(`'z`).

```cm
#[inout]
#[sv::tri(oe: "sda_oe", out: "sda_out")]
bool sda;

bool sda_oe = false;
bool sda_out = false;
```

```systemverilog
inout tri sda;
assign sda = sda_oe ? sda_out : 1'bz;
```

The port is declared as the multi-driver net type `tri`. Reads behave as
ordinary values (assuming a bus pull-up). Do not assign to `sda`
directly — control it through oe/out.

## Clock-domain-crossing synchronization (#[sv::sync])

Asynchronous inputs such as buttons cause metastability when used
directly. `#[sv::sync]` declaratively generates a 2FF synchronizer.

```cm
#[input] posedge clk;
#[input] bool async_btn;

#[sv::sync(clk: "clk", src: "async_btn", stages: 2)]
bool btn_sync;
```

```systemverilog
(* async_reg = "true" *) logic btn_sync_meta1;
(* async_reg = "true" *) logic btn_sync;

always @(posedge clk) begin
    btn_sync_meta1 <= async_btn;
    btn_sync       <= btn_sync_meta1;
end
```

`stages` defaults to 2. Generated registers carry the
`(* async_reg = "true" *)` synthesis attribute.

---

<!-- nav -->
← Prev: [SV Backend - Preserving Module Hierarchy](hierarchy.html) | [Contents](index.html) | Next: [SV Backend - State Init and Simulation](state-sim.html) →
