---
title: 演算子オーバーロード
parent: Tutorials
---

[English](../../en/advanced/operators.html)

# 高度な機能編 - 演算子オーバーロード

**難易度:** 🔴 上級  
**所要時間:** 30分

## 概要

Cm言語では `operator` キーワードを使って、構造体に対する演算子をオーバーロードできます。演算子は `impl T { operator ... }` ブロック内で直接定義します。

## サポートされる演算子

### 比較演算子

| 演算子 | シグネチャ | 説明 |
|--------|-----------|------|
| `==`, `!=` | `operator bool ==(T other)` | 等価比較（`!=`は自動導出） |
| `<`, `>`, `<=`, `>=` | `operator bool <(T other)` | 順序比較（`>`, `<=`, `>=`は自動導出） |

### 算術演算子

| 演算子 | シグネチャ | 説明 |
|--------|-----------|------|
| `+` | `operator T +(T other)` | 加算 |
| `-` | `operator T -(T other)` | 減算 |
| `*` | `operator T *(T other)` | 乗算 |
| `/` | `operator T /(T other)` | 除算 |
| `%` | `operator T %(T other)` | 剰余 |

### ビット演算子

| 演算子 | シグネチャ | 説明 |
|--------|-----------|------|
| `&` | `operator T &(T other)` | ビットAND |
| `\|` | `operator T \|(T other)` | ビットOR |
| `^` | `operator T ^(T other)` | ビットXOR |
| `<<` | `operator T <<(T other)` | 左シフト |
| `>>` | `operator T >>(T other)` | 右シフト |

> **Note:** `!=`は`==`から、`>`, `<=`, `>=`は`<`から自動導出されます。

## 基本的な使い方

演算子は `impl T { ... }` ブロック内で定義します：

```cm
struct Vec2 {
    int x;
    int y;
}

impl Vec2 {
    operator Vec2 +(Vec2 other) {
        return Vec2{x: self.x + other.x, y: self.y + other.y};
    }

    operator Vec2 -(Vec2 other) {
        return Vec2{x: self.x - other.x, y: self.y - other.y};
    }

    operator Vec2 *(Vec2 other) {
        return Vec2{x: self.x * other.x, y: self.y * other.y};
    }
}

int main() {
    Vec2 a = Vec2{x: 10, y: 20};
    Vec2 b = Vec2{x: 3, y: 7};

    Vec2 sum = a + b;   // Vec2{13, 27}
    Vec2 diff = a - b;  // Vec2{7, 13}
    Vec2 prod = a * b;  // Vec2{30, 140}
    return 0;
}
```

## 比較演算子

比較演算子は `impl T for Eq` / `impl T for Ord` で定義します：

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

impl Point for Ord {
    operator bool <(Point other) {
        if (self.x != other.x) {
            return self.x < other.x;
        }
        return self.y < other.y;
    }
}

int main() {
    Point p1 = Point{x: 1, y: 2};
    Point p2 = Point{x: 3, y: 4};

    if (p1 == p2) { println("equal"); }
    if (p1 < p2) { println("p1 < p2"); }
    return 0;
}
```

> **Note:** `Eq`/`Ord`は組み込みinterfaceのため`impl T for Eq`構文が使えます。算術・ビット演算子は`impl T`構文で定義します。

## ビット演算子

```cm
struct Bits {
    int value;
}

impl Bits {
    operator Bits &(Bits other) {
        return Bits{value: self.value & other.value};
    }

    operator Bits |(Bits other) {
        return Bits{value: self.value | other.value};
    }

    operator Bits ^(Bits other) {
        return Bits{value: self.value ^ other.value};
    }

    operator Bits <<(Bits other) {
        return Bits{value: self.value << other.value};
    }

    operator Bits >>(Bits other) {
        return Bits{value: self.value >> other.value};
    }
}
```

## 複合代入演算子

二項演算子を定義すると、対応する複合代入演算子（`+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`）が自動的に使えるようになります。

```cm
int main() {
    Vec2 v = Vec2{x: 10, y: 20};
    v += Vec2{x: 5, y: 3};   // v = v + Vec2{5, 3} と同等
    v -= Vec2{x: 2, y: 1};   // v = v - Vec2{2, 1} と同等
    v *= Vec2{x: 3, y: 2};   // v = v * Vec2{3, 2} と同等
    return 0;
}
```

## with自動実装との違い

```cm
// 全フィールドの比較が自動生成される
struct Point with Eq + Ord {
    int x;
    int y;
}
```

> 📖 with自動実装の詳細は [with自動実装](with-keyword.html) を参照。

---

**前の章:** [with自動実装](with-keyword.html)  
**次の章:** [関数ポインタ](function-pointers.html)

---

**最終更新:** 2026-02-10

---

<!-- nav -->
← 前: [withキーワード（自動トレイト実装）](with-keyword.html) ｜ [目次](index.html) ｜ 次: [高度な機能編 - 関数ポインタ](function-pointers.html) →
