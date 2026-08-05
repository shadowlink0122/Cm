---
title: HashMapが17要素以上で挿入済み要素を喪失（Q7）
parent: v0.17.0 Design
---

# HashMapが17要素以上で挿入済み要素を喪失（Q7）

## 概要

`std::collections::hashmap`のHashMapに17個以上のエントリをinsertすると、境界（初期容量16）を超えた分の要素が取得できなくなる（`get`がNone）。
16個までは全件正常、17個目以降は`get(最後のキー)`がNoneになり、`unwrap()`で実行時パニックする。全バックエンド共通で、リサイズ（rehash）の欠陥または未実装とみられる。コレクションの基本機能の破綻でCritical。

## 再現コード

```cm
import std::collections::hashmap::*;
HashMap<int, string> m();
for (int i = 0; i < 17; i++) {
    m.insert(i, "v" + i);
}
Option<string> last = m.get(16);
// None（期待Some("v16")）。16個までなら全件取得できる
```

## 修正方針

1. `libs/std/collections/hashmap.cm`のinsert/resize経路を確認する。容量拡張時の全エントリ再ハッシュが行われているか、拡張後のバケット参照が新配列を指しているか（Cmの値意味論でバケット配列のコピーを変異していないか）を重点的に見る。
2. 喪失パターン（拡張トリガ後の旧要素／新要素どちらが消えるか）をキー分布を変えて特定する。
3. 回帰: 容量境界前後（16/17/33要素）の全件取得・remove後の再取得・文字列キー版のマトリクスを追加する。既存のhashmapテストが16件以下しか挿入していないため境界テストが必須。

## 検出経緯

第5ラウンドで検出。しきい値の二分は `.tmp/bughunt5/q_hm.cm`（8/16=全件OK、17以降=末尾喪失）。
