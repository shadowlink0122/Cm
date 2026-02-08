---
title: typedef
parent: Tutorials
---

[日本語](../../ja/types/typedef.html)

# Types - typedef

**Difficulty:** 🟡 Intermediate  
**Time:** 15 minutes

## Basic Type Aliases

```cm
typedef Integer = int;
typedef Real = double;
typedef Text = string;

Integer x = 42;
Real pi = 3.14159;
Text name = "Alice";
```

## Struct Aliases

```cm
struct Point {
    int x;
    int y;
}

typedef Position = Point;

int main() {
    Position pos;
    pos.x = 10;
    pos.y = 20;
    return 0;
}
```

## Literal Types (Syntax Only)

```cm
typedef HttpMethod = "GET" | "POST" | "PUT" | "DELETE";
typedef Digit = 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9;
```

---

**Previous:** [Enums](enums.html)  
**Next:** [Generics](generics.html)

---

**Last Updated:** 2026-02-08
