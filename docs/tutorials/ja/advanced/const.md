---
title: 定数と定数式
parent: Advanced
nav_order: 5
---

# 定数と定数式 (const)

Cmでは`const`キーワードを使用してコンパイル時定数を定義できます。v0.12.0以降、配列サイズに定数変数や定数式を使用できるようになりました。

## 基本的な使い方

### const変数の定義

```cm
const int MAX_SIZE = 100;
const double PI = 3.14159;
const bool DEBUG = true;

int main() {
    println("MAX_SIZE = {MAX_SIZE}");  // MAX_SIZE = 100
    println("PI = {PI}");              // PI = 3.14159
    return 0;
}
```

### 配列サイズにconstを使用

```cm
const int SIZE = 10;
const int DOUBLE_SIZE = SIZE * 2;  // 定数式も評価可能

int main() {
    int[SIZE] arr1;           // int[10]
    int[DOUBLE_SIZE] arr2;    // int[20]
    
    println("arr1.size() = {arr1.size()}");  // 10
    println("arr2.size() = {arr2.size()}");  // 20
    
    return 0;
}
```

## 定数式

コンパイル時に評価される式を定数式と呼びます。

### サポートされる演算

| 種類 | 演算子 |
|-----|--------|
| 算術演算 | `+`, `-`, `*`, `/`, `%` |
| ビット演算 | `&`, `\|`, `^`, `<<`, `>>` |
| 比較演算 | `==`, `!=`, `<`, `>`, `<=`, `>=` |
| 論理演算 | `&&`, `\|\|`, `!` |
| 三項演算子 | `? :` |

### 例

```cm
const int A = 10;
const int B = 20;

// 算術演算
const int SUM = A + B;        // 30
const int PRODUCT = A * B;    // 200

// ビット演算
const int SHIFTED = A << 2;   // 40

// 三項演算子
const int MAX = A > B ? A : B;  // 20

// 配列サイズに使用
int[SUM] data;  // int[30]
```

## 制限事項

現在の実装では以下の制限があります：

- グローバルスコープの`const`変数のみ対応
- 関数呼び出しは定数式に含められない
- `constexpr`関数は今後のバージョンで実装予定

## 関連項目

- [変数](../basics/variables.md) - 変数の基本
- [配列](../basics/arrays.md) - 配列の使い方
