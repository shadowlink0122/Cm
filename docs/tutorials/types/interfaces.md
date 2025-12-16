---
layout: default
title: インターフェース
parent: Tutorials
nav_order: 12
---

# 型システム編 - インターフェース

**難易度:** 🔴 上級  
**所要時間:** 30分

## インターフェース定義

```cm
interface Printable {
    void print();
}

interface Drawable {
    void draw();
}
```

## impl構文

```cm
struct Point {
    int x;
    int y;
}

impl Point for Printable {
    void print() {
        println("({}, {})", self.x, self.y);
    }
}

int main() {
    Point p;
    p.x = 10;
    p.y = 20;
    p.print();
    return 0;
}
```

## 複数のインターフェース実装

```cm
impl Point for Drawable {
    void draw() {
        println("Drawing point at ({}, {})", self.x, self.y);
    }
}

int main() {
    Point p;
    p.x = 5;
    p.y = 10;
    p.print();
    p.draw();
    return 0;
}
```

## privateメソッド

```cm
interface Calculator {
    int calculate(int x);
}

struct MyCalc {
    int base;
}

impl MyCalc for Calculator {
    private int helper(int n) {
        return n * 2;
    }
    
    int calculate(int x) {
        return self.helper(x) + self.base;
    }
}
```

---

**前の章:** [ジェネリクス](generics.md)  
**次の章:** [型制約](constraints.md)
