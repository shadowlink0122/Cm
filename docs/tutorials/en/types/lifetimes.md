---
title: Scope and Validity (Not Implemented)
parent: Tutorials
---

[日本語](../../ja/types/lifetimes.html)

# Types - Scope and Validity

**Difficulty:** 🔴 Advanced
**Time:** 10 minutes

## Current Implementation Status

**Important:** As of Cm v0.11.0, lifetime parameters (`'a`, `'static`, etc.) are **NOT implemented**.

## Scope Rules

Current Cm applies these basic scope rules:

```cm
int main() {
    int* r;
    {
        int x = 5;
        r = &x; // x is destroyed at the end of this block
    }
    // Using *r would be undefined behavior (compiler doesn't detect this yet)
    return 0;
}
```

## Basic Borrowing Rules

Currently implemented borrow checking:

1. **Cannot move while borrowed** - Variables with active pointers cannot be moved
2. **Cannot modify while borrowed** - Assignment to borrowed variables is prohibited

```cm
int main() {
    int x = 100;
    const int* px = &x;  // Borrow x

    // int y = move x;  // Error: Cannot move while borrowed
    // x = 200;         // Error: Cannot modify while borrowed

    return 0;
}
```

## Future Implementation

The following features are planned but currently **NOT implemented**:
- Lifetime parameters (`'a`, `'static`, etc.)
- Lifetime annotations
- Advanced borrow checker

---

**Previous:** [Ownership](ownership.html)
