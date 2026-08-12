---
title: 自動実装（with/derive）のソース展開化（手組みMIR生成の廃止）
parent: v0.17.0 Design
---

# 自動実装（with/derive）のソース展開化（手組みMIR生成の廃止）

## 概要

`with Debug` / `#[derive(...)]` の自動実装（Debug/Display/Eq/Ord/Clone/Hash/Css）は、MIR lowering段でMIR命令列を手で組み立てて生成している（auto_impl群、計4,408行）。
手組みMIRは通常経路のlowering改善（dropパス・境界チェック・型付け規約）が自動では適用されず、フィールド型が増えるたびにMIR構築コードへ型分岐を足す構造になっている。
rustcのderiveがASTへの展開（生成コードは通常のコンパイル経路を通る）であるのと同じく、自動実装をCm ASTの合成へ置き換え、生成後は通常のパイプラインに任せる。

## 現状の実測と問題

- auto_impl群4,408行がMirStatement/BasicBlockを直接構築しており、モノモーフィズド構造体用の再生成（generate_monomorphized_auto_impls）や補間との連携（旧経路がdebug/toString整形に依存: type-resolution-simplification.md領域1の除外リスト）など、特別扱いが波及している。
- 通常経路の改善が届かない実績: H9のSDS化・C12のdropパス・B2の数値変換挿入などはlowering経路ごとに適用したが、手組みMIRは独立経路のため都度の目視確認が必要だった。
- フィールド型の網羅switch（スカラ各種・string・ネスト構造体・配列・スライス）がDebug/Eq/Ord/Clone/Hashへ繰り返し出現する。

## 簡素化方針

1. 自動実装をAST合成へ変更する: `with Debug` を検出したら、対応する `impl S { string debug() { ... } }` のASTを型情報から合成し、通常の型検査→HIR→MIRに流す（rustc_expandのderive展開に相当）。
2. 合成コードはCm言語の式（フィールド参照・文字列連結・比較演算子・再帰メソッド呼び出し）だけで書けるため、フィールド型switchは「型がdebug()を持つか・スカラか」程度の分岐に縮む。
3. モノモーフィズド構造体用の再生成は、総称実装のASTを1つ合成してモノモーフ化に任せる形へ置き換える（特殊化ごとの手動再生成を廃止）。
4. 補間の旧経路が持つdebug/toString特別扱い（除外リスト）は、補間脱糖（領域1）完了後に通常メソッド呼び出しとして自然合流する。

## 期待効果

- auto_impl群4,408行の大半を、テンプレート的なAST合成（数百行規模）へ置換できる。
- 生成コードがdropパス・境界チェック・診断・将来の最適化を自動で受ける（独立経路の保守が消える）。
- 新しいderive対象（例: Serialize系）の追加コストが「Cmでどう書くか」の設計だけになる。

## 段階分割

1. 第1段: Eq（最小の演算子1つ）をAST合成へ移行し、生成MIRの等価性と全スイートで挙動不変を確認する。
2. 第2段: Ord/Clone/Hashを移行する。
3. 第3段: Debug/Display/Css（文字列整形系）を移行し、補間側の特別扱い解除（領域1完了後）と合わせて旧経路を削除する。

## テスト計画

- with_debug/derive系の既存スイートを各段で完走させる（出力完全一致）。
- ネスト構造体・スライスフィールド・ジェネリック構造体のderiveケースを追加し、モノモーフ化経由の生成が単一総称実装で賄えることを検証する。
- 生成ASTのスナップショット（合成コードの可読な形）をregressionへ追加し、意図しない生成変化を検出する。

## 進捗

### 第1段（Eqのソース展開）: 非ジェネリック構造体で実装済み

- `src/internal/macro/derive.cpp` に展開器を新設した（macro層=rustc_expand相当の配置）。`synthesize_derive_impls` が `impl S for Eq { operator bool ==(S other) { return self.a == other.a && ...; } }` のCmソースを合成し、`expand_derives` がパース検証のうえASTへ追加する。build.cppのターゲットフィルタ後・型検査前に展開する。
- 展開済みトレイトは構造体のauto_implsから除去する。合成implが唯一の実装となり、型検査の重複impl検出・インターフェース適合（T: Eq境界含む）・impl_info登録・Pass 6の構造体比較書き換えがすべて通常implの経路で機能する（手組みMIRのEq生成は非ジェネリックについて廃止）。
- 空構造体はtrue、ネスト構造体フィールドは相手のop_eq経由、stringフィールドは内容比較と、手組み実装と同一の意味論を全バックエンドで確認した。固定長配列フィールドは要素単位の比較へ再帰展開する（配列全体の==は通常経路に存在せず生ICmpの不正LLVMになるため。多次元は添字を重ねる）。
- regression: 合成ソースのスナップショット検証6件（通常・空・配列フィールド展開・ジェネリック除外・auto_impls除去・未移行トレイト残置）を追加した。

### 発見・修正した既存バグ

- `lower_operator`（mir/lowering/impl.cpp）のselfパラメータがimplターゲットの表記文字列（`Wrap<T>`）で型付けされており、ジェネリック演算子impl内の `self.x` がフィールド解決不能（<error>型）となり**代入文ごと黙って欠落**していた（otherパラメータはHIR解決済みの基底名で非対称）。基底名での型付けへ修正した。

### 判明した制約（ジェネリック構造体を第1段の対象外とした理由）

- ユーザー定義の総称演算子impl（`impl Wrap<T> for Eq`）は、総称演算子関数がモノモーフィゼーションの特殊化対象に登録されず、比較式の書き換え（Pass 6）もimpl_infoの特殊化キーを持たないため、**呼び出し側が生の構造体比較のまま残り誤コンパイルする**（従来から動作していなかった）。方針3（単一総称実装への置き換え）はこの演算子モノモーフ化対応が前提となるため、ジェネリック構造体のEqは従来どおりモノモーフ化後の手組み生成を維持した。演算子のモノモーフ化対応はmonomorphization-typed-instantiation.mdの実装と合わせて扱う。

### 第2段（Ord/Clone/Hash）・第3段（Debug/Display/Css）: 非ジェネリック構造体で実装済み

- 実働の手組み実装（mir/lowering/auto_impl.cpp系）から意味論を抽出し、全トレイトを合成テンプレートへ移植した: Ord=辞書式（各フィールドで<ならtrue・>ならfalse・等しければ次へ、全等false）、Clone=自己の値コピー（集約コピー意味論に委譲）、Hash=FNV-1a（基数0x811c9dc5・素数16777619。基数はi32ビットパターン維持の負数リテラル。整数/bool/charはas intで混合、ネスト構造体はhash()再帰、固定長配列は要素展開）、Debug="S { f: v, ... }"、Display="(v1, v2)"、Css=kebab名昇順の"key: value; "連結（boolは真のとき"key; "・ネストは"key { ... } "・to_cssエイリアスとis_css/isCss両名=呼び出し解決名差の吸収）。
- 展開はauto_implsからの除去で従来経路を自然に無効化する（MIR側の変更不要）。非ジェネリック構造体のwith/deriveは全トレイトが通常パイプラインを通る。展開器はcheckerと同一のフィールド型妥当性規則を持ち、不正なderive（Hash×string等）は展開せずauto_implsへ残して従来の検証診断（Cannot derive ...）を発火させる。
- 死んだコードの削除: 実働と別系統で存在した未使用のAutoImplGeneratorクラス（mir/lowering/auto_impl/ディレクトリ5ファイル+auto_impl.hpp、計約1,700行。どこからも実体化されていなかった）を削除した。
- regression: スナップショット検証を10件へ拡充（Ord/Hash/Debug連結形/Cssソート・エイリアス含む）。

### 発見・修正した既存バグ（第2・3段の過程）

- 波括弧エスケープと補間の併用が全形態で壊れていた（`"{{a}} {x}"`→`"5 {} {b}"`等）。真因は2つ: (1)コード生成のformat展開がエスケープ復元（cm_format_unescape_braces）をプレースホルダ置換の**前**に実行し、復元済みリテラル{a}が置換対象に誤認されていた（print_codegen.cppの2サイトで置換→復元へ順序修正） (2)ランタイムのcm_format_replace系16変種（native/wasm）が最初の'{'を無条件にプレースホルダ開始とみなしていた（{{スキップのスキャンへ統一。native=cm_find_placeholder_startヘルパ・wasm=ループ7箇所・cm_format_string可変長版も{{素通し）。回帰: tests/common/basic/interp_brace_escape（native/js一致）。
- 補間の逐次置換は挿入値が波括弧を含む場合に後続プレースホルダを誤認する一般問題が残る（`"{a} {b}"`でaの値が"{...}"を含むとbの挿入位置がずれる）。生成コードは連結形（ネスト・文字列フィールドは直接連結、スカラのみ単独プレースホルダ）で回避した。一般修正は挿入位置を追跡する置換APIが必要で、将来課題として記録する。

## 解決記録

- 全3段の移行を完了し、非ジェネリック構造体のwith/derive自動実装は手組みMIR生成を離れて通常パイプライン（合成ソース→型検査→HIR→MIR→最適化・drop・診断）で処理される。本文書はarchiveへ移動する。
- ジェネリック構造体は総称演算子implのモノモーフ化未対応（進捗節に記録済み）のため手組みのモノモーフ化後生成（auto_impl.cpp系のfor_monomorphized群）を維持する。単一総称実装への移行と手組み経路の完全削除は、演算子モノモーフ化対応（monomorphization-typed-instantiation.md）とセットの将来課題とする。
- 補間の挿入値波括弧問題の一般修正（置換位置追跡API）も将来課題として記録する。
