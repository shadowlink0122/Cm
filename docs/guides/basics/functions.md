# 言語ガイド - 関数

## 基本的な定義

```cm
int add(int a, int b) {
    return a + b;
}
```

## オーバーロード

`overload` キーワードを使用して、同じ名前で異なる引数を持つ関数を定義できます。

```cm
overload int add(int a, int b) { return a + b; }
overload double add(double a, double b) { return a + b; }
```

## ジェネリック関数

```cm
<T> T identity(T x) {
    return x;
}
```
