---
title: スライス型
parent: Advanced
---

[English](../../en/advanced/slices.html)

# スライス型

**学習目標:** 動的スライス型と高階関数の使い方を学びます。  
**所要時間:** 20分  
**難易度:** 🟡 中級

---

## 概要

スライス型 `T[]` は動的サイズの配列参照を表し、強力な高階関数を提供します。

---

## 基本構文

```cm
// 配列からスライスを作成

// 部分スライス
```

---

## 多次元スライス

```cm
// 2次元スライス
    [1, 2, 3],
    [4, 5, 6],
    [7, 8, 9]
];

```

---

## 高階関数

### map - 変換

各要素に関数を適用して新しい配列を返します。

```cm
// [2, 4, 6, 8, 10]
```

### filter - 絞り込み

条件を満たす要素のみを抽出します。

```cm
// [2, 4]
```

### reduce - 集約

全要素を1つの値に集約します。

```cm
// 15
```

### find - 検索

条件を満たす最初の要素を返します。

```cm
// 4
```

### some - 存在確認

条件を満たす要素が1つでもあればtrue。

```cm
bool has_even = nums.some((x) => { return x % 2 == 0; });
// true
```

### every - 全件確認

全要素が条件を満たせばtrue。

```cm
bool all_even = nums.every((x) => { return x % 2 == 0; });
// true
```

---

## チェーン呼び出し

高階関数は連続して呼び出せます：

```cm

    .filter((x) => { return x % 2 == 0; })  // [2, 4, 6, 8, 10]
    .map((x) => { return x * x; })           // [4, 16, 36, 64, 100]
    .reduce(0, (acc, x) => { return acc + x; });
// 220
```

---

## 構造体のスライス

```cm
}

Person[] people = [
    Person { name: "Alice", age: 30 },
    Person { name: "Bob", age: 25 },
    Person { name: "Carol", age: 35 }
];

// 30歳以上の名前を取得
    .filter((p) => { return p.age >= 30; })
    .map((p) => { return p.name; });
// ["Alice", "Carol"]
```

---

## 次のステップ

- [配列](../basics/arrays.html) - 配列の基礎
- [ラムダ式](lambda.html) - 無名関数の詳細
---

**最終更新:** 2026-02-08

---

<!-- nav -->
← 前: [高度な機能編 - 文字列操作](strings.html) ｜ [目次](index.html) ｜ 次: [FFI（Foreign Function Interface）](ffi.html) →
