---
title: TreeMap
parent: Advanced Features
---

[English](../../en/advanced/treemap.html)

# `std::collections::TreeMap` による順序付きマップ

**ゴール:** キー/値を平衡二分探索木に格納し、O(log n) で探索する。
**レベル:** 🟡 中級

---

## 概要

`TreeMap<K, V>` は**自己平衡二分探索木（AVL木）**を土台にしたジェネリックな連想コンテナ。探索・挿入は **O(log n)** で、線形探索のマップと違い要素数が増えても性能が保たれる。木はインデックス参照のノードアリーナ（固定容量）で表現し、ポインタも `malloc` も使わないため、全バックエンド（jit/native/wasm/js/ts）で同じ動作をする。

キーは昇順に保たれるため、キー型 `K` は比較可能（`<` と `==`）である必要がある。`int`・`string` 等の組み込み型はそのまま、構造体は `with Ord, Eq` を付ける。値型 `V` は任意。

---

## 使い方

```cm
import std::collections::treemap::*;

int main() {
    TreeMap<string, int> m();
    m.put("banana", 2);
    m.put("apple", 1);
    m.put("cherry", 3);

    if (m.contains("apple")) {
        println(m.get("apple"));   // 1
    }
    println(m.size());             // 3

    m.put("apple", 100);           // 既存キーの更新
    println(m.get("apple"));       // 100
    return 0;
}
```

### API

| メソッド | 意味 |
|---------|------|
| `put(key, val)` | 挿入または更新。O(log n) |
| `get(key) -> V` | キーの値（存在しないキーは未規定値。先に contains で確認） |
| `contains(key) -> bool` | キーの有無。O(log n) |
| `size() -> int` | 要素数 |
| `is_full() -> bool` | 固定容量に達したか |

---

## 構造体キー

構造体に `with Ord, Eq` を付ければキーにできる（順序・等価がフィールド順に自動導出される）。

```cm
struct Version with Ord, Eq {
    int major;
    int minor;
}

TreeMap<Version, string> releases();
releases.put(Version { major: 1, minor: 0 }, "first");
releases.put(Version { major: 2, minor: 1 }, "latest");
```

---

## 平衡性と容量

挿入ごとにAVL回転で平衡化するため、ソート済みキーを挿入しても高さは対数に収まる（ソート済み100件挿入 → 高さ約7、100ではない）。

アリーナは固定容量（`CAP`、既定128）。超過した `put` は無視される（`is_full` で確認）。nativeバックエンドでは容量に比例してAOT最適化の時間が増える（インライン配列をSROAが要素展開するため）。jit/js/ts にはこのコストが無いので、大きなマップが必要ならそちらで `CAP` を増やす。

---

<!-- nav -->
← 前: [応用編 - JSON](json.html) ｜ [目次](../index.html) ｜ 次: [型システム編](../types/index.html) →
