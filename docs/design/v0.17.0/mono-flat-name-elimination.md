---
title: モノモーフ化のフラット名逆算の完全廃止（Q2真因の構造的解消）
parent: v0.17.0 Design
---

# モノモーフ化のフラット名逆算の完全廃止（Q2真因の構造的解消）

## 概要

monomorphization-typed-instantiation（archive済み）は「型ノード駆動化と名前マングリング逆算の廃止」を掲げたが、実測では逆算器`parse_flat_type_args`（mono/typeinfo.cpp:89-128）とmono_internal.cppの文字列置換分岐（:115-147・:210-268）が現役で残っている。
可逆な`$`長さ接頭辞エンコーダ（typekey.cpp）は存在するのに、`struct_symbol_key`（typeinfo.cpp:60）の「simple高速パス」（:74-81）が引数キーに`$`が含まれない限りフラット名`base__k1__k2…`を生成するため、**ネスト特殊化引数のときに限って曖昧な名前が選ばれる**という設計欠陥になっている。

これがQ2（nested-generic-type-arg-string.md）の真因である:
`Pair<Box<int>, Box<string>>` → フラット名`Pair__Box__int__Box__string` → `parse_flat_type_args`が`Box|int|Box|string`の4引数と誤解 → substが先頭2つだけ採用し`A:=Box（裸）, B:=int` → フィールド`second`がint(4バイト)になりレイアウト崩壊 → 壊れたポインタ経由のstring読みでrc=0無言死。
`Box<string>`単独が正常なのはparam_count==1の結合特例（:114-122）があるからで、フラット文法が本質的に曖昧（`Box<Box<int>>`・`Box<Box,int>`・ユーザー定義`Box__Box__int`が衝突）というtypekey.hpp:11-14の警告どおりの事故である。

## 残存する文字列手術の棚卸し（実測）

- 曖昧逆算（Q2と同族・正確性クリティカル）8箇所: typeinfo.cpp:100-126（逆算器本体）・:204-210（フィールド型復元）、mono_structs.cpp:121-128（マングリング名からの発見）・:240-249（to_symbol_type）・:508-513（フィールドアクセス再型付け）、scan.cpp:134-141・:279-299（呼び出しサイト分割、parts数不一致で無言ドロップ）、specialize.cpp:136-146。
- ベース名抽出（先頭`__`前を取るだけ・概ね安全だが脆い）15箇所: context.cpp:186/206/267/368、base.cpp:73、auto_impl.cpp:102/910、expr/access.cpp:117/179、expr/binary.cpp:713、expr_call.cpp:44/88/387、stmt系、program_dce.cpp:212/223。
- フラット名産生（曖昧性の供給源）12箇所: typeinfo.cpp:77/119、scan.cpp:163、mono_internal.cpp:100/122/137/252/323/353、impl.cpp:432、lowering.cpp:485/505、stmt/let.cpp:766。
- checker/HIRの重複剥ぎ: types/checking/call/method.cpp:260-270とhir/lowering/expr_member.cpp:128-131が逐語複製（この重複はchecker-to-hir-resolution-handoff.mdで扱う）。

## リファクタリング方針

1. **即修（R1）**: `struct_symbol_key`のsimple高速パスを、引数キーが`__`を含む場合（=引数自体が特殊化）にも`$`エンコード分岐へ強制する。`$`エンコード名の消費側（resolve_struct_field_types:193-201・mono_structs.cpp:499-506等）は既に対応済みのため、Q2はこの1箇所で修正できる。
2. **全廃（R2）**: 特殊化の同定を全経路typekeyへ統一し、`parse_flat_type_args`とmono_internal.cppの文字列置換分岐を削除する。表示用の名前はtypekey::display_nameで生成し、同定（identity）と表示（display）を分離する。
3. ベース名抽出15箇所は、`$`エンコード名でも正しくベースを取れる共通関数（typekey側に既存のdecode系を利用）へ置換する。

## リスク

- シンボル名が変わるため、ゴールデン/IR期待値の再生成が必要になる（マトリクス回帰は値検証主体のため影響は限定的）。
- `__`前提の消費側15箇所の`$`対応漏れは、無置換特殊化の常時検査（mono導入済み）とマングリング衝突ハードエラー（C16導入済み）が検出網になる。

## テスト計画

- ネスト特殊化のマトリクス: `Pair<Box<int>, Box<string>>`・`Pair<Box<string>, Box<int>>`・`Box<Pair<int,string>>`・`Tri<A,B,C>`混載・ユーザー定義`Box__Box__int`風名前との衝突検査を6経路+wasm/jsで。
- 既存のジェネリック回帰全数と、tests/common/genericsスイートの通過。

## 検出経緯

全体複雑度レビュー（2026-08-05）のモノモーフ化調査で、Q2最小再現（.tmp/bughunt5/q_r01e.cm）の実行経路を静的に追跡して真因を特定した。
