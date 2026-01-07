---
title: Lifetimes
parent: Tutorials
---

[日本語](../../ja/types/lifetimes.html)

# Types - Lifetimes

**Difficulty:** 🔴 Advanced  
**Time:** 30 minutes

## What are Lifetimes?

A lifetime is the scope for which a reference is valid. The borrow checker ensures that references do not outlive the data they point to.

```cm
int main() {
    int* r;
    {
        int x = 5;
        r = &x; // Error: x does not live long enough
    }
    return 0;
}
```

---

**Previous:** [Ownership](ownership.html)
