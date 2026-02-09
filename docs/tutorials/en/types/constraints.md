---
title: Constraints
parent: Tutorials
---

[日本語](../../ja/types/constraints.html)

# Types - Constraints

**Difficulty:** 🔴 Advanced  
**Time:** 30 minutes

## Basic Constraints

```cm
interface Comparable<T> {
    operator bool <(T other);
}

<T: Comparable> T min(T a, T b) {
    return a < b ? a : b;
}
```

## AND Bounds

```cm
<T: Eq + Ord> T clamp(T value, T min_val, T max_val) {
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}
```

## OR Bounds

```cm
<T: Numeric | Stringable> void display(T value) {
    println("{}", value);
}
```

## where Clause

```cm
<T, U> Pair<T, U> make_pair(T first, U second)
where T: Clone, U: Clone {
    Pair<T, U> p;
    p.first = first.clone();
    p.second = second.clone();
    return p;
}
```

## Built-in Interfaces

```cm
// Eq - Equality comparison
interface Eq<T> {
    operator bool ==(T other);
}

// Ord - Order comparison
interface Ord<T> {
    operator bool <(T other);
}

// Clone - Cloning
interface Clone {
    Self clone();
}
```

---

**Previous:** [Interfaces](interfaces.html)  
**Next:** [match Expression](../advanced/match.html)
---

**Last Updated:** 2026-02-08
