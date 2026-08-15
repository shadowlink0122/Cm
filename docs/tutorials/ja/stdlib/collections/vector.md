---
title: Vector
---

# std::collections::vector - 動的配列

`Vector<T>` はジェネリックな動的配列です。任意の型を格納でき、ネストも可能です。

> **対応バックエンド:** Native (LLVM) のみ

**最終更新:** 2026-02-08

---

## 基本的な使い方

```cm
import std::collections::vector::Vector;
import std::io::println;

int main() {
    Vector<int> v();       // コンストラクタ呼び出し
    v.push(10);
    v.push(20);
    v.push(30);

    println("len: {v.len()}");           // 3
    println("first: {*v.get(0)}");       // 10

    int last = v.pop();                  // 30
    println("popped: {last}");
    println("len: {v.len()}");           // 2

    return 0;
}
// スコープ終了時に ~self() が自動呼出し → メモリ解放
```

---

## API一覧

| メソッド | 戻り値 | 説明 |
|---------|--------|------|
| `push(value)` | `void` | 末尾に要素を追加 |
| `pop()` | `T` | 末尾の要素を取得して削除 |
| `get(index)` | `T*` | 要素へのポインタを取得（所有権を移動しない） |
| `set(index, value)` | `void` | 指定位置の要素を置換 |
| `len()` | `int` | 要素数 |
| `capacity()` | `int` | 確保済み容量 |
| `is_empty()` | `bool` | 空かどうか |
| `clear()` | `void` | 全要素削除（メモリは保持） |
| `reserve(n)` | `void` | 容量をn以上へ事前拡張（縮小はしない）（v0.17.2） |
| `last()` | `T*` | 末尾要素へのポインタ（popせず参照）（v0.17.2） |
| `sort()` | `void` | 昇順ソート（挿入ソート・プリミティブ型のみ） |
| `sortBy(cmp)` | `void` | 比較関数`int*(T, T)`によるソート（v0.17.2） |

---

## get() はポインタを返す

`get()` は `T*`（ポインタ）を返します。値を読み取るにはデリファレンス `*` が必要です。

```cm
Vector<int> v();
v.push(42);

int* ptr = v.get(0);     // ポインタ取得
int val = *ptr;           // デリファレンスで値を取得
println("{val}");         // 42

// 値を直接使う場合
println("{*v.get(0)}");   // 42
```

---

## ネストされたVector

`Vector<Vector<int>>` のようにネストして2次元配列を実現できます。

### ⚠️ move が必要

内側のVectorを外側にpushする際は **`move`** キーワードが必要です。Vectorは所有権型であり、コピーではなくムーブセマンティクスで転送します。

```cm
import std::collections::vector::Vector;
import std::io::println;

int main() {
    // 2次元ベクタ（行列）
    Vector<Vector<int>> matrix();

    // 行1: [1, 2, 3]
    Vector<int> row1();
    row1.push(1);
    row1.push(2);
    row1.push(3);
    matrix.push(move row1);   // ← moveで所有権を転送

    // 行2: [4, 5, 6]
    Vector<int> row2();
    row2.push(4);
    row2.push(5);
    row2.push(6);
    matrix.push(move row2);   // ← moveで所有権を転送

    // アクセス: matrix[0][1] = 2
    Vector<int>* row = matrix.get(0);
    int val = *row->get(1);
    println("matrix[0][1] = {val}");   // 2

    println("rows: {matrix.len()}");   // 2

    return 0;
}
// スコープ終了時に外側・内側のVector全てが自動的に解放される
```

> **重要:** `move` なしでpushすると二重解放が発生します。Vector、Queue、HashMapなどの所有権型は必ず `move` で渡してください。

---

## ソート

プリミティブ型の要素に対して昇順ソートが可能です。

```cm
Vector<int> v();
v.push(30);
v.push(10);
v.push(20);

v.sort();

println("{*v.get(0)}");  // 10
println("{*v.get(1)}");  // 20
println("{*v.get(2)}");  // 30
```

比較関数を渡す `sortBy` で任意の順序にできます（負=aが先・正=bが先。v0.17.2）。

```cm
v.sortBy((int a, int b) => b - a);   // 降順
println("{*v.get(0)}");  // 30

Vector<double> d();
d.push(2.5);
d.push(0.5);
d.sortBy((double a, double b) => (a < b) ? -1 : 1);  // 昇順
```

---

## メモリ管理

| 操作 | 説明 |
|------|------|
| `self()` | コンストラクタ。初期容量0、データ未確保 |
| `push()` | 容量不足時に自動で2倍に拡張 (0→4→8→16...) |
| `clear()` | 要素数を0にリセット。確保済みメモリは保持 |
| `~self()` | デストラクタ。ヒープメモリを `free()` で解放 |

---

**関連:** [Queue](queue.html) · [HashMap](hashmap.html)

---

<!-- nav -->
← 前: [native::gpu - GPU計算 (Apple Metal)](../gpu.html) ｜ [目次](../index.html) ｜ 次: [std::collections::queue - FIFOキュー](queue.html) →
