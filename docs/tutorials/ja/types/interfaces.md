---
title: インターフェース
parent: Tutorials
---

[English](../../en/types/interfaces.html)

# 型システム編 - インターフェース

**難易度:** 🔴 上級  
**所要時間:** 30分

## 📚 この章で学ぶこと

- インターフェースの定義
- インターフェースの実装 (impl)
- 動的ディスパッチと抽象化
- private メソッドの活用

---

## インターフェースとは

インターフェースは、構造体が持つべきメソッドの「シグネチャ（名前、引数、戻り値）」だけを定義したものです。これにより、異なる構造体を同じように扱うことが可能になります。

```cm
interface Drawable {
    void draw();
}

interface Printable {
    void print();
}
```

---

## インターフェースの実装

`impl 構造体名 for インターフェース名` の形式でメソッドの実装を記述します。

```cm
struct Circle {
    int radius;
}

impl Circle for Drawable {
    void draw() {
        println("Drawing a circle with radius {}", self.radius);
    }
}

struct Square {
    int side;
}

impl Square for Drawable {
    void draw() {
        println("Drawing a square with side {}", self.side);
    }
}
```

### self の扱い

インターフェース内での `self` は、対象となる構造体へのポインタ（例: `*Circle`）として扱われます。したがって、`self.field` でフィールドにアクセスしたり、値を変更したりすることができます。

---

## インターフェースの使用

### メソッド呼び出し

インターフェースを実装した構造体のインスタンスに対して、そのメソッドを呼び出せます。

```cm
int main() {
    Circle c;
    c.radius = 10;
    c.draw();  // Circleのdrawが呼ばれる

    Square s;
    s.side = 5;
    s.draw();  // Squareのdrawが呼ばれる

    // 変更しない構造体はconstで宣言すべき
    // const Circle c2 = {radius: 15};  // constの場合

    return 0;
}
```

---

## 複数のインターフェース実装

1つの構造体に対して、複数のインターフェースを実装できます。

```cm
impl Circle for Printable {
    void print() {
        println("Circle(radius={})", self.radius);
    }
}

int main() {
    Circle c;
    c.radius = 10;
    c.draw();
    c.print();
    return 0;
}
```

---

## private メソッド

`impl` ブロック内でのみ使用できる補助メソッドを定義できます。これはインターフェースのシグネチャには含まれませんが、共通のロジックをまとめるのに便利です。

```cm
interface Calculator {
    int calculate(int x);
}

struct MyCalc {
    int base;
}

impl MyCalc for Calculator {
    // この impl ブロック内でのみ呼び出し可能
    private int helper(int n) {
        return n * 2;
    }
    
    int calculate(int x) {
        // self 経由で private メソッドを呼び出し
        return self.helper(x) + self.base;
    }
}
```

---

## よくある間違い

### ❌ シグネチャの不一致

インターフェースで定義されたメソッドと、実装するメソッドの型や引数が異なるとエラーになります。

```cm
interface Foo {
    void bar(int x);
}

struct S {}

impl S for Foo {
    // void bar(string s) { ... } // エラー: 型が一致しない
    void bar(int x) { ... }      // OK
}
```

### ❌ 実装漏れ

インターフェースに含まれる全てのメソッドを実装する必要があります。

---

## 練習問題

### 問題1: 面積の抽象化
`Shape` インターフェースを作成し、`area()` メソッドを定義してください。次に `Rectangle` と `Circle` 構造体にそれを実装させてください。

<details>
<summary>解答例</summary>

```cm
interface Shape {
    double area();
}

struct Rectangle {
    double w, h;
}

impl Rectangle for Shape {
    double area() { return self.w * self.h; }
}

struct Circle {
    double radius;
}

impl Circle for Shape {
    double area() { return 3.14159 * self.radius * self.radius; }
}
```
</details>

---

## 次のステップ

✅ インターフェースの定義ができる  
✅ インターフェースの実装ができる  
✅ self ポインタの使い方がわかった  
⏭️ 次は [型制約](constraints.html) を学びましょう

## 関連リンク

- [構造体](structs.html) - 直接実装 (Inherent Impl)
- [型制約](constraints.html) - インターフェースを使った制約
- [ジェネリクス](generics.html) - 抽象的な型の扱い

---

**前の章:** [ジェネリクス](generics.html)  
**次の章:** [型制約](constraints.html)