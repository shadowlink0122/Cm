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

スライス型 `T[]` は可変長の配列を表し、push/pop等の変更操作と高階関数を提供します。

---

## スライスの作成

```cm
int main() {
    // 空のスライスを作成してpush
    int[] xs = [];
    xs.push(10);
    xs.push(20);

    // リテラルで初期化
    int[] ys = [1, 2, 3];

    // 固定長配列から変換（要素はコピーされる）
    int[5] arr = [1, 2, 3, 4, 5];
    int[] zs = arr;

    // 部分スライス
    int[] mid = arr[1:4];   // [2, 3, 4]
    int[] head = arr[:2];   // [1, 2]
    int[] tail = arr[3:];   // [4, 5]
    return 0;
}
```

---

## 要素のアクセスと書き込み

```cm
import std::io::println;

int main() {
    int[] xs = [1, 2, 3];
    int a = xs[0];   // 読み出し
    xs[1] = 99;      // 書き込み
    println("x1={xs[1]}");
    return 0;
}
```

---

## スライス専用メソッド

固定長配列のメソッド（[配列](../basics/arrays.html)のメソッド一覧参照）に加えて、スライスでは以下が使用できます。

| メソッド | 戻り値 | 説明 |
|---|---|---|
| `.push(v)` | `void` | 末尾に要素を追加 |
| `.pop()` | `T` | 末尾の要素を取り出して削除 |
| `.remove(i)` / `.delete(i)` | `void` | 位置iの要素を削除 |
| `.clear()` | `void` | 全要素を削除 |
| `.cap()` / `.capacity()` | `int` | 確保済み容量 |

```cm
import std::io::println;

int main() {
    int[] xs = [];
    xs.push(10);
    xs.push(20);
    xs.push(30);

    int p = xs.pop();      // 30
    xs.remove(0);          // [20]
    println("len={xs.len()} cap={xs.cap()}");
    xs.clear();            // []
    return 0;
}
```

要素型は任意です（プリミティブ・string・構造体・ユニオン型・ネストしたスライス `T[][]`）。
構造体・ユニオン要素は値としてコピー格納されるため、push後に元の変数を変更してもスライスの内容は変わりません。

---

## 高階関数

`map` / `filter` / `reduce` / `find` / `findIndex` / `some` / `every` / `forEach` / `sort` / `sortBy` / `reverse` / `first` / `last` が使用できます（固定長配列と共通）。

```cm
import std::io::println;

bool is_even(int x) {
    return x % 2 == 0;
}

int main() {
    int[] nums = [1, 2, 3, 4, 5];

    int[] doubled = nums.map((int x) => { return x * 2; });    // [2, 4, 6, 8, 10]
    int[] evens = nums.filter(is_even);                        // [2, 4]
    int total = nums.reduce((int acc, int x) => { return acc + x; }, 0);  // 15
    int found = nums.find(is_even);                            // 2
    bool has_even = nums.some(is_even);                        // true
    bool all_even = nums.every(is_even);                       // false
    println("total={total} found={found}");
    return 0;
}
```

### チェーン呼び出し

高階関数は連続して呼び出せます（ラムダの引数には型注釈が必要です。`reduce` の引数順は `reduce(関数, 初期値)` です）：

```cm
int[10] numbers = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];

int total = numbers
    .filter((int x) => { return x % 2 == 0; })  // [2, 4, 6, 8, 10]
    .map((int x) => { return x * x; })          // [4, 16, 36, 64, 100]
    .reduce((int acc, int x) => { return acc + x; }, 0);
// 220
```

文字列補間内での関数引数付きメソッド呼び出し（`{xs.some(fn)}` 等）もそのまま利用できます。

---

## 構造体のスライス

```cm
struct Person {
    string name;
    int age;
}

Person[] people = [
    Person { name: "Alice", age: 30 },
    Person { name: "Bob", age: 25 },
    Person { name: "Carol", age: 35 }
];

// 30歳以上の名前を取得
string[] names = people
    .filter((Person p) => { return p.age >= 30; })
    .map((Person p) => { return p.name; });
// ["Alice", "Carol"]
```

> バックエンド注記: 構造体スライスの高階メソッド（filter/map/indexOf/includes/pop）や、グローバル・構造体メンバのスライスへのミューテーションは js/ts バックエンドで動作します（JS配列に対応）。native/jit では基本操作（push/len/添字/for-in/引数・戻り値/要素フィールド更新）に限られます。可変長データを扱うWeb開発等では `--target=ts` を使ってください。

---

## ユニオン型のスライス

ユニオン型を要素にすると、異なる型の値を1つのスライスに混在できます。
要素の実型は静的には決まらないため、取り出しは `as` で行います（誤った型での取り出しは実行時エラーで停止します）。

```cm
import std::io::println;

typedef Value = int | string;

int main() {
    Value[] vals = [];
    vals.push(42 as Value);
    vals.push("hello" as Value);

    int n = vals[0] as int;
    string s = vals[1] as string;
    println("n={n} s={s}");
    return 0;
}
```

---

## 固定長配列をスライス引数へ渡す

固定長配列は、スライス型（`T[]`）のパラメータへそのまま渡せます。渡す際に配列の内容がヒープ上のスライスへコピーされるため、呼び出し先での`push`や要素代入は呼び出し元の配列へは反映されません。

```cm
int sum(int[] xs) {
    int s = 0;
    for (int i = 0; i < xs.len(); i++) {
        s = s + xs[i];
    }
    return s;
}

int main() {
    int[3] fixed = [1, 2, 3];
    println("{sum(fixed)}");
    // 6（コピーが渡るため、sum内で変更しても fixed は変わらない）
    return 0;
}
```

## 次のステップ

- [配列](../basics/arrays.html) - 配列の基礎とメソッド一覧
- [ラムダ式](lambda.html) - 無名関数の詳細

---

**最終更新:** 2026-07-12

---

<!-- nav -->
← 前: [高度な機能編 - 文字列操作](strings.html) ｜ [目次](index.html) ｜ 次: [FFI（Foreign Function Interface）](ffi.html) →
