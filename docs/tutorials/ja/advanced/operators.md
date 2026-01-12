---
title: 演算子オーバーロード
parent: Tutorials
---

[English](../../en/advanced/operators.html)

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

**前の章:** [with自動実装](with-keyword.html)  
**次の章:** [関数ポインタ](function-pointers.html)
