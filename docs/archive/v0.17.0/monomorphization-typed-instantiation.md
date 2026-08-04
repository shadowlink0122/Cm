---
title: モノモーフ化の型駆動化（名前マングリング逆算の廃止）
parent: v0.17.0 Design
---

# モノモーフ化の型駆動化（名前マングリング逆算の廃止）

## 概要

モノモーフ化（src/internal/mir/lowering/mono/、2,290行）は特殊化の同定・生成・呼び出し書き換えの多くを「マングル済み文字列名の解析」で行っており、名前の組み立て・分解・パターン照合が82箇所ある。
C7/C8/C9で導入したtypekey（可逆型キー）が部分採用に留まり、`Container<int>__print` のテキスト解析・型パラメータ名をT/U/V/Wと仮定した名前再構築・`Box__Box__string` の型引数逆算（W5でも再実装）などの脆い経路が残っている。
rustcのモノモーフ化（型付きMIR上のInstance収集で、シンボル名はcodegen直前に一度だけ生成する）に合わせ、特殊化の同定を型ノードのみで行い、名前からの型逆算を全廃する。

## 現状の実測と問題

- 名前操作82箇所: `find("__")`・substr・`<`探索によるベース名/型引数/メソッド名の切り出しがscan/specialize/rewrite全域に分布する。
- 型パラメータ名の仮定: 呼び出し名 `Container<int>__print` から総称名を再構築する際にパラメータ名をT/U/V/Wと仮定しており、ユーザーが別名（`<Elem>`等）を使うと照合に失敗してフォールバック探索へ落ちる。
- 逆算の再実装: get_type_name/make_type_from_name/normalize_type_arg/interp_specialized_struct_name/decode_specialized_type_name（W5）と、同じ「名前⇔型」変換ファミリが5系統ある。
- 推論の脆さの実績: N2はT[]引数の型名が空文字列になり推論不能→T=T自己推論→無置換特殊化という名前ベース経路の欠陥3連鎖だった。
- デストラクタ書き換え・ctor書き換え・自己再帰書き換えがそれぞれ独自の名前パターン照合を持つ。

## 簡素化方針

1. 特殊化の同定キーを `(総称シンボルID, std::vector<hir::TypePtr>)` に統一する（typekeyのencode/decodeは診断表示とシンボル名生成の終端でのみ使用）。
2. 呼び出しサイトの型引数は、型付きHIR（typed-hir-single-source.md）の呼び出し式に注釈された実引数型・期待戻り値型から直接取る（MIRローカル名やマングル名からの推論を廃止）。
3. 特殊化関数の生成は型置換マップ（パラメータID→TypePtr）で行い、置換完了の検証（Generic型ノード不在）を生成直後にアサートする（無置換特殊化の構造的防止）。
4. 呼び出し書き換えは呼び出しサイト表（総称シンボルID+型引数→特殊化ID）の引き当てのみとし、名前パターン照合（`>__ctor`・`__dtor`・`Container<...>`解析）を削除する。
5. シンボル名（`Box__int__get`等）はcodegen直前のマングラ1箇所（base/mangle.hpp拡張）でのみ生成し、生成後の名前を解析するコードを禁止する。

## 段階分割

1. 第1段: 特殊化要求の収集を型キー化する（needed mapのキーを文字列vectorからTypePtr vectorへ。既存のtypekey encodeで互換シンボル名を維持）。
2. 第2段: 呼び出しサイト表による書き換えへ移行し、名前パターン照合4系統（simple/メソッド/ctor/dtor）を削除する。
3. 第3段: 置換完了アサートの常時化と、名前→型逆算ヘルパ5系統の削除（残存参照は診断表示のみに限定）。

## テスト計画

- generics全スイート（52ファイル）とtypekey unit回帰を各段で完走させる。
- ユーザー定義の型パラメータ名（T以外）を使う総称メソッド・ctor・dtorの回帰を追加する（現行の仮定照合が拾えないケースの固定）。
- 無置換特殊化（Generic残存）がアサートで検出されることをregressionで固定する。

## 進捗

### 第1段（特殊化要求の型キー化）: 実装済み

- 特殊化要求をSpecRequest（総称シンボル名+型引数ツリーvector<hir::TypePtr>+呼び出しサイト表）へ再編し、neededマップのキーを「特殊化シンボル名（arg_symbol_keyエンコード済みのため一意）」にした。文字列型引数の運搬・カンマ分割正規化・make_type_from_nameによる文字列→型の復路は全廃した。
- 呼び出しサイトの型引数推論を構造的単一化（unify_type_param）へ置換した。パラメータ型×実引数型（MIRローカルの型ツリー）をPointer/Array/Reference要素・Structのtype_argsで再帰照合し、名前文字列の切り出し（"int[]"末尾除去・"Pair__int__string"再分解）を廃止した。自己推論（T=T）とGeneric残存束縛は構造的に除外される。
- ローワリングが埋め込むマングル済み呼び出し名（表示形Container<int>__print・フラット形Vector__int__init・混在形Vector__Vector<TrackedItem>__len）の復元はdecode_type_name 1箇所へ集約した（フラット名はhir_struct_defsの総称パラメータ数でparse_flat_type_args復元、表示形は既存デコーダ）。旧scanの4系統パターン照合（simple/メソッド/ctor/dtor/フラット）は基底名+サフィックス照合+decode境界の単一ループへ統合した。
- 型置換マップはSpecRequestの型引数ツリーから直接構築し（specialize.cpp）、implメソッドのself型はstruct_symbol_key(基底, 型引数ツリー)で確定する。N2の宛先ローカル型パッチも文字列復路を廃して型引数ツリーを直接代入する。

### 第2段（呼び出しサイト表による書き換え）: 実装済み

- rewrite_generic_callsを「スキャンが記録した（呼び出し元, ブロック）サイトへ特殊化シンボル名を直接引き当てる」だけに書き換えた。全呼び出しの再走査・呼び出し名の再解析・型パラメータ名をT/U/V/Wと仮定した総称名の再構築・ポインタ表記の文字列正規化（__*→__ptr_）は削除した。
- デストラクタ書き換え（temp-drop等が基底名で発行するBase__dtor）は、selfポインタ実引数のローカル型ツリーからstruct_symbol_keyで特殊化名を引き当てる型駆動に置換した（表示名の文字列munging廃止）。
- 回帰追加: tests/common/generics/param_names_non_t.cm（型パラメータ名Elem/Key/Val/Itemの総称構造体メソッド・複数パラメータ・総称関数）。従来のT/U/V/W仮定では照合に失敗していたケースを固定した。

### 第3段（置換完了検査と逆算ヘルパの削除）: 実装済み

- 特殊化関数の生成直後に全ローカル型のGeneric残存を検査し、違反を警告出力する常時検査を追加した（無置換特殊化=N2根因の構造的検出。出力比較テストが警告を検出するため全スイートがアサートとして機能する）。
- 名前→型逆算ヘルパを削除・集約した: normalize_type_arg（文字列のポインタ/ネスト正規化）とmake_specialized_struct_name（文字列版）・make_specialized_name（文字列版）を削除し、シンボル名生成はarg_symbol_key/struct_symbol_key/make_specialized_name（型引数ツリー版）の終端1系統に統一した。interp_specialized_struct_name・decode_specialized_type_name（W5）はtype-resolution-simplificationで削除済み。残る名前→型変換はdecode_type_name（ローワリング由来のマングル済み呼び出し名の復元境界）とmake_type_from_name（その下請け）のみ。

### 残存する構造の記録

- MIRローワリングがメソッド呼び出し名をマングル済み文字列（フラット/表示混在）で発行する構造は残っており、scanのdecode境界はこの共存のために存在する。呼び出しサイトが型引数ツリーを直接運搬する形（HIR呼び出し注釈のMIRへの伝搬）へ改める場合、decode境界も削除できる（将来課題）。
- fixup_println_dispatchとN2宛先型パッチは「総称本体がT型のままMIRへローワリングされ、特殊化時に呼び出し側ローカルの型が確定する」順序に由来する補正で、上記の呼び出しサイト型運搬と同時に削除できる（現時点では機能的に必要。文字列復路は排除済み）。
- 検証: generics/collections全件・interpreterスイート612件（失敗0）・llvm・llvm-wasm・js・sv・unit・regression・cm-test・libs・i18n・sanitize・tsの全スイートを完走した。名前操作箇所は82箇所からdecode境界・typekeyエンコーダ等の境界系66箇所へ減少し、特殊化の同定・生成・書き換えの経路から名前解析を排除した。
