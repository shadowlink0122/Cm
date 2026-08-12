# スライス実体化と総称フィールド型置換の一本化（リファクタリング提案）

## 概要

固定長配列リテラルを動的スライス（CmSlice）へ実体化するコード（`cm_slice_new`確保+要素pushループの生成）が、MIRローワの複数サイトに酷似した形で複製されている。
また「総称構造体のフィールド型は型パラメータのままなので、判定前に型引数で置換する」という前提処理も各サイトが個別に実装しており、置換を持たないサイトが残っているとスライスフィールド判定を素通りして固定長配列blobが生格納される。
この複製の直接帰結が総称derive特殊化のSIGSEGV（lower_struct_literalに置換がなく、`Box<int[]>`のリテラルが不正CmSlice\*を作った。2026-08-11修正済み）であり、同型のバグが残りのサイトで再発しうる。

## 実測（2026-08-11時点）

`cm_slice_new`を生成するMIRローワのサイト:

| ファイル | 箇所数 | 文脈 |
|---|---|---|
| mir/lowering/stmt/let.cpp | 6 | let初期化の配列→スライス（通常形+分割代入形で各3箇所） |
| mir/lowering/expr/construct.cpp | 2 | 構造体リテラルのスライスフィールド初期化 |

総称フィールド型の型パラメータ置換は、construct.cpp（今回追加）・モノモーフィゼーションのsubstitute_type_tree・checkerのvalidate_derive_instantiationがそれぞれ独自実装を持つ（substコールバックの重複）。

## リファクタリング方針

- スライス実体化を単一ヘルパ（例: `materialize_slice(LoweringContext&, LocalId src_array, const hir::TypePtr& elem_type)`）へ抽出し、let/構造体リテラル/その他の全サイトを同ヘルパ経由に統一する。
- 型パラメータ置換はモノモーフィゼーションのsubstitute_type_treeを共有APIへ昇格し（hir層のフリー関数化）、construct.cpp・checkerのローカル複製を削除する。
- 「フィールド型・要素型の判定は必ず置換後の型で行う」を規約化し、判定ヘルパの引数型で強制する（未置換のTypePtrを受けない）。

## 段階分割

1. スライス実体化ヘルパの抽出とlet.cpp/construct.cppの置換（挙動同一。生成MIRの形をregressionで固定してから実施する）。
2. substitute_type_treeの共有API化とローカル複製3箇所の削除（挙動同一）。
3. 置換忘れの再発防止: 総称構造体×スライス/ユニオン/ネスト構造体フィールドのリテラル・let・代入・引数渡しのマトリクスをintegrationへ追加する。

## リスク

- let.cppの6箇所は通常形と分割代入形で微妙に文脈が異なるため、ヘルパの引数設計を先に確定してから機械的に置換する（文脈差をヘルパ内の分岐にしない）。

## テスト計画

- regression: ヘルパ抽出前後で生成MIRが同一であること（既存のslice関連regressionを流用）。
- integration: 段階3のマトリクス（全バックエンド一致）。

## 検出経緯

総称derive特殊化のSIGSEGV修正（2026-08-11）の真因調査で、lower_struct_literalだけが型パラメータ置換を持たないこと、スライス実体化コードが8箇所に複製されていることを確認し起案した。

## 実装記録（2026-08-11）

着手時の精査で複製の実態は「cm_slice_new+要素pushループが3サイト（let空初期化・letリテラル・構造体リテラルフィールド）、cm_array_to_slice発行が6サイト（coerce_fixed_array_to_slice正準実装・letリテラル内側要素・let非リテラル・構造体リテラル内側要素・グローバル初期化子・return・push要素）」だった（起案時の8箇所はgrep行数による概算）。

- **cm_array_to_slice発行の一本化**: `LoweringContext::materialize_array_to_slice(src_place, src_array_type, slice_type, dest, elem_hint)`を実体化コアとして新設し、既存の`coerce_fixed_array_to_slice`（ゲート付き）はこれへの委譲にした。place基点（return値のderef先・グローバル格納先）と宛先指定（return_local・グローバル・let変数への直接格納）を引数化したため、全サイトが生成MIR同形のまま置換できた。空配列リテラルの要素型フォールバック（レシーバの内側スライス型からストライドを計算）はelem_hint引数として吸収した。
- **cm_slice_new+要素pushループの一本化**: `ExprLowering::materialize_slice_literal(elements, slice_type, ctx, dest)`（expr/materialize.cpp新設）へ抽出し、let空初期化（容量0）・letリテラル・構造体リテラルフィールドの3サイトを置換した。push関数選択はslice_elem_dispatchの正準表・要素の暗黙変換はcoerce_to_expected・内側固定長配列はmaterialize_array_to_sliceへ委譲で統一。
- **構造体リテラル経路の意味論がlet経路へ揃った（挙動修正を含む）**: 旧構造体リテラル経路はpush関数選択が手書きif連鎖（固定長配列要素も無条件push_slice）で、要素のcoerce_to_expected（ユニオン/インターフェイス要素の構築）とfloat要素へのdouble縮小も欠いていた。統一によりY6（固定長配列要素はblob格納）・W1・ユニオン要素構築がフィールド初期化でも有効になった。
- **型パラメータ置換の共有API化**: `ast::substitute_type_params(type, subst)`を新設し（ast/types.cpp。hir::Type=ast::Typeのため全層で共有可能）、モノモーフィゼーションの`substitute_type_tree`を委譲化・construct.cppのローカルラムダを削除した。checker側の`substitute_generic_type`は表示名照合を含む別意味論の既存正準API（6箇所超で共用中）であり複製ではないため対象外とした（起案時の「3重実装」はこの精査で2重+別正準と確定）。
- **検証**: unit・regression（陳腐化していたDeriveExpansionTest.GenericStructIsNotExpandedをジェネリックderiveソース合成一本化後の挙動＝総称impl合成ありへ更新）・interpreter/llvm/js/svスイート。
- **段階3（再発防止マトリクス）**: `tests/common/generics/field_materialize_matrix.cm`を追加した（総称構造体×スライス/ユニオン/ネスト構造体/多次元フィールド×リテラル・let・引数渡し・return経路。jit/native/js一致）。このマトリクスがjsバックエンドのユニオンtypedefタグ計算欠落（特殊化後正準化側の実装過程で顕在化）を実際に検出しており、検出網として機能した。
