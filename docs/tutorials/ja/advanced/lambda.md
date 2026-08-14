---
title: ラムダ式
parent: Advanced
---

[English](../../en/advanced/lambda.html)

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
// 基本形: (型付き引数リスト) => { 文... } または (型付き引数リスト) => 式
int*(int) double_it = (int x) => { return x * 2; };

// 例: ブロックを省略した式形式（returnを書かない）
int*(int) triple = (int x) => x * 3;

int main() {
    println("{double_it(5)}");  // 10
    println("{triple(5)}");     // 15
    return 0;
}
```

---

## 使用例

### 変数への代入

```cm
// 関数ポインタ型 戻り値型*(引数型) の変数に代入する
int*(int) double_it = (int x) => { return x * 2; };
const int result = double_it(5);  // 10
```

### 高階関数への渡し

```cm
int[5] arr = [1, 2, 3, 4, 5];

// map でラムダ式を使用
int[] doubled = arr.map((int x) => x * 2);   // [2, 4, 6, 8, 10]

// filter でラムダ式を使用
int[] evens = arr.filter((int x) => x % 2 == 0);   // [2, 4]

// reduce でラムダ式を使用（初期値0から畳み込み）
int total = arr.reduce((int acc, int x) => acc + x, 0);   // 15
```

---

## 型推論

引数の型は推論されないため、必ず明示します（`(x) => ...` は構文エラー）。
一方、式形式のラムダの戻り値型は式から推論されます：

```cm
// 戻り値型は式 x * 2 から int と推論される
int*(int) double_it = (int x) => x * 2;
```

---

## 複数引数

```cm
int*(int, int) add = (int a, int b) => { return a + b; };

println("{add(3, 4)}");  // 7
```

---

## 戻り値なし

```cm
void*(int) print_it = (int x) => {
    println("Value: {x}");
};

print_it(42);  // "Value: 42"
```

---

## よくある使用パターン

### コールバック

```cm
void process(int*(int) callback, int value) {
    println("{callback(value)}");
}

int main() {
    process((int x) => x + 100, 5);  // 105
    return 0;
}
```

### ソートのカスタム比較

```cm
int[4] a = [3, 1, 4, 2];

// sortByに比較ラムダを渡して降順ソート（sort()は引数なしの昇順専用）
int[] desc = a.sortBy((int x, int y) => y - x);   // [4, 3, 2, 1]
```

---

## クロージャのキャプチャは値コピー（読み取り専用）

ラムダ式が外側の変数を参照すると、その変数は**値としてキャプチャ（コピー）**されます。
コピーへの書き込みは元の変数に反映されないため、キャプチャした変数への代入・複合代入・インクリメント/デクリメントはコンパイルエラーになります（構造体メンバや配列要素への書き込みも同様です）。

```cm
int x = 1;
const void*() f = () => {
    x = 42;  // エラー: Cannot assign to captured variable 'x' inside a closure
};
```

外側の変数を変更したい場合は、ポインタをキャプチャして間接的に書き込みます（ポインタ値のコピーは元の変数を指したままなので変更が伝播します）。

```cm
int x = 1;
int* px = &x;
const void*() f = () => { *px = 42; };  // OK: ポインタ経由の書き込みは伝播する
f();
println("{x}");  // 42
```

---

## 次のステップ

- [関数ポインタ](function-pointers.html) - 関数の詳細
- [スライス型](slices.html) - 高階関数の詳細
---

**最終更新:** 2026-02-08

---

<!-- nav -->
← 前: [高度な機能編 - 関数ポインタ](function-pointers.html) ｜ [目次](index.html) ｜ 次: [高度な機能編 - 文字列操作](strings.html) →
