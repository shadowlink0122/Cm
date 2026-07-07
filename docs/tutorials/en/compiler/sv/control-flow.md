---
title: SV Backend - Control Flow and Loops
parent: Tutorials
nav_order: 14
---

[日本語](../../../ja/compiler/sv/control-flow.html)

# SV Backend - Control Flow and Loops

This is a detail page of the [SystemVerilog Backend](index.html). It covers the conversion rules for branches and loops, and operator semantics.

---

## if / else if / else

```cm
if (rst) {
    counter = 0;
} else if (enable) {
    counter = counter + 1;
} else {
    // idle
}
```
```systemverilog
if (rst) begin
    counter <= 32'd0;
end else if (enable) begin
    counter <= counter + 32'd1;
end else begin
end
```

## switch → case

```cm
switch (state) {
    case(0) { next_state = 1; }
    case(1) { next_state = 2; }
    else { next_state = 0; }
}
```
```systemverilog
case (state)
    32'd0: begin next_state <= 32'd1; end
    32'd1: begin next_state <= 32'd2; end
    default: begin next_state <= 32'd0; end
endcase
```

> **Note:** Cm's switch syntax uses the `case(pattern) { ... }` form.
> The default case is written as `else { ... }`.

---

## Loops (While-Loop Reconstruction)

`for` / `while` loops inside a process are reconstructed as SV procedural `while` loops (updated in v0.15.1, 2026-07-04):

```cm
async void accumulate(posedge clk) {
    uint total = 0;
    for (uint i = 0; i < 4; i = i + 1) {
        total = total + i;
    }
    sum = total;   // executed after the loop
}
```

```systemverilog
always @(posedge clk) begin
    total = 32'sd0;
    i = 32'sd0;
    _t1002 = i;
    _t1003 = 32'sd4;
    _t1004 = _t1002 < _t1003;
    while (_t1004) begin
        total = total + i;
        i = i + 32'sd1;
        _t1002 = i;              // the condition is recomputed at the end of the loop
        _t1003 = 32'sd4;
        _t1004 = _t1002 < _t1003;
    end
    sum <= total;                // code after the loop is emitted in the correct position
end
```

How it works internally:
1. **Natural loops** (back edges) are detected in the MIR CFG based on dominance relations
2. The loop body is emitted as `while (cond) begin ... end`
3. The header block's condition-computation statements are re-executed at the end of the body (the condition temporaries are assigned twice, so they are excluded from inline expansion and remain as registers)
4. Code after the loop (the exit block) is emitted after the loop

> **Note about older versions:** Previously the back edge was lost, generating incorrect SV where the body ran "at most once" and post-loop code was unreachable. This is now guaranteed by the regression test `tests/sv/control/for_loop` (verifies sum=6 in simulation).

### break (disable idiom)

Loop exits are emitted as `disable` on a named block (Verilog-1995
compatible). The SV-2005 `break` keyword is not used because older
Icarus Verilog (v11 and earlier) and some synthesis tools do not
support it:

```systemverilog
begin : __loop0
    while (1'b1) begin
        if (c >= limit) begin
            disable __loop0;   // equivalent to break
        end else begin
            c = c + 32'sd1;
        end
    end
end
```

### Static unrolling of constant loops (generate equivalent)

Loops whose initial value, bound, and step are all constants are
statically unrolled at compile time for the SV target, so no `while`
remains in the generated SV (synthesis tools cannot unroll dynamic
while loops). Limits: 1024 iterations / 50,000 statements after
unrolling. `while (true) + break` and dynamically-bounded loops are
still emitted as while + disable.
Regression test: `tests/sv/control/const_loop_unroll`.

### Nested Loops

Nested loops are also reconstructed correctly (an inner loop is identified by its natural-loop membership, so it is never confused with the outer loop's back edge). Regression test: `tests/sv/control/nested_loop`.

### Synthesis Notes

- If the loop count is **statically known**, synthesis tools (Gowin/Vivado, etc.) unroll the loop during synthesis
- If the loop count depends on inputs, most synthesis tools report an error (simulation still works). For work that does not have to complete within one clock cycle, we recommend writing an FSM that advances one step per clock
- Loops with an unconditional header such as `while (true)` + `break` are also reconstructed via named blocks + `disable` (supported since v0.15.1)

---

## Operators

| Cm | SV | Notes |
|----|----|-------|
| `+` `-` `*` `/` `%` | Same | Arithmetic |
| `&` `\|` `^` `~` | Same | Bitwise operations |
| `<<` | `<<` | Left shift |
| `>>` | `>>` / `>>>` | **Signed types use `>>>` (arithmetic shift)** |
| `==` `!=` `<` `<=` `>` `>=` | Same | Comparison (signed constants are emitted with `'sd`) |
| `&&` `\|\|` | Same | Logical operations |
| `!x` | `~x` | Logical negation unified with bitwise negation |
| `x as T` | `N'(x)` etc. | Width changes use size casts; sign changes use `$signed`/`$unsigned` |

### Precedence Guarantee

The structure of expressions in Cm source (parentheses, evaluation order) is preserved in the generated SV.
In SV, `==` binds tighter than `&`, so losing parentheses would change the meaning —
the compiler always emits the necessary parentheses:

```cm
if ((r_qm & 256) == 0) { ... }
// → if (((r_qm & 32'd256) == 32'd0))
```

## Don't-care bit matching (v0.16.0)

`?` in a binary literal means "don't compare this bit" — ideal for
instruction decoders:

```cm
match (op) {
    0b1?00 => { kind = 1; }   // matches 1x00 (bit2 ignored)
    0b0?1? => { kind = 2; }
    _ => { kind = 0; }
}
```

- Desugared to `(op & mask) == value` if-else chains, identical on all backends
- `?` literals are match-pattern only (not usable in expressions)
- Plain match/switch now emits `unique case` in SV, enabling duplicate-hit detection in simulation


---

<!-- nav -->
← Prev: [SV Backend - Processes and Assignments](processes.html) | [Contents](index.html) | Next: [SV Backend - Data Structures](data.html) →
