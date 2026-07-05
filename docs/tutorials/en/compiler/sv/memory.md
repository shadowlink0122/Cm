# SV Backend - Memory Initialization (ROM/RAM)

Three ways to emit initial contents for instruction ROMs, font ROMs, etc.

## 1. Array literal initializers (small memories)

Array initializers are emitted as an `initial` block. FPGA synthesis
tools treat them as ROM/RAM initial contents; simulators apply them at
time 0:

```cm
utiny[4] rom = [10, 20, 30, 40];
```

```systemverilog
logic [7:0] rom [0:3];
initial begin
    rom[0] = 10;
    rom[1] = 20;
    rom[2] = 30;
    rom[3] = 40;
end
```

Also works with `#[sv::bram]` / `#[sv::lutram]` attributed arrays.

## 2. The `#[sv::memfile]` attribute (large memories)

Emits `$readmemh` to load contents from a hex file, so large ROMs no
longer need giant case statements:

```cm
#[sv::bram]
#[sv::memfile("font.hex")]
utiny[4096] font_rom;
```

```systemverilog
(* ram_style = "block" *) logic [7:0] font_rom [0:4095];
initial $readmemh("font.hex", font_rom);
```

- Works with uninitialized arrays (provide the hex file in your
  synthesis/simulation environment)
- When combined with an array literal, `$readmemh` takes precedence

## 3. The `--emit-memfile` option

Writes array literal initializers out as `.hex` files at compile time
(next to the generated SV):

```bash
cm compile --target=sv rom.cm -o rom.sv --emit-memfile
```

The initial values stay managed in Cm source while the generated SV
references `$readmemh`, keeping the SV small for large ROMs.

Regression tests: `tests/sv/memory/array_init`, `tests/sv/memory/readmemh`
