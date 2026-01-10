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
    int i = identity(42);
    double d = identity(3.14);
    string s = identity("Hello");
    return 0;
}
```

## 型推論

```cm
<T> T max(T a, T b) {
    return a > b ? a : b;
}

int main() {
    int i = max(10, 20);
    double d = max(3.14, 2.71);
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
    Pair<int, string> p = make_pair(1, "one");
    return 0;
}
```

---

**前の章:** [typedef](typedef.html)  
**次の章:** [インターフェース](interfaces.html)