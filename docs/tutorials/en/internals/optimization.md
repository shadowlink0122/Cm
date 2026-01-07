---
title: Optimization Passes
parent: Internals
---

[日本語](../../ja/internals/optimization.html)

# Optimization Pass Details

**Goal:** Understand all optimization passes implemented in the Cm compiler.  
**Time:** 30 minutes  
**Difficulty:** 🔴 Advanced

---

## Pass List

The Cm compiler implements the following optimization passes:

| Pass Name | Abbr | Level | Effect |
|-----------|------|-------|--------|
| Constant Folding | CF | All | Calculate at compile time |
| Dead Code Elimination | DCE | All | Remove unused code |
| Constant Propagation | CP | -O1+ | Replace variables with constants |
| Common Subexpression Elimination | CSE | -O1+ | Remove redundant calculations |
| SCCP | SCCP | -O2+ | Advanced constant analysis |
| Loop Invariant Code Motion | LICM | -O2+ | Move calculations out of loops |
| Goto Chain Removal | GC | All | Merge consecutive jumps |
| Empty Block Removal | BB | All | Remove useless blocks |

---

## Constant Folding

Replaces expressions that can be calculated at compile time with constants.

```cm
// Before Optimization
int x = 2 + 3 * 4;
bool b = !true;

// After Optimization
int x = 14;
bool b = false;
```

---

## Dead Code Elimination

Removes code that defines values that are never used (and have no side effects).

```cm
// Before Optimization
int main() {
    int x = 10;
    int y = 20; // y is never used
    return x;
}

// After Optimization
int main() {
    int x = 10;
    // int y = 20; is removed
    return x;
}
```

---

## Constant Propagation

If a variable is known to be a constant, replaces its usages with that constant.

```cm
// Before Optimization
int x = 10;
int y = x + 5;

// After Optimization (Propagation + Folding)
int x = 10;
int y = 15; // x is replaced by 10, then 10 + 5 is folded
```

---

## SCCP (Sparse Conditional Constant Propagation)

An advanced constant propagation that considers the reachability of conditional branches.

```cm
// Before Optimization
bool cond = true;
if (cond) {
    return 100;
} else {
    return 200; // Unreachable code
}

// After Optimization
return 100; // Branch removed
```

---

## LICM (Loop Invariant Code Motion)

Moves calculations that are constant within a loop to the outside of the loop.

```cm
// Before Optimization
for (int i = 0; i < n; i++) {
    int val = x * y; // Constant within loop
    arr[i] = val + i;
}

// After Optimization
int val = x * y; // Moved to pre-header
for (int i = 0; i < n; i++) {
    arr[i] = val + i;
}
```

---

## Common Subexpression Elimination (CSE)

Replaces repeated calculations of the same expression with a single calculation stored in a temporary variable.

```cm
// Before Optimization
int a = x * y + z;
int b = x * y + w;

// After Optimization
int tmp = x * y; // Calculated once
int a = tmp + z;
int b = tmp + w;
```

---

## Goto Chain Removal

Merges consecutive jump instructions into a single jump, skipping unnecessary blocks.

```
// Before Optimization (MIR)
bb0:
    goto bb1
bb1:
    goto bb2
bb2:
    return

// After Optimization
bb0:
    goto bb2 // Skips bb1, jumps directly to bb2
bb2:
    return
```

---

## Optimization Levels

```bash
./cm compile file.cm           # -O0: No optimization
./cm compile -O1 file.cm       # -O1: Basic optimization
./cm compile -O2 file.cm       # -O2: All optimizations
```

| Level | Enabled Passes |
|-------|----------------|
| -O0 | Goto Chain, BB Removal |
| -O1 | -O0 + CP, CSE, DCE, CF |
| -O2 | -O1 + SCCP, LICM |

---

## Reference Implementation

- [`src/mir/optimization/`](https://github.com/shadowlink0122/Cm/tree/main/src/mir/optimization) - Implementation of all optimization passes.