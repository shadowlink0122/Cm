---
title: enumへのinherent implメソッドが未サポート（Q5）
parent: v0.17.0 Design
---

# enumへのinherent implメソッドが未サポート（Q5）

## 概要

単純enumへのinherent implメソッド（`impl Color { string name() { return match(self) {...}; } }`）が、呼び出し時に「Unknown method 'name' for type 'int'」で拒否される。
単純enumの値がint表現へ正規化された後にメソッド解決が行われるため、enum名でのメソッド表引きに到達しない。機能として未サポートなら「enumにはimplできない」旨をimpl宣言時点で診断すべきで、現状はimpl宣言が黙って受理され呼び出し側で的外れな型名（int）のエラーになる。

## 修正方針

1. 仕様決定: enumへのinherent implを (a) サポートする（メソッド解決でenum元型名を保持しint正規化前にディスパッチ）か、(b) 非サポートとしてimpl宣言時に診断するか。matchベースのname()等は実用価値が高く(a)を推奨。
2. (a)の場合、self型はenum（int表現）で渡し、`match (self)`のタグ比較がそのまま機能することを確認する。Tagged Union enum（ペイロード付き）への拡張も同時に検討する。
3. 回帰: 単純enum・ペイロード付きenumそれぞれのメソッド呼び出し（値レシーバ・match内・補間内）。

## 検出経緯

第5ラウンドで検出。再現は `.tmp/bughunt5/q3/s01_enum_methods.cm`。
