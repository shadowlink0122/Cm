---
layout: default
title: 演算子オーバーロード
parent: Tutorials
nav_order: 22
---

# 高度な機能編 - match式

**難易度:** 🔴 上級  
**所要時間:** 30分

## 基本的なmatch式

```cm
int value = 2;

match (value) {
    0 => println("zero"),
    1 => println("one"),
    2 => println("two"),
    _ => println("other"),
}
```

## Enum値パターン

```cm
enum Status {
    Ok = 0,
    Error = 1,
    Pending = 2
}

void handle_status(int s) {
    match (s) {
        Status::Ok => println("Success!"),
        Status::Error => println("Failed!"),
        Status::Pending => println("Waiting..."),
    }
}
```

## パターンガード

```cm
int classify(int n) {
    match (n) {
        n if n < 0 => println("Negative"),
        n if n == 0 => println("Zero"),
        n if n > 0 => println("Positive"),
    }
    return 0;
}
```

## 変数束縛パターン

```cm
int describe(int n) {
    match (n) {
        x if x % 2 == 0 => println("{} is even", x),
        x if x % 2 == 1 => println("{} is odd", x),
    }
    return 0;
}
```

## 網羅性チェック

```cm
enum Color {
    Red = 0,
    Green = 1,
    Blue = 2
}

void print_color(int c) {
    match (c) {
        Color::Red => println("Red"),
        Color::Green => println("Green"),
        Color::Blue => println("Blue"),
    }
}
```

---

**前の章:** [型制約](../types/constraints.md)  
**次の章:** [with自動実装](with-keyword.md)
MATCH
# 高度な機能編 - 演算子オーバーロード

**難易度:** 🔴 上級  
**所要時間:** 30分

## operator実装

```cm
interface Add<T, U> {
    operator T +(U other);
}

struct Point {
    int x;
    int y;
}

impl Point for Add {
    operator Point +(Point other) {
        Point result;
        result.x = self.x + other.x;
        result.y = self.y + other.y;
        return result;
    }
}

int main() {
    Point p1, p2;
    p1.x = 10; p1.y = 20;
    p2.x = 5; p2.y = 10;
    
    Point sum = p1 + p2;
    return 0;
}
```

## 各種演算子

```cm
struct Vec2 {
    float x;
    float y;
}

impl Vec2 for Add {
    operator Vec2 +(Vec2 other) {
        Vec2 result;
        result.x = self.x + other.x;
        result.y = self.y + other.y;
        return result;
    }
}

impl Vec2 for Sub {
    operator Vec2 -(Vec2 other) {
        Vec2 result;
        result.x = self.x - other.x;
        result.y = self.y - other.y;
        return result;
    }
}
```

## 比較演算子

```cm
impl Point for Eq {
    operator bool ==(Point other) {
        return self.x == other.x && self.y == other.y;
    }
}

impl Point for Ord {
    operator bool <(Point other) {
        if (self.x != other.x) {
            return self.x < other.x;
        }
        return self.y < other.y;
    }
}
```

---

**前の章:** [with自動実装](with-keyword.md)  
**次の章:** [関数ポインタ](function-pointers.md)
