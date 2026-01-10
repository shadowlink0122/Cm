---
title: 構造体
parent: Tutorials
---

[English](../../en/types/structs.html)

# 型システム編 - 構造体

**難易度:** 🟡 中級  
**所要時間:** 30分

## 📚 この章で学ぶこと

- 構造体の定義と使い方
- フィールドアクセス
- コンストラクタの実装
- ネストした構造体
- 構造体配列

---

## 構造体の定義

### 基本的な定義

```cm
struct Point {
    int x;
    int y;
}

struct Rectangle {
    int width;
    int height;
    string color;
}

struct Person {
    string name;
    int age;
    double height;
}
```

### 型のサイズ

構造体のサイズは、全フィールドのサイズの合計です。

```cm
struct Point {
    int x;    // 4 bytes
    int y;    // 4 bytes
}
// 合計: 8 bytes
```

---

## 構造体の初期化

### フィールド初期化

```cm
Point p1;
p1.x = 10;
p1.y = 20;

println("Point: ({}, {})", p1.x, p1.y);
```

### デフォルト値

```cm
struct Config {
    int timeout;
    bool debug;
}

int main() {
    Config cfg;
    // 未初期化フィールドは不定値
    cfg.timeout = 30;
    cfg.debug = false;
    return 0;
}
```

**注意:** 現在、構造体リテラル初期化（`Rectangle{100, 50, "blue"}`）は未実装です。

---

## コンストラクタ

### デフォルトコンストラクタ

```cm
struct Point {
    int x;
    int y;
}

impl Point {
    self() {
        self.x = 0;
        self.y = 0;
    }
}

int main() {
    Point origin;  // (0, 0)で初期化
    println("({}, {})", origin.x, origin.y);
    return 0;
}
```

### 引数付きコンストラクタ

```cm
impl Point {
    overload self(int x, int y) {
        self.x = x;
        self.y = y;
    }
}

int main() {
    Point p1;           // デフォルト: (0, 0)
    Point p2(10, 20);   // 引数付き: (10, 20)
    return 0;
}
```

### 複数のコンストラクタ

```cm
struct Rectangle {
    int width;
    int height;
}

impl Rectangle {
    // デフォルト: 1x1
    self() {
        self.width = 1;
        self.height = 1;
    }
    
    // 正方形
    overload self(int size) {
        self.width = size;
        self.height = size;
    }
    
    // 長方形
    overload self(int w, int h) {
        self.width = w;
        self.height = h;
    }
}

int main() {
    Rectangle r1;        // 1x1
    Rectangle r2(5);     // 5x5
    Rectangle r3(3, 4);  // 3x4
    return 0;
}
```

---

## メソッドの実装

### 直接実装 (Inherent Impl)

インターフェースを使わずに、構造体に直接メソッドを定義できます。
`self` は構造体へのポインタとして扱われるため、フィールドの値を変更できます。

```cm
struct Point {
    int x;
    int y;
}

impl Point {
    // コンストラクタ
    self(int x, int y) {
        this.x = x;
        this.y = y;
    }

    // メソッド（selfは自動的に *Point 型になる）
    void move(int dx, int dy) {
        // self.x は (*self).x と同等（自動デリファレンス）
        self.x += dx;
        self.y += dy;
    }

    void print() {
        println("Point({}, {})", self.x, self.y);
    }
}

int main() {
    Point p(10, 20);
    p.print();      // Point(10, 20)
    
    p.move(5, 5);   // 状態を変更
    p.print();      // Point(15, 25)
    
    return 0;
}
```

### インターフェース実装

特定の振る舞いを共有したい場合は、インターフェースを使用します。

```cm
interface Printable {
    void print();
}

impl Point for Printable {
    void print() {
        println("Point({}, {})", self.x, self.y);
    }
}
```

---

## ネストした構造体

### 構造体フィールド

```cm
struct Point {
    int x;
    int y;
}

struct Line {
    Point start;
    Point end;
}

int main() {
    Line line;
    line.start.x = 0;
    line.start.y = 0;
    line.end.x = 100;
    line.end.y = 100;
    
    println("Line from ({}, {}) to ({}, {})",
        line.start.x, line.start.y,
        line.end.x, line.end.y);
    
    return 0;
}
```

### ネストした初期化

```cm
struct Point {
    int x;
    int y;
}

struct Circle {
    Point center;
    double radius;
}

int main() {
    Circle c;
    c.center.x = 50;
    c.center.y = 50;
    c.radius = 10.0;
    
    return 0;
}
```

---

## 構造体配列

```cm
struct Point {
    int x;
    int y;
}

int main() {
    Point[3] points;
    
    points[0].x = 0;
    points[0].y = 0;
    
    points[1].x = 10;
    points[1].y = 20;
    
    points[2].x = 30;
    points[2].y = 40;
    
    // 配列のループ
    for (int i = 0; i < 3; i++) {
        println("Point[{}]: ({}, {})", 
            i, points[i].x, points[i].y);
    }
    
    return 0;
}
```

---

## 構造体のコピー

### 値渡し

```cm
void modify(Point p) {
    p.x = 100;  // ローカルコピーを変更
}

int main() {
    Point p1(10, 20);
    modify(p1);
    println("{}", p1.x);  // 10（変更されない）
    return 0;
}
```

### ポインタ渡し

```cm
void modify(Point* p) {
    (*p).x = 100;  // 元のオブジェクトを変更
}

int main() {
    Point p1(10, 20);
    modify(&p1);
    println("{}", p1.x);  // 100（変更される）
    return 0;
}
```

---

## よくある間違い

### ❌ フィールド名の誤り

```cm
struct Point {
    int x;
    int y;
}

Point p;
// p.z = 10;  // エラー: フィールドzは存在しない
```

### ❌ 未初期化フィールドの使用

```cm
Point p;
// println("{}", p.x);  // 警告: 未初期化の可能性
```

### ❌ 構造体の直接比較

```cm
Point p1(10, 20);
Point p2(10, 20);
// if (p1 == p2) { }  // エラー: ==演算子が未定義
// 解決策: with Eq を使うか、operator==を実装
```

---

## 練習問題

### 問題1: 円の構造体
円を表す構造体を作成し、面積を計算するメソッドを実装してください。

<details>
<summary>解答例</summary>

```cm
struct Circle {
    double x;
    double y;
    double radius;
}

interface Shape {
    double area();
}

impl Circle for Shape {
    double area() {
        const double PI = 3.14159;
        return PI * self.radius * self.radius;
    }
}

int main() {
    Circle c;
    c.x = 0.0;
    c.y = 0.0;
    c.radius = 5.0;
    
    println("Area: {:.2}", c.area());
    return 0;
}
```
</details>

---

## 次のステップ

✅ 構造体の定義と初期化ができる  
✅ コンストラクタが実装できる  
✅ ネストした構造体が使える  
⏭️ 次は [Enum型](enums.html) を学びましょう

## 関連リンク

- [インターフェース](interfaces.html) - メソッド定義
- [ジェネリクス](generics.html) - ジェネリック構造体
- [with自動実装](../advanced/with-keyword.html) - 自動トレイト実装

---

**前の章:** [関数](../basics/functions.html)  
**次の章:** [Enum型](enums.html)
