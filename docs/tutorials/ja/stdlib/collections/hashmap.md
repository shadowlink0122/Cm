---
title: HashMap
---

# std::collections::hashmap - 連想配列

`HashMap<K, V>` はジェネリックなキー・バリュー連想配列です。

> **対応バックエンド:** Native (LLVM) のみ

**最終更新:** 2026-02-08

---

## 基本的な使い方

```cm
import std::collections::hashmap::HashMap;
import std::io::println;

int main() {
    HashMap<int, int> m();

    m.insert(1, 100);
    m.insert(2, 200);
    m.insert(3, 300);

    println("len: {m.len()}");              // 3
    println("get(2): {m.get(2)}");          // 200
    println("contains(1): {m.contains(1)}"); // true

    m.remove(1);
    println("after remove(1):");
    println("  contains(1): {m.contains(1)}"); // false
    println("  len: {m.len()}");                // 2

    return 0;
}
// スコープ終了時に ~self() が自動呼出し → メモリ解放
```

---

## API一覧

| メソッド | 戻り値 | 説明 |
|---------|--------|------|
| `insert(key, value)` | `void` | キーと値を挿入（既存キーは上書き） |
| `get(key)` | `V` | キーに対応する値を取得 |
| `contains(key)` | `bool` | キーが存在するか |
| `remove(key)` | `void` | キーと値を削除 |
| `len()` | `int` | 要素数 |
| `clear()` | `void` | 全要素削除 |

---

## マルチジェネリクス

キーと値に異なる型を使用できます。

```cm
HashMap<int, int> scores();        // int → int
HashMap<int, double> prices();     // int → double
HashMap<int, bool> flags();        // int → bool
```

> **注意:** 現在のハッシュ関数は `key as int` で計算するため、キー型は `int` または `int` にキャスト可能な型が推奨されます。

---

## 使用例: カウンタ

```cm
import std::collections::hashmap::HashMap;
import std::io::println;

int main() {
    HashMap<int, int> counter();

    // 各値の出現回数をカウント
    int data[5] = {1, 2, 1, 3, 1};
    for (int i = 0; i < 5; i++) {
        int key = data[i];
        if (counter.contains(key)) {
            int count = counter.get(key);
            counter.insert(key, count + 1);
        } else {
            counter.insert(key, 1);
        }
    }

    println("1: {counter.get(1)} times");  // 3
    println("2: {counter.get(2)} times");  // 1
    println("3: {counter.get(3)} times");  // 1

    return 0;
}
```

---

## 内部構造

`HashMap<K, V>` はオープンアドレス法（線形探索）で実装されています。

- **初期容量:** 16エントリ
- **衝突解決:** 線形探索 (linear probing)
- **ハッシュ関数:** `key as int` → `abs(hash) % capacity`

```
entries: [  ][K1:V1][  ][  ][K2:V2][  ][K3:V3][  ]...
              occupied             occupied    occupied
```

---

## 注意事項

- `get()` でキーが存在しない場合、`V` のデフォルト値（ゼロ値）が返ります
- 事前に `contains()` で存在確認してから `get()` するのが安全です
- 自動リサイズは未実装です。大量のデータを扱う場合は容量に注意してください

---

**関連:** [Vector](vector.html) · [Queue](queue.html)
