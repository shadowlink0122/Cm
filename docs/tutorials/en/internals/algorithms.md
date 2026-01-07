---
title: Compiler Algorithms
parent: Internals
---

[日本語](../../ja/internals/algorithms.html)

# Compiler Algorithms

**Goal:** Understand the standard compiler algorithms used in the Cm compiler.  
**Time:** 20 minutes  
**Difficulty:** 🔴 Advanced

---

## Data Flow Analysis

### Work-list Algorithm

Performs iterative analysis until convergence is reached.

**Usage:** SCCP, Constant Propagation, Liveness Analysis

```
Input:  CFG (Control Flow Graph)
Output: Data flow information at each point

worklist = all basic blocks
while worklist is not empty:
    block = worklist.pop()
    old_out = out[block]
    in[block] = meet(predecessors(block))
    out[block] = transfer(block, in[block])
    if out[block] != old_out:
        add successors to worklist
```

---

## Control Flow Analysis

### Tarjan's SCC (Strongly Connected Components)

Detects circular dependencies.

**Usage:** Module circular dependency detection.

### Dominator Tree

Determines if execution must pass through a specific point.

**Usage:** LICM (Loop detection), SSA construction.

---

## Intermediate Representation

### SSA (Static Single Assignment)

A form where each variable is assigned exactly once.

**Benefits:**
- Efficient Use-Def chains
- Simplifies constant propagation
- Easier dead code detection

```cm
// Normal Code
x = 1;
x = 2;
y = x;

// SSA Form
x_1 = 1;
x_2 = 2;
y = x_2;
```

### Phi Function

Integrates values from multiple paths.

```
if (cond) {
    x_1 = 10;
} else {
    x_2 = 20;
}
// Select value based on execution path
x_3 = φ(x_1, x_2); 
```

---

## Optimization Algorithms

### Value Numbering

Identifies equivalent expressions using hashing.

**Usage:** GVN (Global Value Numbering).

```cm
// Assign the same number to identical operations
int a = x + y; // v1 = Add(vx, vy)
int b = x + y; // v1 = Add(vx, vy) -> No recalculation needed
```

---

## Type System

### Monomorphization

Instantiates generic functions for each concrete type.

```cm
// Generic Definition
<T> T identity(T x) { return x; }

// Generated Code
int identity__int(int x) { return x; }
string identity__string(string x) { return x; }
```

---

## Reference

- [Optimization Passes](optimization.html) - Details of each pass.
