---
title: TreeMap
---

[English](../../../en/stdlib/collections/treemap.html)

# std::collections::treemap - 順序付きマップ

`TreeMap<K, V>` は自己平衡二分探索木（AVL木）による順序付きのキー・バリューマップです（v0.17.0で追加）。
探索・挿入が **O(log n)** で、要素数の増加に強く、キーは常に順序比較で管理されます。

> **対応バックエンド:** JIT / Native / WASM / JS / TS（ノードをインデックス参照のアリーナで表現するため全バックエンドで同一動作）

---

## 基本的な使い方

```cm
import std::collections::treemap::*;
import std::io::println;

int main() {
    TreeMap<string, int> m();

    m.put("apple", 1);
    m.put("banana", 2);
    m.put("cherry", 3);

    println("size: {m.size()}");                       // 3
    println("apple: {m.get_or(\"apple\", 0)}");        // 1
    println("contains(kiwi): {m.contains(\"kiwi\")}"); // false

    // getはOption<V>を返す（不在はNone）
    Option<int> v = m.get("banana");
    println("banana: {v.unwrap_or(-1)}");              // 2

    m.put("apple", 10);  // 既存キーは上書き
    println("apple: {m.get_or(\"apple\", 0)}");        // 10
    return 0;
}
```

---

## API一覧

| メソッド | 戻り値 | 説明 |
|---------|--------|------|
| `put(key, value)` | `void` | キーと値を挿入（既存キーは上書き）。O(log n) |
| `get(key)` | `Option<V>` | 値を取得（存在すれば`Some(値)`、不在なら`None`）。O(log n) |
| `get_or(key, default)` | `V` | 値を取得（不在時は`default`を返す非Option版）。O(log n) |
| `contains(key)` | `bool` | キーが存在するか。O(log n) |
| `remove(key)` | `void` | キーを削除（AVL回転で平衡を維持・不在キーは無視・空きスロットは再利用）。O(log n)（v0.17.2） |
| `keys_in_order()` | `K[]` | 全キーを昇順で取得（in-order走査）（v0.17.2） |
| `clear()` | `void` | 全要素を削除（v0.17.2） |
| `size()` | `int` | 要素数 |

---

## キー型の制約

キー型 `K` は順序比較（`<` と `==`）が必要です。

- `int` / `string` などの組み込み型はそのまま使えます
- 構造体をキーにする場合は `with Ord, Eq` を付けます
- 順序を持たない型はキーにできません（その用途は [HashMap](hashmap.html) を使ってください）

値型 `V` は任意です。

---

## HashMapとの使い分け

| | TreeMap | HashMap |
|---|---------|---------|
| 探索・挿入 | O(log n) | 平均O(1)（線形探索の衝突解決） |
| キーの制約 | 順序比較（`<`・`==`） | `int` にキャスト可能な型が推奨 |
| 内部構造 | AVL木（挿入ごとに平衡化） | オープンアドレス法 |
| 容量 | 自動拡張（スライスのアリーナ） | 負荷率50%で自動2倍拡張 |

キー数が多い・キーが文字列・容量を事前に見積もれない場合はTreeMapが向いています。

---

**関連:** [HashMap](hashmap.html) · [Vector](vector.html) · [Queue](queue.html)

---

<!-- nav -->
← 前: [std::collections::hashmap - 連想配列](hashmap.html) ｜ [目次](../index.html) ｜ 次: [TreeSet / HashSet - 集合](sets.html) →
