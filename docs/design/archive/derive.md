# 自動実装 (with) 設計

## 概要

`Debug`, `Clone` 等は **interface** として定義されています。`with` キーワードを使うと、コンパイラがこれらの interface を**自動実装**します。

```cpp
// 手動実装（通常の impl）
struct Point {
    int x;
    int y;
};

impl Debug for Point {
    String debug() {
        return "Point { x: " + to_string(this.x) + ", y: " + to_string(this.y) + " }";
    }
};

// 自動実装（with で省略）
struct Point with Debug {
    int x;
    int y;
};
// ↑ コンパイラが impl Debug for Point を自動生成
```

## 基本構文

```cpp
struct Point with Debug, Clone, Default {
    int x;
    int y;
};

void main() {
    Point p = Point(10, 20);
    println(p.debug());  // "Point { x: 10, y: 20 }"
    
    Point p2 = p.clone();
    Point p3 = Point::default();  // Point { x: 0, y: 0 }
}
```

## 組み込み interface

| interface | メソッド | 用途 |
|-----------|---------|------|
| `Debug` | `debug() -> String` | デバッグ出力 |
| `Clone` | `clone() -> Self` | 値のコピー |
| `Default` | `default() -> Self` | デフォルト値生成 |
| `Eq` | `eq(Self) -> bool` | 等価比較 |
| `Ord` | `cmp(Self) -> int` | 順序比較 |
| `Hash` | `hash() -> uint64` | ハッシュ値生成 |

### interface 定義（標準ライブラリ）

```cpp
// cm-core で定義済み
interface Debug {
    String debug();
};

interface Clone {
    Self clone();
};

interface Default {
    static Self default();
};

interface Eq {
    bool eq(Self other);
};
```

---

## 各自動実装の詳細

### Debug

```cpp
struct User with Debug {
    String name;
    int age;
};

// 生成される関数
String debug(User u) {
    return "User { name: \"" + u.name + "\", age: " + to_string(u.age) + " }";
}

// 使用例
User u = User("Alice", 30);
println(debug(u));  // "User { name: \"Alice\", age: 30 }"
```

### Clone

```cpp
struct Point with Clone {
    int x;
    int y;
};

// 生成される関数
Point clone(Point p) {
    return Point(p.x, p.y);
}
```

### Default

```cpp
struct Config with Default {
    int timeout;      // デフォルト: 0
    bool enabled;     // デフォルト: false
    String name;      // デフォルト: ""
};

// 生成される関数
Config Config::default() {
    return Config(0, false, "");
}

// カスタムデフォルト値
struct Config2 {
    int timeout = 30;    // 明示的デフォルト
    bool enabled = true;
};
```

### Eq

```cpp
struct Point with Eq {
    int x;
    int y;
};

// 生成される演算子
bool operator==(Point a, Point b) {
    return a.x == b.x && a.y == b.y;
}

bool operator!=(Point a, Point b) {
    return !(a == b);
}
```

---

## ネストした構造体

```cpp
struct Inner with Debug, Clone {
    int value;
};

struct Outer with Debug, Clone {
    Inner inner;
    String name;
};

// Debug出力
// "Outer { inner: Inner { value: 42 }, name: \"test\" }"
```

> **要件**: ネストした型も同じ自動実装を持っている必要がある

---

## 列挙型

```cpp
enum Color with Debug, Clone, Eq {
    Red,
    Green,
    Blue,
    Rgb(int, int, int),
};

void main() {
    Color c1 = Color::Red;
    Color c2 = Color::Rgb(255, 128, 0);
    
    println(debug(c1));  // "Red"
    println(debug(c2));  // "Rgb(255, 128, 0)"
    
    println(c1 == Color::Red);  // true
}
```

---

## バックエンドへの変換

### Rust

```cpp
// Cm
struct Point with Debug, Clone {
    int x;
    int y;
};

// → Rust
#[derive(Debug, Clone)]
struct Point { x: i64, y: i64 }
```

### TypeScript

```cpp
// Cm
struct Point with Debug {
    int x;
    int y;
};

// → TypeScript
interface Point { x: number; y: number; }

function debug_Point(p: Point): string {
    return `Point { x: ${p.x}, y: ${p.y} }`;
}
```

---

## 文法定義

```bnf
struct_def ::= 'struct' IDENT [ 'with' auto_impl_list ] '{' { field_def } '}'

auto_impl_list ::= IDENT { ',' IDENT }

enum_def ::= 'enum' IDENT [ 'with' auto_impl_list ] '{' { variant } '}'
```

---

## 予約語追加

`with` を予約語リストに追加:

```
async, await, break, const, continue, delete, else, export, extern,
false, for, if, impl, import, inline, interface, match, mutable, new,
null, private, return, static, struct, this, true, void, volatile, while, with
```

