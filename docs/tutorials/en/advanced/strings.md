---
title: String Operations
parent: Tutorials
---

[日本語](../../ja/advanced/strings.html)

# Advanced - String Operations

**Difficulty:** 🟡 Intermediate  
**Time:** 20 minutes

## String Methods

```cm
int main() {
    string str = "Hello, World!";

    int len = str.len();
    char first = str.charAt(0);
    string sub1 = str.substring(0, 5);
    int pos = str.indexOf("World");
    string upper = str.toUpperCase();
    string lower = str.toLowerCase();
    string trimmed = "  text  ".trim();
    bool starts = str.startsWith("Hello");
    bool ends = str.endsWith("!");
    bool contains = str.contains("World");
    string repeated = "Ha".repeat(3);
    string replaced = str.replace("World", "Cm");
    return 0;
}
```

## String Slicing

```cm
int main() {
    string s = "Hello, World!";

    string sub1 = s[0:5];
    string sub2 = s[7:12];
    string tail = s[7:];
    string head = s[:5];
    string copy = s[:];
    string last3 = s[-3:];
    return 0;
}
```

---

**Previous:** [Function Pointers](function-pointers.html)  

---

**Last Updated:** 2026-02-08

---

<!-- nav -->
← Prev: [ラムダ式](lambda.html) | [Contents](index.html) | Next: [スライス型](slices.html) →
