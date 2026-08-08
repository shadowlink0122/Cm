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

1. **即修（R1・実施済み）**: `struct_symbol_key`のsimple高速パスを、複数引数基底で引数キーが`__`を含む場合（=引数自体が特殊化）に`$`エンコード分岐へ強制した（1引数基底は結合特例で可逆のためフラット名を維持）。Q2自体はこの修正と内側リテラル型注釈の上書き抑止（checker側）で修正済み（[nested-generic-type-arg-string.md](../../archive/v0.17.0/type-system/nested-generic-type-arg-string.md)の実装記録を参照）。
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
## 実装記録（逆算読者のtypekey統一とC8実害修正・2026-08-08）

R1（struct_symbol_keyの強制$エンコード）後に残っていた逆算読者の非対称と、テスト計画にあった衝突ケースの実害を処置した。

- **parse_flat_type_argsの全6呼び出しサイトをtypekey優先へ統一**: 従来$エンコード名を可逆復号してから逆算へフォールバックするのはサイズ/フィールド読者2箇所のみだった。残る4箇所（scan.cppのdecode_type_name・mono_structs.cppの特殊化検出とto_symbol_type・specialize.cppの特殊化生成）へis_encoded_key分岐を追加し、$名がヒューリスティック逆算へ渡る経路を全廃した（検出ガードの`__`前提も$対応へ拡張）。
- **scan.cppのサイレントドロップへ痕跡**: 型引数の件数不一致で呼び出しサイトの特殊化要求が無言破棄される箇所へデバッグログと「無置換特殊化の常時検査が下流の検出網」の明文化を追加した。
- **C8衝突の実害を発見・修正**: テスト計画の「ユーザー定義`Box__Box__int`風名前との衝突検査」を書いたところ、native/jitで実害が再現した（`u.marker`が111でなく壊れた値。jsは正値・修正前のバイナリでも同一）。真因はexpr/access.cppのメンバアクセスが**完全名がユーザー定義構造体かのC8検査なしに**先頭`__`でベース分割し、ユーザー構造体のフィールドをジェネリック基底（Box<T>）の型で誤再型付けしていたこと。完全名がstruct_defsに在る場合は分割しないガードを追加した。回帰はnested/flat_name_collision.cm（ユーザー定義Box__Box__intとBox<Box<int>>の共存・native/js一致）。

**残り（R2/R3の構造課題・実害なし）**: parse_flat_type_args本体とmono_internal.cppのフラット名産生/消費分岐の削除（単引数フラットは結合特例で可逆のため現在は正しく動作する）、`__`前提のベース名抽出約30箇所の共通ヘルパ化（$名は`__`を含まないため誤動作はしないが基底を剥がせない）、display_nameによる同定/表示の分離徹底。全サイトのtypekey全面化はシンボル名の一斉変更を伴うため、専用の検証ターンで実施する。
## 実装記録（ベース名抽出の正準関数化・2026-08-08）

方針3（ベース名抽出の共通関数化）を実施した。

- `typekey::spec_base_name(name)`を新設した（$エンコード名は$前・フラット名は最初の__前・素名はそのまま）。構造体名の基底抽出サイトのうち`__`前提で$名を剥がせなかった箇所——context.cppのデストラクタ登録のテンプレート基底抽出（$特殊化要素型のdtor解決が届かなかった）と素名ガード・context.cpp/base.cppのモノモーフ化enum基底フォールバック・LLVM types.cppのenum mono基底正規化——を正準関数へ置換した。
- expr/access.cppの複製フラット引数抽出（型引数が空のマングリング名から__分割で型引数を復元する独自ループ）へ、typekey可逆復号の優先分岐を追加した（$名はdecode_type_argsで構造的に復元し、フラット名のみ従来ループへ）。
- メソッド名分割（Type__methodのrfind/find("__")）は構造体基底抽出と意味論が異なるため対象外とした（$名は__を含まないため現行の分割は$名でも正しく機能する。この区別は本文書の棚卸し分類に追記済みの前提）。
- 全13スイートPASS。

残り: フラット名産生の全廃（struct_symbol_keyの単引数フラット経路とmono_internal.cppの文字列置換分岐の削除・parse_flat_type_args本体の削除）。産生側の$全面化はシンボル名の一斉変更を伴うため、無置換特殊化検査とマングリング衝突ハードエラーを検出網とした専用の検証枠で実施する。
## 実装記録（産生チョークポイント化とドメイン橋・$全面化の移行計画確定・2026-08-08）

産生の$全面化を実際に試行し、その過程で挙動保存の一元化を完了するとともに、全面化に必要な移行順序を実測で確定した。

**完了（挙動保存・全13スイートPASS）:**
- mono_internal.cppの特殊化名産生5分岐（置換後の再フラット化・埋め込みパラメータ置換・角括弧形・素名+subst・$名の復号置換）を、struct_symbol_keyチョークポイント経由へ一本化した（従来は各分岐が独自の文字列連結を持ち、キー規約変更が5箇所の追従を要した）。$エンコード名の型パラメータ置換（復号→置換→再エンコード）も追加。
- typekeyへ関数名ドメインとの橋（arg_key_from_tree・struct_key_from_tree・spec_fn_prefix）を追加し、derive生成関数名（auto_impl系12箇所）・演算子呼び出し名（binary op_eq/op_lt）・要素デストラクタ名（specialize）をspec_fn_prefix経由へ統一した（現行のフラット既定ではno-op＝挙動同一。$移行時にドメイン変換が一括で効く）。
- mono_structsの特殊化検出でフラット名ローカルを正準キーへ収束改名する処理・M15要素dtorフォールバックとscan単一化の$対応・dtorレジストリの基底抽出のドメイン明確化（登録名は関数名ドメインのため最初の__を基底区切りとして優先）・auto_implのMirStruct基底/表示名のspec_base_name化を実装した。

**試行で確定した結合の全容（$全面化の移行順序）:**
struct_symbol_keyを常時$へ切り替えると全13スイートで約20件が段階的に露出した。フラット規約は「フラットfn接頭辞（base__argkey__method）＝フラット構造体キー」という偶然の一致に依存しており、以下が独立にこの一致へ結合している——(1)HIR期に構築されるメソッド呼び出し名（checker/expr_memberのmangle）、(2)derive生成関数名とその呼び出し側、(3)演算子呼び出し名、(4)dtor登録名（let/emit_destructors）、(5)LLVM types.cppの型引数付き構造体のフラット名自作（不一致時はtypedef-unionの8バイトフォールバック形状へ落ちる）、(6)イテレータ等のフィールドアクセス経路。移行は次の順で行う: ①関数名ドメインを全サイト（呼び出し側・生成側）でspec_fn_prefix正準化 → ②codegenの型参照を正準キー化（enumはTagged Union経路のため除外ガード） → ③キー産生の$切替（本試行のパッチ再適用） → ④parse_flat_type_argsとフラット読者の削除。今回は①の生成側とドメイン橋まで完了し、②③④は上記計画とパッチ内容を本記録に固定した上で復元した。
