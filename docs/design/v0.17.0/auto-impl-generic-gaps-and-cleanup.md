---
title: derive自動実装の残存MIR生成の整理（死に体削除とSlice/Unionギャップ）
parent: v0.17.0 Design
---

# derive自動実装の残存MIR生成の整理（死に体削除とSlice/Unionギャップ）

## 概要

derive-as-source-expansion（archive済み）で非ジェネリック構造体のderiveはCmソース合成（macro/derive.cpp、build.cpp:234でexpand_derives）へ移行済みだが、MIR直生成系が約2,970行（auto_impl.cpp 979・auto_impl_compare.cpp 1022・auto_impl_css.cpp 約560・auto_impl_debug.cpp 約410）残っている。
実測の内訳は2種類で、扱いを分けるべきである。

1. **死に体（削除候補）**: 非ジェネリック用のgenerate_builtin_{eq,lt,clone,hash,debug,display,css}本体はexpand_derivesが正常系のderiveをauto_implsから除去するため事実上到達しないが、物理的に残置されている。
2. **現役（品質ギャップあり）**: ジェネリック特殊化用のgenerate_*_for_monomorphized（auto_impl.cpp:92-131）はモノモーフ化された各MirStructへMIRを直組みするが、フィールド型switchに**TypeKind::SliceとTypeKind::Unionの分岐が存在せず**（grep全ファイル0件）、スライスやユニオンをフィールドに持つジェネリック構造体のderiveは無言で誤ったMIR（生バイナリ比較等のデフォルト分岐）に落ちる。同じフィールド型switchがeq/ord/clone/hash/debug/display/cssで最大4ファイルに複製されており、新フィールド型対応は毎回複数箇所修正になる。

自動生成メソッド（Point__debug等）がhir_func_defsに存在しないための特例（checker.hppのauto_impl_info_・compat.cpp:514の適合判定・expr_call.cppの表ミス許容）も残っており、単一ソース化が完了するまで消えない。

## リファクタリング方針

1. **第1段（削除）**: 非ジェネリック用MIR生成器の本体を削除し、expand_derivesが処理しなかったderiveは（無言でMIR生成に落とすのではなく）診断で停止する。到達しないコード約1,500行の削減と、「どちらの生成系が動いたのか」の曖昧さの解消。
2. **第2段（ギャップ封鎖・実施済み）**: R21修正（[derive-generic-and-field-gaps.md](../../archive/v0.17.0/interfaces-derive/derive-generic-and-field-gaps.md)）で、特殊化時の置換後フィールド型検証（validate_derive_instantiation）によりSlice/Union/タグ付きenum型引数を型検査診断で停止するようにした。あわせてジェネリックのClone/Hash/Debug/Displayメソッド解決の配線と値enumフィールドのint意味論対応も実施済み。MIRパス自体へのSlice/Union対応（診断でなく動作）は第3段の単一ソース化に委ねる。
3. **第3段（単一ソース化の完遂）**: monomorphization-typed-instantiationの残課題であるジェネリック演算子implのモノモーフ化登録を実装し、ジェネリック構造体も単一のジェネリックimplソース合成→特殊化の経路へ載せて、*_for_monomorphized系と特例群（auto_impl_info_等）を全廃する。

## テスト計画

- ジェネリック構造体×フィールド型（スカラ/string/ネスト構造体/固定長配列/スライス/ユニオン）×derive種別（eq/ord/clone/hash/debug）のマトリクス。Slice/Unionケースは診断化済み（tests/common/errors/derive_generic_{slice,union}_arg.cm）、正常系はtests/common/interface/generic_derive_methods.cm。
- 第1段後にderiveスイート全数で生成系の切り替わりが無いことを確認する。

## 検出経緯

全体複雑度レビュー（2026-08-05）でderive-as-source-expansionの実装記録と現物を突き合わせ、宣言された削除（旧AutoImplGenerator約1,700行）とは別に残る現役系のギャップを特定した。
## 実装記録（第1段・2026-08-08）

第1段（非ジェネリック用MIR直生成の死に体削除）を実施した。

- clone/hash/debug/display/css/to_css/is_cssの非ジェネリック用生成器7本（約900行）を削除した。eq/ltの生成器2本は、ユーザー定義インターフェースの演算子auto-impl（generate_auto_operator_impl。==/<シグネチャを持つinterfaceのwith実装）が現役で利用しているため本体を維持した（概要の「約1,500行」見積もりとの差分はこの2本と、cloneが実測小さかったことによる）。
- ディスパッチの組み込みderive分岐は、無言で旧MIR直生成へ落とす代わりに「expand_derivesが消費すべき組み込みderiveの展開漏れ」の内部エラーで停止するようにした。これにより「どちらの生成系が動いたのか」の曖昧さが消え、将来expand_derivesに漏れが生じた場合は全スイートが即検出する。
- 検証: 全13スイートPASS（deriveスイート全数で内部エラーの発火ゼロ＝非ジェネリック組み込みderiveが全てソース展開経路で処理されていることの実証）。

残り: 第3段（ジェネリック演算子implのモノモーフ化登録を実装し、ジェネリック構造体も単一のソース合成→特殊化経路へ載せ、*_for_monomorphized系と特例群（auto_impl_info_等）を全廃する）。
