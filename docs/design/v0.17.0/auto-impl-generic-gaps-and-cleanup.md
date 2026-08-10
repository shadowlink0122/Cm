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

## 実装記録（第3段・前半＝ジェネリック演算子implのモノモーフ化登録）

第3段の第一構成要素「ジェネリック演算子implのモノモーフ化登録」を実装した。ユーザー定義の総称演算子impl（`impl S<T> for Eq { operator bool ==(...) }`・Ord同様）が特殊化されずPass 6の比較書き換えが生の構造体比較へフォールバックしていた欠陥（derive-as-source-expansionの「判明した制約」節に記録）を解消した。

- 演算子（==/<等）の呼び出しは生のBinaryOpのままPass 6まで残り、呼び出しサイト走査（scan_generic_calls）に現れない。そのため呼び出しサイト駆動では特殊化できない。生成済み構造体特殊化集合（`generated_struct_specializations`）を起点に総称演算子impl（名前に`<`と`>__op_`を含む関数）の特殊化要求を種蒔きする`seed_operator_specializations`をモノモーフ化の固定点ループへ追加した。特殊化した演算子関数はMirLoweringが`impl_info[特殊化構造体名][Eq/Ord]`へ登録し、Pass 6が特殊化演算子関数の呼び出しへ書き換える。
- 総称演算子implはHIR関数（HirFunction）を持たずgeneric_funcsに含まれないため、演算子implと総称構造体だけを持ち総称関数を一切持たないプログラムでは、モノモーフ化ドライバが「総称関数ゼロ」で早期リターンし種蒔きが走らなかった（==は既定の構造体比較がフィールド単位値比較で正しく、Ordは既定が生ポインタ比較のため`a < b`/`b < a`が入れ替わる無言の誤値になっていた）。ドライバに`has_generic_operator_impl`判定を追加し、演算子implが存在すれば総称関数ゼロでも固定点ループを回すようにした。
- 特殊化関数生成（generate_generic_specializations）を総称演算子impl（HirFunctionを持たない）に対応させた: 型パラメータ名を総称シンボル名の`<...>`部から復元し、self/other等の値パラメータ（型引数なしの基底構造体名で型付け）を特殊化構造体キーへ再型付けする。原本の総称演算子関数はcleanup_generic_functionsで除去する。
- 検証: jit/llvm/native/js/ts（直接実行）で総称Eq/Ordが全一致（`tests/common/interface/operator/generic.cm`）。総称構造体のOrd（非対称）でself/otherの取り違えが無いこと、Eq/Ordを同一構造体に併有しても混線しないこと、ネスト構造体・string型引数のEqが委譲比較で正しいことを固定した。
- 範囲外（前半では未対応）: 総称算術演算子impl（`impl S<T> for Add { operator S<T> +(...) }`）は`self.x + other.x`（x:T）が型検査で「Add operator requires numeric operands」に落ちる型検査層の制約が先にあり、MIRへ到達しないため種蒔きの算術分岐は現状不活性（型パラメータへの数値境界が入るまで保留）。

## 残課題（第3段・後半＝*_for_monomorphized全廃）と判明した前提

ジェネリック構造体のderiveを単一総称implソース合成へ載せ替え`generate_*_for_monomorphized`系を全廃する後半は、以下の言語機能が前提となることが実測で判明したため、それらの整備後に実施する。

- **プリミティブへの一様メソッド付与**: Hash/Debug/Display/Cssの合成本体はフィールド型に応じ`self.v.hash()`/`self.v.debug()`（構造体）か`(self.v as int)`/補間（スカラ）へ分岐する。総称構造体のフィールド`T v`は単一の合成本体を持ち特殊化で確定するため、`self.v.hash()`形に固定すると型引数が`int`等のプリミティブのとき`int.hash()`が存在せず成立しない（実測: `Unknown method 'hash' for type 'int'`）。プリミティブに`hash()`/`debug()`/`toString()`を一様に付与すれば単一総称implで賄える。Eq/Ord/Cloneは`self.v == other.v`・`self.v < other.v`・`return self;`で全型引数に成立するが、Hash/Debug/Display/Cssと生成経路を分けると二重管理になるため、全トレイト一括の載せ替えを一様メソッド整備後に行う。
- **ユニオンの等価演算子の正当化（是正済み）**: 型引数がユニオン（`Box<int | string>`）のEqは、合成本体`self.v == other.v`がユニオン`==`へ落ちる。ユニオン`==`は生表現比較の誤値（実測: `IU a = 1; IU c = 2; a == c` が`true`）だったが、MIR loweringの「タグ一致＋アクティブ変種のペイロード比較」脱糖で是正済み（tests/common/types/union/equality.cm）。これによりユニオン型引数のEq載せ替えの前提は満たされた（載せ替え自体はプリミティブ一様メソッドと同時の第3段後半で行う。なおスライス変種を含むユニオンは、抽出`u as int[]`のnative既知課題が別途残る）。
- スライス型引数（`Box<int[]>`）のEqはスライス`==`が内容比較で正しく動作するため、上記2点の整備と同時にR21診断から正常動作へ移行できる（`tests/common/errors/derive/generic_slice_arg.cm`は載せ替え時に正常系へ移す）。

現時点で`generate_*_for_monomorphized`系と特例群（auto_impl_info_等）は上記前提が未整備のため現役維持する。
