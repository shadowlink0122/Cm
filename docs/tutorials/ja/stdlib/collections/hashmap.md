---
title: HashMap
---

# std::collections::hashmap - 連想配列

`HashMap<K, V>` はジェネリックなキー・バリュー連想配列です。

> **キー型の制約:** ハッシュは `key as int` のため、**intへキャスト可能なキー専用**です。文字列キーはポインタ値ハッシュになり実行時生成キーが外れるため、[StringMap / StringSet](#stringmap---文字列キー連想配列v0172) を使ってください。

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

    println("len: {m.len()}");                    // 3
    println("get(2): {m.get(2).unwrap_or(0)}");   // 200（getはOption<V>を返す）
    println("get_or(9, -1): {m.get_or(9, -1)}");  // -1（不在キーはデフォルト値）
    println("contains(1): {m.contains(1)}");       // true

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
| `get(key)` | `Option<V>` | 値を取得（存在すれば`Some(値)`、不在なら`None`） |
| `get_or(key, default)` | `V` | 値を取得（不在時は`default`を返す非Option版） |
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

    // 各値の出現回数をカウント（不在キーはget_orで0扱い）
    int[5] data = [1, 2, 1, 3, 1];
    for (int i = 0; i < 5; i++) {
        int key = data[i];
        counter.insert(key, counter.get_or(key, 0) + 1);
    }

    println("1: {counter.get_or(1, 0)} times");  // 3
    println("2: {counter.get_or(2, 0)} times");  // 1
    println("3: {counter.get_or(3, 0)} times");  // 1

    return 0;
}
```

---

## 内部構造

`HashMap<K, V>` はオープンアドレス法（線形探索）で実装されています。

- **初期容量:** 16エントリ（負荷率50%で自動的に2倍へ拡張・全エントリ再ハッシュ。v0.17.0）
- **衝突解決:** 線形探索 (linear probing)
- **ハッシュ関数:** `key as int` → `abs(hash) % capacity`

```
entries: [  ][K1:V1][  ][  ][K2:V2][  ][K3:V3][  ]...
              occupied             occupied    occupied
```

---

## 注意事項

- **v0.17.0から `get()` は `Option<V>` を返します**: 不在キーは `None` として型で表現されます。従来の `int v = m.get(k);` は `m.get_or(k, 0)` または `m.get(k).unwrap_or(0)` へ移行してください（旧getは不在キーで未初期化メモリを返す危険な契約でした）
- `Option` の扱いは `is_some()` / `is_none()` / `unwrap()` / `unwrap_or(default)` / `match` が使えます
- v0.17.0で容量の自動拡張に対応しました（従来は容量16を超えるinsertが黙って失われていました）

---

**関連:** [Vector](vector.html) · [Queue](queue.html)

---

## StringMap - 文字列キー連想配列（v0.17.2）

キーを文字列に固定し、内容ハッシュ（FNV-1a）で探索する連想配列です。同一内容の文字列は生成方法（リテラル・連結・補間）によらず正しくヒットします。

```cm
import std::collections::strmap::*;
import std::io::println;

int main() {
    StringMap<int> m();
    for (int i = 0; i < 100; i++) {
        m.insert("symbol_{i}", i);        // 実行時生成キー
    }
    println("{m.get_or(\"symbol_42\", -1)}");  // 42
    println("{m.contains(\"sym\" + \"bol_7\")}");  // true（内容ハッシュ）

    m.remove("symbol_0");
    string[] ks = m.keys_in_order();       // 生存キーの挿入順
    println("len={m.len()} keys={ks.len()}");
    return 0;
}
```

| メソッド | 戻り値 | 説明 |
|---------|--------|------|
| `insert(key, value)` | `void` | 挿入（既存キーは上書き）。平均O(1)・負荷率50%で自動拡張 |
| `get(key)` | `Option<V>` | 取得（不在は`None`） |
| `get_or(key, default)` | `V` | 取得（不在時は`default`） |
| `contains(key)` / `remove(key)` | `bool` / `void` | 存在確認 / 削除（墓石方式・スロット再利用） |
| `keys_in_order()` | `string[]` | 生存キーを挿入順で返す |
| `len()` / `is_empty()` / `clear()` | - | 要素数 / 空判定 / 全削除 |

スライスバックでデストラクタを持たないため、他の構造体のフィールドとしても安全に使えます（全バックエンド対応）。
集合が欲しい場合は `StringSet`（`std::collections::strset`）を使います。

---

<!-- nav -->
← 前: [std::collections::queue - FIFOキュー](queue.html) ｜ [目次](../index.html) ｜ 次: [TreeMap - 順序付きマップ](treemap.html) →
