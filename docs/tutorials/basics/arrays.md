---
layout: default
title: 配列
parent: Tutorials
nav_order: 8
---

# 配列（Arrays）

Cm言語の配列は、C++スタイルの固定長配列をサポートします。

## 📋 目次

- [基本的な使い方](#基本的な使い方)
- [配列の初期化](#配列の初期化)
- [インデックスアクセス](#インデックスアクセス)
- [配列メソッド](#配列メソッド)
- [構造体配列](#構造体配列)
- [ポインタへの変換](#ポインタへの変換)
- [for-in構文](#for-in構文)

## 基本的な使い方

### 配列宣言

```cm
// 基本的な配列宣言
int[5] numbers;

// 宣言と初期化
int[3] values = {1, 2, 3};

// サイズ推論
int[] auto_size = {10, 20, 30};  // サイズ3と推論
```

## 配列の初期化

```cm
// ゼロ初期化
int[10] zeros;  // すべて0

// リテラル初期化
int[5] primes = {2, 3, 5, 7, 11};

// 部分初期化
int[5] partial = {1, 2};  // {1, 2, 0, 0, 0}
```

## インデックスアクセス

```cm
int[3] arr = {10, 20, 30};

// 読み取り
int first = arr[0];   // 10
int second = arr[1];  // 20

// 書き込み
arr[2] = 40;

// ループでアクセス
for (int i = 0; i < 3; i++) {
    println("{}", arr[i]);
}
```

## 配列メソッド

### サイズ取得

```cm
int[5] arr;

int size1 = arr.size();     // 5
int size2 = arr.len();      // 5
int size3 = arr.length();   // 5（全て同じ）
```

### 要素検索

```cm
int[5] numbers = {1, 2, 3, 4, 5};

// indexOf - 要素の位置を検索
int pos = numbers.indexOf(3);  // 2
int not_found = numbers.indexOf(10);  // -1

// includes - 要素が含まれているか
bool has_3 = numbers.includes(3);  // true
bool has_10 = numbers.includes(10);  // false

// contains（includesのエイリアス）
bool has_5 = numbers.contains(5);  // true
```

### 高階関数メソッド

```cm
int[5] numbers = {1, 2, 3, 4, 5};

// some - いずれかが条件を満たすか
bool has_even = numbers.some(|x| x % 2 == 0);  // true

// every - すべてが条件を満たすか
bool all_positive = numbers.every(|x| x > 0);  // true

// findIndex - 条件を満たす最初の要素のインデックス
int idx = numbers.findIndex(|x| x > 3);  // 3 (値4のインデックス)
```

## 構造体配列

```cm
struct Point {
    int x;
    int y;
}

// 構造体配列の宣言
Point[3] points;

// 初期化
points[0].x = 10;
points[0].y = 20;

points[1].x = 30;
points[1].y = 40;

// 構造体配列の初期化リスト
Point[2] pts = {
    Point(1, 2),
    Point(3, 4)
};
```

## ポインタへの変換

配列は自動的にポインタに変換されます（Array Decay）。

```cm
int[5] arr = {1, 2, 3, 4, 5};

// 配列→ポインタ変換
int* p = arr;  // arr[0]のアドレス

// ポインタ経由でアクセス
int first = *p;     // 1
int second = *(p + 1);  // 2
```

### 関数に配列を渡す

```cm
void print_array(int* data, int size) {
    for (int i = 0; i < size; i++) {
        println("{}", data[i]);
    }
}

int main() {
    int[5] numbers = {1, 2, 3, 4, 5};
    print_array(numbers, 5);  // 配列→ポインタ変換
    return 0;
}
```

## for-in構文

範囲ベースのforループで配列を反復処理できます。

```cm
int[5] numbers = {1, 2, 3, 4, 5};

// 型指定あり
for (int n in numbers) {
    println("{}", n);
}

// 型推論
for (n in numbers) {
    println("{}", n);
}

// 構造体配列
struct Point { int x; int y; }
Point[3] points = {Point(1, 2), Point(3, 4), Point(5, 6)};

for (Point p in points) {
    println("({}, {})", p.x, p.y);
}
```

## 多次元配列

```cm
// 2次元配列
int[3][4] matrix;

// 初期化
matrix[0][0] = 1;
matrix[0][1] = 2;

// ループでアクセス
for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 4; j++) {
        matrix[i][j] = i * 4 + j;
    }
}
```

## 実装状況

| バックエンド | 状態 |
|------------|------|
| インタプリタ | ✅ 完全対応 |
| LLVM Native | ✅ 完全対応 |
| WASM | ✅ 完全対応 |

## サンプルコード

完全なサンプルは以下を参照してください：

- `examples/arrays/basic_array.cm` - 基本的な配列操作
- `examples/arrays/struct_array.cm` - 構造体配列
- `tests/test_programs/array/` - テストケース

## 関連ドキュメント

- [ポインタ](pointers.md) - ポインタ操作
- [for-in構文](../guides/control-flow.md) - 範囲ベースループ
- [メソッド一覧](../guides/methods.md) - 配列メソッド詳細

---

**更新日:** 2024-12-16
