[English](generics.en.html)

# 言語ガイド - ジェネリクス

## ジェネリック関数

```cm
<T: Ord> T max(T a, T b) {
    return a > b ? a : b;
}
```

## ジェネリック構造体

```cm
struct Box<T> {
    T value;
}
```

## 型制約

`where` 句を使用して複雑な制約を指定できます。

```cm
<T> void process(T val) where T: Eq + Clone {
    // ...
}
```