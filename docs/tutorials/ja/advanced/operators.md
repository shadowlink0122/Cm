---
title: 演算子オーバーロード
parent: Tutorials
---

[English](../../en/advanced/operators.html)

# 高度な機能編 - 演算子オーバーロード

**難易度:** 🔴 上級  
**所要時間:** 30分

## 概要

Cm言語では `operator` キーワードを使って、構造体に対する演算子をオーバーロードできます。
オーバーロード可能な演算子は、対応するインターフェースを `impl` で実装します。

## サポートされる演算子インターフェース

| インターフェース | 演算子 | メソッド | 説明 |
|----------------|--------|---------|------|
| **Eq** | `==`, `!=` | `operator bool ==(T other)` | 等価比較 |
| **Ord** | `<`, `>`, `<=`, `>=` | `operator bool <(T other)` | 順序比較 |
| **Add** | `+` | `operator T +(T other)` | 加算（計画中） |
| **Sub** | `-` | `operator T -(T other)` | 減算（計画中） |
| **Mul** | `*` | `operator T *(T other)` | 乗算（計画中） |
| **Div** | `/` | `operator T /(T other)` | 除算（計画中） |
| **Mod** | `%` | `operator T %(T other)` | 剰余（計画中） |

> **Note:** `!=`は`==`から、`>`, `<=`, `>=`は`<`から自動導出されます。明示的に実装する必要はありません。

## Eq - 等価比較

```cm
struct Point {
    int x;
    int y;
}

impl Point for Eq {
    operator bool ==(Point other) {
        return self.x == other.x && self.y == other.y;
    }
}

int main() {
    Point p1 = Point(10, 20);
    Point p2 = Point(10, 20);
    Point p3 = Point(5, 5);

    if (p1 == p2) {
        println("p1 == p2: true");
    }
    // != は == から自動導出
    if (p1 != p3) {
        println("p1 != p3: true");
    }
    return 0;
}
```

## Ord - 順序比較

```cm
impl Point for Ord {
    operator bool <(Point other) {
        if (self.x != other.x) {
            return self.x < other.x;
        }
        return self.y < other.y;
    }
}

int main() {
    Point p1 = Point(1, 2);
    Point p2 = Point(3, 4);

    // < を実装すると、>, <=, >= も使える
    if (p1 < p2) { println("p1 < p2"); }
    if (p2 > p1) { println("p2 > p1"); }
    return 0;
}
```

## Add / Sub - 加算・減算（計画中）

> ⚠️ **注意:** Add/Sub演算子オーバーロードは現在型チェッカーで未サポートです。今後のバージョンで実装予定です。

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

int main() {
    Vec2 a = Vec2(1.0, 2.0);
    Vec2 b = Vec2(3.0, 4.0);

    Vec2 sum = a + b;   // Vec2(4.0, 6.0)
    Vec2 diff = a - b;  // Vec2(-2.0, -2.0)
    return 0;
}
```

## Mul / Div / Mod - 乗算・除算・剰余（計画中）

> ⚠️ **注意:** Mul/Div/Mod演算子オーバーロードは現在型チェッカーで未サポートです。

```cm
struct Number {
    int value;
}

impl Number for Mul {
    operator Number *(Number other) {
        return Number(self.value * other.value);
    }
}

impl Number for Div {
    operator Number /(Number other) {
        return Number(self.value / other.value);
    }
}

impl Number for Mod {
    operator Number %(Number other) {
        return Number(self.value % other.value);
    }
}

int main() {
    Number a = Number(10);
    Number b = Number(3);

    Number prod = a * b;  // Number(30)
    Number quot = a / b;  // Number(3)
    Number rem  = a % b;  // Number(1)
    return 0;
}
```

## with自動実装との違い

### with（自動生成）

```cm
// 全フィールドの比較が自動生成される
struct Point with Eq + Ord {
    int x;
    int y;
}
```

### 明示的実装（カスタムロジック）

```cm
struct Point {
    int x;
    int y;
}

// IDだけで比較したい場合など
impl Point for Eq {
    operator bool ==(Point other) {
        return self.x == other.x;  // yは無視
    }
}
```

> 📖 with自動実装の詳細は [with自動実装](with-keyword.html) を参照。

## 実装状況

| 演算子 | JIT | LLVM | WASM | JS |
|--------|-----|------|------|-----|
| `==` / `!=` (Eq) | ✅ | ✅ | ✅ | ✅ |
| `<` / `>` / `<=` / `>=` (Ord) | ✅ | ✅ | ✅ | ✅ |
| `+` (Add) | ⬜ | ⬜ | ⬜ | ⬜ |
| `-` (Sub) | ⬜ | ⬜ | ⬜ | ⬜ |
| `*` (Mul) | ⬜ | ⬜ | ⬜ | ⬜ |
| `/` (Div) | ⬜ | ⬜ | ⬜ | ⬜ |
| `%` (Mod) | ⬜ | ⬜ | ⬜ | ⬜ |

---

**前の章:** [with自動実装](with-keyword.html)  
**次の章:** [関数ポインタ](function-pointers.html)

---

**最終更新:** 2026-02-10
