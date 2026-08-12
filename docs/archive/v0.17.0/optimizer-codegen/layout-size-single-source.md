---
title: 型サイズ照会の一本化（見積もり実装の削除）
parent: v0.17.0 Design
---

# 型サイズ照会の一本化（見積もり実装の削除）

## 概要

型サイズを答える実装が4系統あり、うち2つは自認する「見積もり」である。

1. **HIR calculate_type_size**（hir/lowering/impl.cpp:496-694）: sizeof式の解決に使われるが、ジェネリック構造体はフィールド数×8で見積もり、定義が見つからないと**暫定256バイト**を返す（:688、「Phase 1: 緊急修正」コメント付き）。
2. **MIR LoweringContext::calculate_type_size**（mir/lowering/context.cpp:230-286）: sizeof_for_Tマーカー処理用で、構造体をフィールド数×8で見積もり、`__`分割によるベース名検索（:267-268）というマングリング名逆算まで含む。宣言側コメント（context.hpp:239）が「見積もりでありレイアウト非互換」と自認している。
3. **mono calculate_specialized_type_size**（mir/lowering/mono/typeinfo.cpp:270）: スライス要素ストライドとsizeof_for_T用の第3実装で、フラット名逆算（resolve_struct_field_types→parse_flat_type_args）に依存する。
4. **真実のlayout系**: layout_size/layout_align（context.cpp:559-609、登録済みMirStructのフィールドを自然アライメントで畳む）とlayout.hppのストライドAPI（Z2/Y6で統一済み）。

sizeofがユーザーに見える値を1〜3の見積もりで答えうる構造であり、フィールド数×8はパディング・実型幅と乖離する。
Y6/Z2で「要素ストライドはlayout API 1系統」と統一した直後に、サイズ照会側で同じ分裂が残っているのは非対称である。

## リファクタリング方針

1. **sizeofの解決をMIRへ遅延**: HIR側はsizeofを常にマーカー（既存のsizeof_for_T機構の一般化）として発行し、MIRで型が確定した時点でlayout_size 1系統により解決する。HIRのcalculate_type_size（:688の256暫定を含む）を削除する。
2. **MIR側見積もりの置換**: LoweringContext::calculate_type_sizeの構造体分岐をlayout_size委譲へ置き換え、`__`分割ベース検索を削除する（mono-flat-name-elimination.mdの棚卸し対象と同一箇所）。
3. **mono第3実装の統合**: calculate_specialized_type_sizeを、特殊化構造体がMirStruct登録済みであることを前提にlayout_size/strideへ委譲し、フィールド型のフラット名復元を廃止する。
4. スカラ・ポインタ・配列のプリミティブ幅はlayout.hppへ集約し、target_pointer_size分岐を1箇所にする。

## テスト計画

- sizeof値の回帰マトリクス: パディングを持つ構造体（char+int等）・ジェネリック特殊化・ネスト特殊化・固定長配列・ユニオンを、native/jit/wasm32で期待値固定して検証する（フィールド数×8では通らない非正方ケースを必ず含める）。

## 検出経緯

全体複雑度レビュー（2026-08-05）。「暫定256バイト」「フィールド数×8」の見積もり経路がsizeofというユーザー可視の値に到達しうることを実査で確認した。
## 実装記録（2026-08-08）

見積もり実装がユーザー可視のsizeofへ到達する問題を解消した。実測ではsizeof(Pair<char, int>)=16（正8）・sizeof(Nested<char>)=24（正3）で、パーサが具体化インスタンスをTypeKind::Struct（type_args付き）で渡すため、HIRのStruct分岐が未置換フィールド型（仮引数名→default 8バイト）で畳んでいたのが直接の発生源だった（設計文書が指摘したGeneric分岐のフィールド数×8とは別サイトで、同族の見積もりが2箇所あった）。

- HIR calculate_type_size/calculate_type_align: Struct/Generic両分岐へ「型引数を仮引数へ束縛→フィールド型を再帰置換→実レイアウトを畳む」を実装した（substitute_generic_args/build_generic_subst。UnionType等のType派生はコピーがスライスするため置換対象外）。フィールド数×8の見積もりと暫定256バイトのフォールバックを削除し、未解決型パラメータのみポインタサイズを仮定する（ジェネリック関数内のsizeof(T)は従来通りsizeof_forマーカー経路がモノモーフ化時に実レイアウトで解決）。
- MIR LoweringContext::calculate_type_size: フィールド数×8見積もりと`__`分割によるベース名逆算（mono-flat-name-eliminationの棚卸し対象と同一箇所）を削除し、真実のlayout_size 1系統への委譲サンクにした。layout_sizeへReferenceキンドのポインタ幅を追加（wasm32での8/4不整合の芽を除去）。
- 方針1（sizeofのMIR全面遅延）は採らなかった: 具体化インスタンスのマーカー化は非ジェネリック関数内で解決者が存在せず、マーカー文字列の型名復元（フラット名逆算と同族）を新設することになるため、HIR側での置換レイアウト計算の方が単純かつ逆算ゼロで済む。
- mono calculate_specialized_type_size（typeinfo.cpp）は変更せず現役維持: sizeof_forマーカー解決の正実装であり、既にフィールド実レイアウトで畳む（コメントに旧フィールド数×8の害が記録済み）。MirStruct登録前の型にも効く必要がありlayout_size委譲は前提が合わない。

結果、サイズを答える実装は「HIR（具体型・置換レイアウト）・mono typeinfo（マーカー解決）・layout系（MIR真実）」の3つで全て実レイアウト計算になり、見積もり実装は0になった。

テスト: tests/common/types/sizeof_generic.cm（非正方Pair<char,int>/<char,char>・ネストNested<char>/<long>・固定長配列フィールドWithArr<int>/<char>・ポインタフィールドのwasm32差は.expect.llvm-wasmで固定）。全13スイートPASS。

残課題: プリミティブ幅のlayout.hpp集約（target_pointer_size分岐の1箇所化）は各実装が同値を返すため実害なしとして見送り（方針4）。HIRとMIRの構造体レイアウト畳み込みは同アルゴリズムの2実装が残る（層をまたぐ共有はHIR→MIR依存の逆転が必要で、layout-size部分の共通ヘッダ抽出は将来課題）。
