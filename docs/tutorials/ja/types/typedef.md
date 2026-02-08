---
title: typedef型エイリアス
parent: Tutorials
---

[English](../../en/types/typedef.html)

# 型システム編 - typedef型エイリアス

**難易度:** 🟡 中級  
**所要時間:** 15分

## 基本的な型エイリアス

```cm
typedef Integer = int;
typedef Real = double;
typedef Text = string;

Integer x = 42;
Real pi = 3.14159;
Text name = "Alice";
```

## 構造体のエイリアス

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

## リテラル型（構文のみ）

```cm
typedef HttpMethod = "GET" | "POST" | "PUT" | "DELETE";
typedef Digit = 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9;
```

---

**前の章:** [Enum型](enums.html)  
**次の章:** [ジェネリクス](generics.html)

---

**最終更新:** 2026-02-08
