---
title: メソッド解決の一元化（キー正準化とresolve_method API）
parent: v0.17.0 Design
---

# メソッド解決の一元化（キー正準化とresolve_method API）

## 概要

メソッド解決が4つの独立機構——type_methods_文字列キー表・interface_methods_表・配列/文字列ビルトインのハードコード分岐・演算子のimpl_interfaces_集合判定——に分裂し、レシーバ型からメソッド表キーを計算するコードが12箇所で別実装になっている。
未修正バグ調査のQ1（for-inプロトコル穴）・Q4（impl for Add内部エラー）・Q5（enumメソッド不能）はいずれもこの分裂の症状であり、個別修正しても次のレシーバ種別追加で同族が再発する構造である。

## 実測した分裂の内訳

- **キー計算12箇所**: 登録側はdecl.cpp:733/751（`type_to_string(target_type)`を未正規化のまま使用）とauto_impl.cpp（呼び出し元文字列）、参照側はmethod.cpp:78-86（フル名+namespace剥ぎの2変種）・:134-144（ジェネリックキーを`name<T,U>`形へ再構築）・:262-274（enum基底を`<`と`__`で剥ぎ）・:709/:742（配列キー）、function.cpp:269/:288（静的呼び出し・ジェネリック再構築の複製）、stmt.cpp:687/699（for-in）。
- **登録キーと参照キーの構造的不一致**: 登録は未正規化（enum名のまま）、参照時のレシーバ型はlet時点でenum→int正規化済み（utils/compat.cpp:56-70）のため、値enumのメソッド表は恒久的に到達不能——Q5の真因。
- **ジェネリックキー再構築の複製**: method.cpp:134とfunction.cpp:288が登録形とバイト一致必須の文字列組み立てを別々に持つ。
- **namespace方式3種**: 参照側のsuffix剥ぎ（method.cpp:79）・prefix取り（function.cpp:264）・resolve_in_namespaceが併存。
- **types/内の全throw4件が診断でなく例外**: decl.cpp:665/668/734/792はbuild.cpp:273のcatchで「internal error (typecheck)」として漏れる——Q4の真因（うち:665と:792は同一検査の二重実装）。
- **for-in（stmt.cpp:669-759）はiter存在だけで確定**: has_nextは一切検索せず、nextが無くてもuse_iterator=trueのまま進む——Q1の真因。
- **演算子は別世界**: infer_binaryはtype_methods_の`operator+`を引かずimpl_interfaces_の集合員判定のみで、auto_implが書いた演算子MethodInfoは型検査では未消費。

## リファクタリング方針

1. **正準キー関数**: `std::string method_table_key(const ast::TypePtr&)` を1つ定義し、登録（register_impl/auto_impl）と参照（infer_member/静的呼び出し/for-in/演算子）の全側で共有する。enum・ジェネリック・namespace・配列の剥ぎ/再構築ロジックをこの1関数へ畳む。
2. **統一解決API**: `resolve_method(recv_type, name) -> optional<Resolution>` を新設し、infer_memberの9分岐・function.cppの静的分岐・for-inのiter/next検索を同一入口へ集約する。ビルトインラダー（method.cpp:403-906の約500行）も段階的に表駆動へ移す。
3. **例外の診断化（実施済み）**: types/内のthrow4件はQ4修正でerror(span, …)へ置換済み（[arith-operator-interface-decl.md](../../archive/v0.17.0/arith-operator-interface-decl.md)。二重検査はregister_impl側で診断・check_impl側は打ち切りのみに整理）。
4. **for-inプロトコル検証（実施済み）**: iter発見時のhas_next(): bool・next()の存在/シグネチャ検査はQ1修正で実装済み（[forin-iterator-protocol-checks.md](../../archive/v0.17.0/forin-iterator-protocol-checks.md)）。現状はtype_methods_直接参照のため、resolve_method API導入時にその上へ乗せ替える。
5. **enum正体の保持（Q5は名前保持方式で実施済み）**: Q5修正は正規化の全面遅延でなく「int解決時にenum名をnameへ保持する」ピギーバック方式で実装した（[enum-inherent-impl-methods.md](../../archive/v0.17.0/enum-inherent-impl-methods.md)の実装記録を参照。表示・互換・codegenはkind駆動のため不変）。正規化遅延そのもの（解決キー計算の後段化）はメソッド表キー正準化と合わせて本提案の残課題とする。

## 段階分割

- 第1段（実施済み）: throw4件の診断化（Q4修正）とfor-inプロトコル検証（Q1修正）。いずれも統一APIを待たずに実施した。
- 第2段: method_table_key共有化とresolve_method導入。ジェネリックキー再構築2箇所・namespace3方式をこの段で1本化する。
- 第3段: enum正体保持（Q5修正）と演算子解決のresolve_method統合（演算子はLHS型を返す現挙動を保存する）。

## テスト計画

- レシーバ種別（構造体/ジェネリック特殊化/enum/ユニオン/インターフェース/配列/文字列/namespace修飾）×呼び出し形（値レシーバ/静的/for-in/演算子）の解決マトリクス。
- エラーテスト: 未宣言インターフェースimpl・重複impl・重複メソッド・has_next欠如・next欠如。

## 検出経緯

全体複雑度レビュー（2026-08-05）のメソッド解決調査で、Q1/Q4/Q5の真因を含む分裂構造として実測した。
