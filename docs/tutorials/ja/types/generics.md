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
