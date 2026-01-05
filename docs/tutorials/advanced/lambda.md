---
layout: default
title: ラムダ式
parent: Advanced
nav_order: 7
---

# ラムダ式

**学習目標:** Cm言語のラムダ式（無名関数）の使い方を学びます。  
**所要時間:** 15分  
**難易度:** 🟡 中級

---

## 概要

ラムダ式は関数をその場で定義できる構文です。

---

## 基本構文

```cm
// 基本形
(引数) => { 処理 }

// 例
(int x) => { return x * 2; }
```

---

## 使用例

### 変数への代入

```cm
// 関数ポインタに代入
func<int, int> double_it = (int x) => { return x * 2; };

int result = double_it(5);  // 10
```

### 高階関数への渡し

```cm
int[] numbers = [1, 2, 3, 4, 5];

// map でラムダ式を使用
int[] doubled = numbers.map((int x) => { return x * 2; });
// [2, 4, 6, 8, 10]

// filter でラムダ式を使用
int[] evens = numbers.filter((int x) => { return x % 2 == 0; });
// [2, 4]
```

---

## 型推論

引数の型は推論されることがあります：

```cm
int[] arr = [1, 2, 3];

// 型推論により x は int
int[] result = arr.map((x) => { return x + 1; });
```

---

## 複数引数

```cm
func<int, int, int> add = (int a, int b) => { return a + b; };

int sum = add(3, 4);  // 7
```

---

## 戻り値なし

```cm
func<int, void> print_it = (int x) => {
    println("Value: {}", x);
};

print_it(42);  // "Value: 42"
```

---

## よくある使用パターン

### コールバック

```cm
void process_async(func<int, void> callback) {
    int result = 42;
    callback(result);
}

process_async((int x) => {
    println("Got: {}", x);
});
```

### ソートのカスタム比較

```cm
struct Person {
    string name;
    int age;
}

Person[] people = [...];

// 年齢でソート
people.sort((Person a, Person b) => {
    return a.age - b.age;
});
```

---

## 次のステップ

- [関数ポインタ](function-pointers.md) - 関数の詳細
- [スライス型](slices.md) - 高階関数の詳細
