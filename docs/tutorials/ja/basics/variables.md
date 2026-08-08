---
title: 変数と型
parent: Tutorials
---

[English](../../en/basics/variables.html)

# 基本編 - 変数と型

**難易度:** 🟢 初級  
**所要時間:** 20分

## 📚 この章で学ぶこと

- Cm言語の型システム
- プリミティブ型の種類
- 変数の宣言と初期化
- const/static修飾子

---

## プリミティブ型

### 整数型

```cm
// 符号付き整数
tiny t = 127;         // 8bit: -128 〜 127
short s = 32767;      // 16bit: -32768 〜 32767
int i = 42;           // 32bit: -2^31 〜 2^31-1
long l = 1000000;     // 64bit: -2^63 〜 2^63-1

// 符号なし整数
utiny ut = 255;       // 8bit: 0 〜 255
ushort us = 65535;    // 16bit: 0 〜 65535
uint ui = 100;        // 32bit: 0 〜 2^32-1
ulong ul = 2000000;   // 64bit: 0 〜 2^64-1
```

### 浮動小数点型

```cm
float f = 3.14;       // 32bit単精度
double d = 3.14159;   // 64bit倍精度

// v0.10.0で追加：非負浮動小数点
ufloat uf = 2.5;      // 32bit非負
udouble ud = 10.0;    // 64bit非負
```

`ufloat`/`udouble`への負リテラルの代入は警告（`--strict`ではエラー）になります。実行時に負へ転じる演算までは検査されないため、非負の保証が必要な箇所では値の検証を併用してください。

### その他の型

```cm
bool b = true;        // 真偽値: true/false
char c = 'A';         // 文字
string str = "Hello"; // 文字列
```

---

## 変数の宣言

### 基本的な宣言

```cm
int x;           // 宣言のみ
int y = 10;      // 宣言と初期化
```

### 複数変数の宣言

```cm
int a, b, c;
int x = 1;
int y = 2;
int z = 3;
```

**型推論（auto / var）:** 初期化式から型を推論する場合は `auto`（または別名の `var`）を使えます。推論に頼らず型を明示するスタイルも従来どおり使えます。

```cm
auto x = 42;        // int と推論
var name = "Cm";    // string と推論（var は auto の別名）
auto pi = 3.14;     // double と推論
```

---

## const修飾子（v0.11.0以降で必須）

**重要な変更（v0.11.0）:** `const`キーワードは不変変数に対して**必須**となりました。`const`のない変数はデフォルトで可変です。

```cm
// v0.11.0以降：不変変数には明示的にconstが必要
const int MAX_SIZE = 100;
const double PI = 3.14159;
const string GREETING = "Hello";

// MAX_SIZE = 200;  // エラー: constは変更不可
```

### const vs 可変変数

```cm
// 可変変数（デフォルト）
int counter = 10;
counter = 20;      // OK：再代入可能

// 不変変数（constが必須）
const int constant = 10;
// constant = 20; // エラー: const変数は変更不可

// コンパイラは変数がconstになるべきか警告を出します
int value = 42;
// 'value'が変更されない場合、コンパイラが提案：「'value'をconstにすることを検討してください」
```

---

## static変数

関数内で状態を保持する静的変数です。

```cm
void counter() {
    static int count = 0;
    count++;
    println("Count: {count}");
}

int main() {
    counter();  // "Count: 1"
    counter();  // "Count: 2"
    counter();  // "Count: 3"
    return 0;
}
```

### static変数の特徴

1. **初回のみ初期化** - 初めて実行される時だけ初期化
2. **値が保持される** - 関数を抜けても値が残る
3. **関数内スコープ** - 関数の外からはアクセス不可

---

## 型変換

### 暗黙的な型変換

値を保存する拡大変換（`int→long`・`short→int`・`int→double`・`float→double`等）は暗黙に行えます。

```cm
int i = 10;
double d = i;    // int → double（OK: 拡大変換）
long l = i;      // int → long（OK: 拡大変換）
```

情報を失いうる縮小変換（`double→int`・`long→int`・`int→short`等）や符号解釈が変わる変換（`int→uint`等）は警告になります（`check/lint --strict` ではエラー）。
宣言型に適合するリテラル（`short s = 5;`・`float f = 2.5;`）は例外として無診断です。

```cm
double pi = 3.14;
// int x = pi;   // double → int は縮小変換の警告（asの付与を提案される）
```

### 明示的な型変換（キャスト）

縮小の意図があるときは `as` で明示します。

```cm
double pi = 3.14159;
int truncated = pi as int;  // 3
```

---

## 型のサイズ

| 型 | サイズ | 範囲 |
|----|--------|------|
| `tiny` | 8bit | -128 〜 127 |
| `utiny` | 8bit | 0 〜 255 |
| `short` | 16bit | -32768 〜 32767 |
| `ushort` | 16bit | 0 〜 65535 |
| `int` | 32bit | -2^31 〜 2^31-1 |
| `uint` | 32bit | 0 〜 2^32-1 |
| `long` | 64bit | -2^63 〜 2^63-1 |
| `ulong` | 64bit | 0 〜 2^64-1 |
| `float` | 32bit | IEEE 754単精度 |
| `double` | 64bit | IEEE 754倍精度 |
| `ufloat` | 32bit | 非負単精度 |
| `udouble` | 64bit | 非負倍精度 |
| `bool` | 8bit | true/false |
| `char` | 8bit | ASCII文字 |

---

## よくある間違い

### ❌ 未初期化変数の使用

```cm
int x;
println("{x}");  // 警告: 初期化されていない可能性
```

### ❌ 型の不一致

```cm
int x = "Hello";  // エラー: 型が一致しない
```

### ❌ constの変更

```cm
const int MAX = 100;
MAX = 200;  // エラー: const変数は変更不可
```

---

## 練習問題

### 問題1: 型の選択
以下の値に最適な型を選んでください：

1. 年齢（0〜150）
2. 世界人口（約80億）
3. 円周率
4. ログインフラグ（成功/失敗）

<details>
<summary>解答例</summary>

```cm
utiny age = 25;           // 0〜255で十分
ulong population = 8000000000;  // 大きな数値
double pi = 3.14159;      // 高精度
bool login_success = true;  // 真偽値
```
</details>

### 問題2: カウンター
関数を呼ぶたびにカウントアップする`counter()`関数を実装してください。

<details>
<summary>解答例</summary>

```cm
void counter() {
    static int count = 0;
    count++;
    println("Count: {count}");
}

int main() {
    counter();  // 1
    counter();  // 2
    counter();  // 3
    return 0;
}
```
</details>

---

## 次のステップ

✅ プリミティブ型が理解できた  
✅ 変数の宣言と初期化ができる  
✅ const/staticの使い方がわかった  
⏭️ 次は [演算子](operators.html) を学びましょう

## 関連リンク

- [構造体](../types/structs.html)
- [typedef型エイリアス](../types/typedef.html)
- [ジェネリクス](../types/generics.html)

---

**前の章:** [Hello, World!](hello-world.html)  
**次の章:** [演算子](operators.html)

---

**最終更新:** 2026-02-08

---

<!-- nav -->
← 前: [基本編 - Hello, World!](hello-world.html) ｜ [目次](index.html) ｜ 次: [基本編 - 演算子](operators.html) →
