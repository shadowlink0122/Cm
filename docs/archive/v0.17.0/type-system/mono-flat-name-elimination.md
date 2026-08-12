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
- checker/HIRの重複剥ぎ: types/checking/call/method.cpp:260-270とhir/lowering/expr_member.cpp:128-131が逐語複製（この重複はchecker-to-hir-resolution-handoff.mdで扱い、spec_base_name委譲で解消済み）。

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
## 実装記録（$切替の再試行と収束実測・2026-08-08）

ドメイン橋が整った状態で$切替（移行計画③）を再試行し、収束状況を実測した。

- **追補した橋（フラット規約下では不活性・本コミットで保持）**: 演算子呼名のfn接頭辞統一の残りサイト（auto_impl_compareのネストフィールドeq呼名2箇所・比較ラダー2箇所、impl_infoへの演算子登録2箇所）と、operand.cppの$エンコード名からの第1型引数抽出分岐（従来はBox__int→intの旧文字列抽出のみで$名が素通りしていた）。
- **収束実測**: 前回試行の約20件に対し、追補済みブリッジの下では8件→（演算子呼名統一で）4件→（codegenのtypes.cpp正準キー参照+enumガードで）3件→（operand561の手組みフラット構築の正準キー化で）1件まで収束した。codegen 2箇所のパッチ内容（types.cppの型引数付き構造体の正準キー参照・operand.cppの受け手構造体名正準構築）は本記録に固定した。
- **残る1件**: ネストVector（Vector<Vector<int>>）のget()経由参照で内側Vectorの読み出し位置がずれる（row1.len()が10/11等）。要素ポインタのGEPストライドに関わる未特定の第3の解決経路が残っており、次の検証枠でこの1件の特定から再開する。
- キー産生は従来規約（フラット既定+曖昧時$退避）へ復元し、全13スイートで挙動同一を確認した。

移行計画は「②codegen型参照の正準キー化（パッチ記録済み）→③キー産生$切替→残1件の特定修正→④逆算器削除」へ更新。

## 実装記録（②③の再試行と呼び出し側結合の実測確定・2026-08-11）

移行計画②（codegen型参照の正準キー化）＋③（キー産生の$切替）を同時適用して再試行し、残る結合の正体を関数名ドメインの**呼び出し側**として実測で確定した。

- **試行内容**: `struct_symbol_key` を `typekey::struct_key_from_tree` への委譲（常時$エンコード）へ切替え、codegen 2箇所（LLVM types.cppの型引数付き構造体の手組みフラット連結・operand.cppのフィールド投影の受け手構造体名）を同関数の正準キー参照へ置換した。
- **露出した失敗の正体**: ネストVectorのテストがLLVM検証失敗（`load ptr, i32 0`）で停止し、IR検分で特殊化ctorが旧フラット名 `Vector__Vector__int__ctor` のまま生成されていることを確認した。真因は**HIR/checker期に構築される呼び出し名**（ctor・メソッド）が `type_to_string` 由来のフラット文字列で組まれており、$キーで登録された構造体定義と乖離して特殊化関数の自己型解決が外れること。前回記録の「未特定の第3の解決経路」は、この呼び出し側の関数名ドメイン（移行計画①の未了分）だった。
- **実測した呼び出し側サイト（①の対象一覧）**: `hir/lowering/stmt.cpp` のlet ctor呼名・`hir/lowering/decl.cpp:299/325/359` のctor/dtor生成名・`hir/lowering/expr_member.cpp:1168` のメソッド呼名・`hir/lowering/expr.cpp:1049` の `Type<...>::method` 静的呼名・`types/checking/stmt.cpp` のchecker側ctor名（C16衝突表の登録側と対）。いずれも `mangle::` ヘルパへ文字列を渡す形のため、①は「型文字列でなく型ツリーから `struct_symbol_key`→`spec_fn_prefix` で組む」への置換となる（checker側の登録名・C16衝突表との整合が必要）。
- **今回の恒久分**: ctor呼名の手組み複製2箇所（HIR let経路・checker let経路）を `mangle::ctor_name` へ一本化した（挙動同一。①実施時の置換点を2→1へ集約）。②③はフラット既定へ復元し、全ネストジェネリック回帰の同値を確認した。

移行計画は「①呼び出し側の関数名ドメイン正準化（上記サイト一覧）→②codegen型参照の正準キー化→③キー産生$切替→④逆算器削除」で確定（②③のパッチ内容は本記録と前回記録のとおり）。

## 実装記録（②③の完遂＝キー産生の$全面化とcodegen正準キー化・2026-08-11）

移行計画②③を完遂し、特殊化の同定からフラット名を全廃した。前回記録の「残る1件」（ネストVectorのget参照ストライド）も本記録の正規化で解消した。

- **③キー産生の$切替**: `struct_symbol_key` を常時$エンコードへ切替えた（フラット既定+曖昧時退避を廃止。$は識別子に使えない文字のためC8のユーザー名衝突検査も不要になり削除）。`arg_symbol_key` はdecode対応へ拡張し、先行特殊化で具体名化されたフラット/エンコード名のリーフ（name="Vector__int"・args空）を可逆復号してから正準キーへ再エンコードする（ユーザー定義の__入り同名structはC8ガードで復号しない）。
- **特殊化引数ツリーの正準化（残1件の真因）**: 前回「未特定の第3の解決経路」とされていたのは、**キーでなく置換に使う型ツリー自体のstale名**だった。チェッカ/HIR由来のツリーに「name=旧フラット名のままtype_argsは構造化済み」（name="Vector__int"・args=[int]）というstale形が混入し、置換後のパラメータ/ローカル型としてcodegenへ漏れ、lookup欠落でtypedef-unionの8バイトフォールバック形状に落ちて値渡し構造体が切り詰められていた（外側pushのmemcpyが12バイト＝row1.len()誤値の直接原因）。`normalize_spec_arg_tree` を新設し、特殊化要求の記録チョークポイント3箇所（scanのrecord・mono_structsの2箇所）で「フラットリーフの復号」と「stale名のtype_args併存ツリーの基底名再建」を再帰適用するようにした。
- **②codegen型参照の正準キー化**: LLVM types.cppの型引数付き構造体の手組みフラット連結（約180行の型名switch群）とoperand.cppのフィールド投影の受け手構造体名構築を、`typekey::struct_key_from_tree` の正準キー参照へ置換した（enum特殊化はTagged Union経路のため除外ガード・$含み名はスキップ）。非ジェネリック関数の具体型引数（`Iterator<int>*` 等）が手組み連結でlookupを外しフォールバック形状に落ちていた欠陥もこれで解消した。
- **②JSエミッタの構造体定義解決の正準化**: JS backendは構造体フィールド名の解決に `struct_map_` の名前直引き＋`type_to_mangled_name`（旧フラット名）フォールバックを使っており、$キー化でlookupが外れると位置名 `fieldN` へ退避してフィールドの読み書きが分裂する実害があった（`(*p).val = 100` の書きだけ `field0` へ落ち、読み `.val` と乖離）。`findStructDef`（名前直引き→$正準キー再検索）をJSCodeGenの単一ヘルパとして新設し、フィールド投影・place型追跡・格納先型解決・デフォルト値生成・CSS最適化の全lookupサイトを同ヘルパへ一本化した（旧フラット名フォールバックは削除）。
- **検証**: interpreterスイート全数（705件中700 PASS・0 FAIL）・unit・regression・ネスト系の重点回帰（nested_collection_generics・nested_vector_lifecycle_test・nested_generic_destructor・iter_generic・method_interp・flat_name_collision）が$レジーム下で全PASS。テスト計画のネスト特殊化マトリクス（`Pair<Box<int>, Box<string>>`・順序違い・逆ネスト・`Tri`3引数混載）を `tests/common/generics/nested/spec_matrix.cm` として追加し、interpreter/llvm/jsの3バックエンド一致を確認した。生成シンボルの$はLLVM・JS（識別子に$可）・SV（IEEE 1800の単純識別子は非先頭$可）でいずれも合法。
- **残（①④）**: ①HIR/checker期の呼び出し名・型名のフラット産生の置換（現状はmono境界の正規化で吸収しており、入力デコーダとしての `parse_flat_type_args` はこのため存置）と、①完了後の④逆算器削除。同定（identity）は$のみとなったため、フラット名は「HIR由来入力の互換デコード」に役割が縮小した。

## 実装記録（①④の完遂＝呼び出し名の正準化と逆算器の全削除・2026-08-11）

移行計画①④を完遂し、フラット名の産生と逆算を全廃した。本設計文書の全段階（①②③④）が完了となる。

- **typekeyの層移動**: `typekey.{hpp,cpp}` を `mir/lowering/mono/` から `syntax/ast/` へ移動し、名前空間を `cm::mir::typekey` から `cm::ast::typekey` へ改めた。typekeyの実依存は `ast::Type`（hir::Typeはastの別名）のみで、hir/types層の産生サイトから使うには12層規律（hir→{base,syntax}・types→{base,syntax}）上syntax層への配置が必要だった。層依存チェックは12層・依存辺24本のまま通過。
- **①呼び出し名の正準化**: 型ツリーから関数名ドメインの正準接頭辞を組む `typekey::fn_prefix_from_tree` を新設し、産生サイト4箇所を置換した——(1) HIRメソッド呼名（expr_member.cppの型名文字列の角括弧再パース＝表示形argの半マングル `Vector__Vector<int>__push` 混入源。C8はstruct_defs_の完全名ガードで維持）、(2) HIR let ctor呼名（stmt.cppの `type_to_string` 由来の表示形 `Vector<Vector<int>>__ctor`）、(3) checker側ctor名（types/checking/stmt.cpp。HIR側と同一規則へ）、(4) MIRのdtor登録名（stmt/let.cppの手組み再帰マングル＝曖昧フラット `Vector__Vector__int` の産生源）。静的ジェネリック呼び出し（`Box<int>::new`）は文字列のみで型ツリーが無いため、引数文字列分割の半マングルを廃して表示形 `Box<int>__new` をそのまま産生し、monoスキャンの表示形照合へ復号を一元化した。定義側の総称パターン名（`Vector<T>__ctor` 等・decl.cpp）はmono照合の正準形のため存置。
- **④逆算器の全削除**: 産生正準化後、`parse_flat_type_args` の全呼び出しサイト（decode_type_name・mono_structsの3箇所・specialize）へ環境変数ゲートのプローブを仕込み、tests/common+js+svの全.cm（749件）のコンパイルで発火ゼロ（`__TaggedUnion_*` 内部名の空基底分割→即棄却のみ）を実測してから、フラット分岐5箇所と `parse_flat_type_args` 本体・宣言を削除した。あわせて呼び出し0件になっていたフラット名産生の始祖 `ast::type_to_mangled_name` も削除した。教訓: 大規模削除は「静的に到達不能と推論する」のではなく、チョークポイントへのプローブ+全数コンパイルで発火ゼロを実測してから行う。
- **検証**: interpreterスイート全数（706件中701 PASS・0 FAIL）・unit・regression・ネスト系重点回帰（nested_collection_generics・spec_matrix・flat_name_collision・nested_vector_lifecycle_test・static_generic_call・iter_generic・method_interp）が全PASS。呼び出し名はデバッグログ実測で ctor/dtor/メソッド全て `Vector__Vector$1$3$int__*` の正準形へ収束（表示形は静的呼び出しの `Base<args>__method` のみ＝スキャンで復号）。
- **到達状態**: 特殊化の同定は$エンコードキーのみ・関数名ドメインは正準argkeyの `base__argkey__method`（argkeyは`__`を含まないため一意分割可能）・曖昧フラット名の産生器と逆算器はゼロ。残る `__` はドメイン規約の区切り（関数名・モジュール修飾 `A__b`・内部名 `__TaggedUnion_*` 等）のみで、いずれも型引数の埋め込みではない。
