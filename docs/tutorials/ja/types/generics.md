---
title: ジェネリクス
parent: Tutorials
---

[English](../../en/types/generics.html)

# 型システム編 - ジェネリクス

**難易度:** 🔴 上級  
**所要時間:** 35分

## ジェネリック関数

```cm
<T> T identity(T value) {
    return value;
}

int main() {
    const int i = identity(42);  // 結果は不変
    const double d = identity(3.14);
    const string s = identity("Hello");
    return 0;
}
```

## 型推論

```cm
<T> T max(T a, T b) {
    return a > b ? a : b;
}

int main() {
    const int i = max(10, 20);  // 結果は不変
    const double d = max(3.14, 2.71);
    return 0;
}
```

## ジェネリック構造体

```cm
struct Box<T> {
    T value;
}

int main() {
    Box<int> int_box;
    int_box.value = 42;
    
    Box<string> str_box;
    str_box.value = "Hello";
    
    return 0;
}
```

## 複数の型パラメータ

```cm
struct Pair<T, U> {
    T first;
    U second;
}

<T, U> Pair<T, U> make_pair(T first, U second) {
    Pair<T, U> p;
    p.first = first;
    p.second = second;
    return p;
}

int main() {
    const Pair<int, string> p = make_pair(1, "one");  // 結果は不変
    return 0;
}
```

## ポインタ・配列引数からの型推論

型パラメータが引数のポインタ（`T*`）・配列（`T[]`）の内側に現れる場合も、実引数の対応する部分型から`T`が推論されます。

```cm
<T> void swap(T* a, T* b) {
    T tmp = *a;
    *a = *b;
    *b = tmp;
}

<T> T first(T[] xs) {
    return xs[0];
}

int main() {
    int a = 1;
    int b = 2;
    swap(&a, &b);           // T=int（&a・&bのint*からintを推論）
    println("{a} {b}");     // 2 1

    int[] xs = [10, 20, 30];
    println("{first(xs)}"); // 10
    return 0;
}
```

## ジェネリックコレクションのRAII

ジェネリックコレクション（`Vector<T>`など）は、`self()`コンストラクタと`~self()`デストラクタを持ちます。

```cm
import std::collections::vector::*;

struct TrackedObject {
    int id;
}

impl TrackedObject {
    ~self() {
        println("~TrackedObject({self.id})");
    }
}

int main() {
    {
        Vector<TrackedObject> objects();  // コンストラクタ呼び出し
        objects.push(TrackedObject { id: 100 });
        objects.push(TrackedObject { id: 200 });
        // スコープ終了時:
        // 1. ~Vector()が呼ばれる
        // 2. 各要素の~TrackedObject()が呼ばれる
    }
    return 0;
}
```

**出力:**
```
~TrackedObject(100)
~TrackedObject(200)
```

---

**前の章:** [typedef](typedef.html)  
**次の章:** [インターフェース](interfaces.html)
---

**最終更新:** 2026-02-08

---

<!-- nav -->
← 前: [型システム編 - typedef型エイリアス](typedef.html) ｜ [目次](index.html) ｜ 次: [型システム編 - インターフェース](interfaces.html) →

## ジェネリック構造体のリテラル構築

宣言型から型引数が推論されるため、リテラルの型名には型引数を書かずに構築できます（v0.17.0）。

```cm
struct Box<T> { T v; }
struct Pair<A, B> { A first; B second; }

Box<int> b = Box{v: 7};                            // 裸名リテラル（推論）
Pair<int, string> p = {first: 7, second: "seven"}; // 無名リテラル（推論）
```

明示型引数付きリテラル（`Box<int>{v: 7}`）は比較演算子との構文曖昧性のため未対応です。フィールド個別代入（`Box<int> b; b.v = 7;`）も従来どおり使えます。

## ネストした特殊化を型引数に持つ構造体

ジェネリック特殊化を別のジェネリックの型引数として渡せます。内側リテラルの型も宣言型から推論されます。

```cm
struct Box<T> { T v; }
struct Pair<A, B> { A first; B second; }

Pair<Box<int>, Box<string>> nested = Pair { first: Box { v: 42 }, second: Box { v: "deep" } };
println("{nested.first.v} {nested.second.v}");  // 42 deep

Box<Pair<int, string>> outer = Box { v: Pair { first: 5, second: "inner" } };
println("{outer.v.first} {outer.v.second}");    // 5 inner
```
