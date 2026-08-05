---
title: 暗黙変換の統一ドライバ化（変換挿入サイト散在の構造的解消）
parent: v0.17.0 Design
---

# 暗黙変換の統一ドライバ化（変換挿入サイト散在の構造的解消）

## 概要

暗黙変換のMIR挿入がヘルパ3種（coerce_numeric_context・coerce_to_union・coerce_fixed_array_to_slice）を消費サイトごとに手組みで連鎖する構造になっており、全種を連鎖しているのは引数・デフォルト引数の2サイトだけである。
B2・Y1〜Y3・Y5・Z5・Q3と続いた「受理されるのに変換が挿入されないサイトがある」バグ族は、この散在構造の直接の帰結であり、サイト個別修正では次の変換種追加時に必ず再発する。
消費サイトを実測したところ11サイトあり、ユニオン変換はヘルパ経由とインラインCast直書きの2方式が併存し、配列→スライスはヘルパを呼ぶのが2サイトのみで3サイトはインライン再実装、インターフェースupcastに至っては変換系に存在しない。

## 現状マトリクス（サイト×変換種の実測）

| サイト | numeric | union | array→slice | interface |
|---|---|---|---|---|
| let初期化（スカラ） stmt/let.cpp:658 | ✅ | ⚠️インラインCast | ✗ | ✗ |
| letスライスリテラル要素 stmt/let.cpp:464 | ✗（別インライン） | ✅ | ✗ | ⚠️インライン一時 |
| 単純代入 stmt/assign.cpp:121 | ✅ | ⚠️インラインCast | ✗ | ✗ |
| メンバ/添字/deref代入 stmt/assign.cpp:141 | ✅ | ✗ | ✗ | ✗ |
| return stmt/control.cpp:100 | ✅ | ✅ | ⚠️インライン別実装:48-98 | ✗ ←Q3 |
| 構造体リテラルフィールド expr/construct.cpp:217 | ✅ | ✅ | ✗ | ✗ |
| push expr_slice.cpp:148 | ✗ | ✅ | ⚠️インライン | ⚠️インライン一時 |
| 呼び出し引数 expr_call.cpp:271 | ✅ | ✅ | ✅ | ✗ |
| デフォルト引数 expr_call.cpp:352 | ✅ | ✅ | ✅ | ✗ |
| 複合代入RHS expr/binary.cpp:419 | ✅ | ⚠️インラインCast | ✗ | ✗ |

インターフェースupcastはMIRの変換ではなく、(a) H1のインライン一時変数パターン（stmt/let.cpp:472-486とexpr_slice.cpp:177-191に逐語複製）と、(b) 各バックエンドのassign認識（LLVM statement/assign.cpp:79,142,232のcreateInterfaceFatPtr・JS emit_statements.cpp:112-134）に分裂している。
さらに戻り値fat pointerのheap-boxing（スタックローカルへのfat pointerがdangleする問題の対処）はLLVMのassign.cpp:58-77にしか存在せず、Q3（インターフェース戻り値）がバックエンドごとに別の壊れ方をする真因になっている。
checker側の受理判定（utils/compat.cpp・utils/conversion.cpp）とMIR側の挿入も独立実装であり、conversion.cpp冒頭コメントが2実装分離を明言している——受理と挿入が乖離できる構造そのものが無言ミスコンパイルの温床である。

## リファクタリング方針

1. **統一ドライバ**: `LocalId LoweringContext::coerce_to_expected(LocalId value, const hir::TypePtr& expected)` を新設し、全11サイトを「値を作る→coerce_to_expected→格納」の1形へ集約する。
2. **再帰的ディスパッチ**: フラットなnumeric→union→sliceの固定順ではなく、宛先がユニオンならまず変種を解決して値を変種型へ再帰的にcoerceしてからwrapする（ユニオンofスライス・ユニオン内インターフェース・変種内numeric正規化が固定順では壊れるため）。
3. **インターフェースupcastのMIR化**: fat pointer構築をMIRの構築物（Rvalueまたは専用ヘルパ）へ昇格し、return時のheap-boxingはフラグ1つで表現して、LLVM/JS/SV/interpreterのassign認識コードを撤去する。これがQ3の恒久修正を包含する。
4. **受理と挿入の同表化**: checkerが受理した変換種をHIRノードへ注釈し、loweringは注釈を読んで挿入するだけにする（再導出の廃止）。Z5のclassify_numeric_conversionを変換種全体（numeric/union/slice/interface）へ拡張した`conversion_kind`表を単一の真実とする。

## 段階分割

- 第1段（低リスク・高効果）: coerce_to_expected導入と11サイトの置換。インラインCast方式のユニオン3サイト・インライン再実装のarray→slice 3サイトをヘルパへ吸収する。挙動変更なしの純リファクタリング。
- 第2段（Q3修正を包含）: インターフェースupcastのMIR構築物化とreturnのbox統一。4バックエンドのassign認識を削除し、interface-return-fat-pointer.mdのバグをこの機構で修正する。
- 第3段: checker注釈駆動化（受理した変換のHIR記録）。types_compatibleの構造変換分岐を注釈生成へ置き換え、受理・挿入の2実装を1表に畳む。

## テスト計画

- 変換種（numeric/union/slice/interface）×サイト（let/代入/メンバ代入/return/フィールド/push/引数/デフォルト引数/複合代入）のマトリクス回帰を6経路（jit O0/O2・native O0〜O3）+wasm/jsで追加する。
- 再帰ケース: ユニオンofスライス変種への固定長配列代入・ユニオン内インターフェース変種・変種への縮小numericを明示的に検証する。

## 検出経緯

全体複雑度レビュー（2026-08-05）で実測。バグ族の系譜はB2→Y1〜Y3→Y5→Z5→Q3で、いずれも「新しい変換種や新しいサイトが手動連鎖から漏れる」同型である。
