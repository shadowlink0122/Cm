---
title: 型制約
parent: Tutorials
---

[English](../../en/types/constraints.html)

# 型システム編 - 型制約

**難易度:** 🔴 上級  
**所要時間:** 30分

## 基本的な型制約

```cm
interface Comparable<T> {
    operator bool <(T other);
}

<T: Comparable> T min(T a, T b) {
    return a < b ? a : b;
}
```

## AND境界

```cm
<T: Eq + Ord> T clamp(T value, T min_val, T max_val) {
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}
```

## OR境界

```cm
<T: Numeric | Stringable> void display(T value) {
    println("{}", value);
}
```

## where句

```cm
<T, U> Pair<T, U> make_pair(T first, U second)
where T: Clone, U: Clone {
    Pair<T, U> p;
    p.first = first.clone();
    p.second = second.clone();
    return p;
}
```

## 組み込みインターフェース

```cm
// Eq - 等価比較
interface Eq<T> {
    operator bool ==(T other);
}

// Ord - 順序比較
interface Ord<T> {
    operator bool <(T other);
}

// Clone - 複製
interface Clone {
    Self clone();
}
```

---

**前の章:** [インターフェース](interfaces.html)  
**次の章:** [match式](../advanced/match.html)
---

**最終更新:** 2026-02-08

---

<!-- nav -->
← 前: [型システム編 - インターフェース](interfaces.html) ｜ [目次](index.html) ｜ 次: [型システム編 - 所有権と借用](ownership.html) →
