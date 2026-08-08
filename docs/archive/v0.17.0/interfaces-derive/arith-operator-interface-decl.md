---
title: 算術演算子インターフェースのimpl形が内部エラー（Q4）
parent: v0.17.0 Design
---

# 算術演算子インターフェースのimpl形が内部エラー（Q4）

## 概要

`impl Vec2 for Add { operator Vec2 +(Vec2 other) {...} }` の形（インターフェース指定付き演算子impl）が「internal error (typecheck): 'Add' is not a declared interface」になる。
Eq/Ordは組み込み宣言済みで同形が通るのに対し、算術・ビット系（Add/Sub/Mul/Div/Mod/BitAnd/BitOr/BitXor/Shl/Shr）は組み込みインターフェース宣言が無く、さらに未宣言インターフェースへのimplが例外→内部エラーとして漏れる（診断品質の問題を併発）。
inherent impl形（`impl Vec2 { operator Vec2 +(...) }`）は正常に動作し、`+`連鎖・`+=`複合代入まで正しい（未修正バグ調査（Q系）で検証済み）。

## 修正方針

1. 算術・ビット演算子インターフェース（Add/Sub/Mul/Div/Mod/BitAnd/BitOr/BitXor/Shl/Shr）をEq/Ordと同様に組み込み宣言へ追加し、`impl T for Add`形を受理する（decl.cpp側は演算子種別からのインターフェース自動登録が既にあるため、宣言の追加で整合する見込み）。
2. 未宣言インターフェースへのimplは例外でなく通常診断（「'X' is not a declared interface」）で報告する（diagnostics-engine-unificationの例外漏れ検査対象）。
3. チュートリアルoperators.mdに正式なimpl形（inherent／for Interface両対応なら両方）を明記する。

## 検出経緯

未修正バグ調査（Q系）で検出。inherent impl形の健全性は `.tmp/bughunt5/q2/r03_operator_overload.cm`（連鎖・==・+=）で確認済み。

## 実装記録（修正済み）

1. `register_builtin_interfaces`（src/internal/types/checking/auto_impl.cpp）へ算術・ビット演算子インターフェース10種（Add/Sub/Mul/Div/Mod/BitAnd/BitOr/BitXor/Shl/Shr）をEq/Ordと同形の表駆動で組み込み宣言した。各インターフェースはジェネリックパラメータTと演算子メソッド（T引数・T戻り）を持ち、operator定義からの自動登録名（decl.cppのswitch）と同一名で整合する。適合検証（implがインターフェースの全メソッドを実装しているかの検査）は既存機構に存在しないため、宣言追加のみで`impl T for Add`形が受理・動作する。
2. 例外の診断化: decl.cppのthrow4件（未宣言インターフェースへのimpl×2箇所・同一インターフェースへの重複impl・同名メソッドの重複定義）を通常診断（i18n 3メッセージ・ja訳付き）へ置換した。従来はbuild側のcatchで「internal error (typecheck)」表示だった。check_impl側の未宣言検査はregister_implが診断済みのため重複報告せず本文検査だけ打ち切る。dispatch側でcurrent_span_を設定し診断位置を付与した。
3. 回帰: 肯定テスト（tests/common/interface/operator_arith_interface.cm、10演算子のimpl-for形+連鎖+複合代入）とエラーテスト3本（未宣言インターフェース・重複impl・重複メソッド）、i18n E2E（q4-undeclared-iface-en/ja）を追加した。チュートリアルoperators.md（ja/en）の「算術・ビット演算子はimpl T構文で定義します」の古い注記を差し替え、インターフェース指定形の節（対応表付き）を新設した。
4. 既知の制約として記録: ジェネリック関数本体での境界付き算術（`<T: Add> T sum(T a, T b) { return a + b; }`）はcheckerの二項演算検査がジェネリックパラメータの境界を参照しないため未対応（`<T: Ord>`比較は従来から使用可能）。checker受理に加えmono/codegenの演算子ディスパッチ拡張を要するため、[method-resolution-unification.md](../../../design/v0.17.0/method-resolution-unification.md)の統一解決API導入後の課題とする。
