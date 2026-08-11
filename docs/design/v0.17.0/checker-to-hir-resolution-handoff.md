---
title: 型検査の解決結果をHIRへ引き渡す（lower_member再導出の廃止）
parent: v0.17.0 Design
---

# 型検査の解決結果をHIRへ引き渡す（lower_member再導出の廃止）

## 概要

ast::MemberExpr（syntax/ast/expr.hpp:277-284）はobject・member・is_method_call・argsしか保持せず、checkerが完全に解決したメソッド対象・ジェネリック置換後の戻り値型・フィールド情報を捨てている。
その結果、HIRのlower_member（hir/lowering/expr_member.cpp:85、単一関数で約1100行）が名前と型から解決を全て再導出しており、Result/Option基底名の`__`剥ぎがchecker（types/checking/call/method.cpp:260-270）と逐語複製（expr_member.cpp:128-131）になるなど、同じ事実の二重実装が積み上がっている。
typed-hir-single-source（archive済み）は「型検査後のHIRは全式が型付き」を確立したが、型だけでなく**解決結果**も単一ソース化しないと、checkerとHIRの解決が乖離した瞬間に無言の誤lowering（W5・V系のチェーン族と同型）が再発する。

## 実測

- lower_memberの内訳: Result/Optionビルトイン脱糖（:119-229、タグ再検索・ペイロード型再導出込み）、配列ビルトイン群（:232以降）、文字列ビルトイン、ユーザーメソッド呼び出し——各分岐がobj_typeのkind/element_type/名前を再検査して挙動を選ぶ巨大switch。
- checkerは同じ判定をinfer_member（method.cpp:37-349の9分岐）で済ませており、結果を捨てて型だけ伝播している。

## リファクタリング方針

1. MemberExprへ解決注釈を追加する: 解決済みメソッドシンボル（マングリング済み関数名）・レシーバ基底型・ビルトイン種別（Array/String/ResultOption/User）・置換済み戻り値型。checkerのinfer_memberが確定時に書き込む。
2. lower_memberは注釈のディスパッチだけを行い、名前剥ぎ・タグ再検索・型再判定の再導出コードを削除する。
3. 不変条件検査（typed-hir-single-sourceのwalk検証）へ「MemberExprは注釈済み」を追加し、未注釈ノードの素通りを機械検出する。

## 効果とリスク

- 効果: lower_memberの大幅縮小（再導出分岐の削除）、`__`剥ぎ複製の解消、checker/HIRの解決乖離バグ族の構造的封止。
- リスク: AST↔checker↔HIRの契約変更でメンバ系テスト全域に触れる。メソッド解決一元化（../../archive/v0.17.0/type-system/method-resolution-unification.md）のresolve_method導入後に、その戻り値Resolutionをそのまま注釈として書く順序が最小コストである。

## テスト計画

- 既存のチェーン系回帰（H10系・W5系・補間内チェーン）全数と、注釈欠落を検出する不変条件unitテスト。

## 検出経緯

全体複雑度レビュー（2026-08-05）。lower_member 1100行は関数長スキャンの全ソース中最大で、checker再導出が主因と特定した。

## 実装記録（前提整備と逐語複製の解消・2026-08-11）

注釈導入の前提となる周辺整備が他文書の完遂で進み、本文書の具体的複製1件を解消した。

- **Result/Option基底剥ぎの逐語複製を解消**: checker側（strip_spec_suffix）とHIR側（expr_member.cppの`__`分割）が同じ意味論を別実装していた基底名抽出を、typekeyの正準関数`spec_base_name`（$エンコード名対応）への委譲で単一実装に統一した。
- **前提の充足**: 方針1の注釈ソースとなるresolve_method統一API（[method-resolution-unification.md](../../archive/v0.17.0/type-system/method-resolution-unification.md)）が導入済みになり、「その戻り値Resolutionをそのまま注釈として書く」最小コスト経路が利用可能になった。またHIR側のメソッド呼名構築は型ツリーからの正準構築（[mono-flat-name-elimination.md](../../archive/v0.17.0/type-system/mono-flat-name-elimination.md)の呼び出し名正準化）へ置換済みで、checker/HIRの名前ドメイン乖離の主経路は閉じている。
- 残: 方針1〜3の本体（MemberExprへの解決注釈・lower_memberの注釈ディスパッチ化・未注釈検出の不変条件）。着手時はresolve_methodのMethodResolutionを注釈型としてそのまま流用する。
