---
title: 算術演算子インターフェースのimpl形が内部エラー（Q4）
parent: v0.17.0 Design
---

# 算術演算子インターフェースのimpl形が内部エラー（Q4）

## 概要

`impl Vec2 for Add { operator Vec2 +(Vec2 other) {...} }` の形（インターフェース指定付き演算子impl）が「internal error (typecheck): 'Add' is not a declared interface」になる。
Eq/Ordは組み込み宣言済みで同形が通るのに対し、算術・ビット系（Add/Sub/Mul/Div/Mod/BitAnd/BitOr/BitXor/Shl/Shr）は組み込みインターフェース宣言が無く、さらに未宣言インターフェースへのimplが例外→内部エラーとして漏れる（診断品質の問題を併発）。
inherent impl形（`impl Vec2 { operator Vec2 +(...) }`）は正常に動作し、`+`連鎖・`+=`複合代入まで正しい（第5ラウンドで検証済み）。

## 修正方針

1. 算術・ビット演算子インターフェース（Add/Sub/Mul/Div/Mod/BitAnd/BitOr/BitXor/Shl/Shr）をEq/Ordと同様に組み込み宣言へ追加し、`impl T for Add`形を受理する（decl.cpp側は演算子種別からのインターフェース自動登録が既にあるため、宣言の追加で整合する見込み）。
2. 未宣言インターフェースへのimplは例外でなく通常診断（「'X' is not a declared interface」）で報告する（diagnostics-engine-unificationの例外漏れ検査対象）。
3. チュートリアルoperators.mdに正式なimpl形（inherent／for Interface両対応なら両方）を明記する。

## 検出経緯

第5ラウンドで検出。inherent impl形の健全性は `.tmp/bughunt5/q2/r03_operator_overload.cm`（連鎖・==・+=）で確認済み。
