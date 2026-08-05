---
title: HashMapが17要素以上で挿入済み要素を喪失（Q7）
parent: v0.17.0 Design
---

# HashMapが17要素以上で挿入済み要素を喪失（Q7）

## 概要

`std::collections::hashmap`のHashMapに17個以上のエントリをinsertすると、境界（初期容量16）を超えた分の要素が取得できなくなる（`get`がNone）。
16個までは全件正常、17個目以降は`get(最後のキー)`がNoneになり、`unwrap()`で実行時パニックする。全バックエンド共通で、コレクションの基本機能の破綻でCritical。

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

## 実測で確定した真因（2026-08-05）

リサイズ（rehash）が**未実装**だった。実装は容量16固定のオープンアドレス法（線形探索）で、`insert`は全スロットを探索して空きも既存キーも見つからない場合にループを抜けて**黙ってreturn**するため、満杯後のinsertが無診断で喪失していた（20件insertで`len()=16`・4件喪失を確認）。

## 実装記録

`libs/std/collections/hashmap.cm`へ容量拡張`grow()`を実装した:

- `insert`の先頭で負荷率50%（`size * 2 >= cap`）に達したら容量を2倍へ拡張する。線形探索の探索列を短く保ち、空スロットの存在を常に保証するため、満杯によるinsert喪失は構造的に起きなくなる。
- `grow()`は新バッファ（2倍容量・全スロット未占有初期化）を確保し、旧バッファの占有エントリを新容量で再ハッシュして挿入し直し、`entries`/`cap`を差し替えて旧バッファをfreeする。`size`は不変。
- ハッシュ（`key as int`の絶対値 % 容量）・衝突解決（線形探索）・エントリstride（64バイト固定確保）は従来のまま。

## 回帰テスト

- `tests/common/collections/hashmap_resize_test.cm`: 容量境界前後（16/17/33件）の全件取得、複数回growをまたぐ200件挿入と拡張後の上書き・contains、拡張をまたいだremove後の残存キー全件取得、文字列値・負のintキーの拡張またぎを、jit O0/O2・native O0/O2・wasm O0/O2の6経路で出力一致検証（collectionsカテゴリはlibcのvoid*依存のためjs/tsは既存の一括スキップ対象）。
- 既存の`hashmap_test.cm`（16件以下の全API）も継続通過。

## 将来課題

- `remove`が探索列を分断する既存問題（トゥームストーン無しの`occupied=false`化のため、同一探索列上の後続エントリが到達不能になりうる）。今回のQ7スコープ外として記録する。
- 文字列キーのハッシュが`key as int`（ポインタビット由来）のため、実行時構築した同内容文字列のルックアップは既存から不安定。ハッシュの内容ベース化はコレクション設計の将来課題。

## 検出経緯

第5ラウンドで検出。しきい値の二分は `.tmp/bughunt5/q_hm.cm`（8/16=全件OK、17以降=末尾喪失）。
