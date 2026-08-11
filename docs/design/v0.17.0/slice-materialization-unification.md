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
