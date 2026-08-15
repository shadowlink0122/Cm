# dtor持ち構造体の暗黙コピー診断（二重解放ハザードの言語側対策）

## 問題

Cmの構造体代入は浅いコピーで、デストラクタ持ち構造体を値としてコピーすると両方のインスタンスでdtorが走り二重解放になる。
この事故はHashSetコンストラクタで実際に発生し（[記録](../archive/v0.17.2/bugfix-hashset-double-free.html)）、非決定的SIGABRTとして顕在化した。
恒久対策として同記録に「dtor持ち構造体の暗黙コピーへの警告」と「move代入のサポート」が挙げられ、v0.17.2時点では設計課題に留まっていた。

## 現状調査

- `move` 式はパーサ上任意の単項式位置で受理され、let初期化（`T b = move a;`）だけでなく代入RHS（`self.f = move a;`）でも既にMIRまで機能する（HIRのVarRefに`is_moved_from`が立ち、MIRが移動元のdtor登録を解除する）。実験でdtorが1回だけ走ることを確認した。
- したがって「move代入のサポート」は実装済みであり、残る恒久対策は暗黙コピーの診断のみ。
- dtor登録はlet宣言のローカルのみ（`mir/lowering/stmt/let.cpp`）。関数パラメータはdtor登録されないため、値渡し引数は現時点で二重解放の直接原因にならない（診断対象から除外し、本書に記録する）。

## 方針

数値縮小変換（Z5）と同じ段階導入方式を採る: 通常は警告、`--strict`（check/lint）ではエラーへ昇格する。
既存構文は引き続き受理するため破壊的変更はない（[[cm-language-design-principles]]の破壊的変更回避に従う）。

### 診断条件

受理サイト（let初期化・代入・return・構造体リテラルのフィールド初期化）で、以下をすべて満たす場合に診断する。

- 値の式が場所式（変数参照・フィールド参照・要素参照）である（コンストラクタ呼び出しやメソッド戻り値などの一時値は所有が一意なので対象外）
- 値の式が `move` でラップされていない
- 値の型が明示的なデストラクタ（`~self()`）を持つ構造体である（typedefは解決し、ジェネリックはベース名で判定する）

### 対象外（本書で記録する制限）

- 関数引数の値渡し: パラメータはdtor登録されないため二重解放にならない（コピーがポインタを共有する点は残るが、診断は実害のある二重解放系に絞る）
- dtor持ち構造体をフィールドに含むだけの構造体: メンバdtorの自動合成が無いため外側の型のコピーは二重解放にならない（メンバdtor合成の導入時に再検討する）
- ジェネリック関数内の型パラメータ`T`のコピー: 型検査は単相化前のため判定できない

## 実装

- `TypeChecker` にdtor持ち型名の集合を追加し、impl登録時（`impl.destructor` 非null）にベース型名を記録する
- `check_dtor_copy_policy(source_type, value_expr, span)` を `utils/ownership.cpp` に新設し、let初期化・代入（`=`）・return・構造体リテラルフィールドの4サイトから呼ぶ
- メッセージは `MsgId::TcImplicitDtorCopy`（en/ja）で、`move` による所有権移動を提案する

## 標準ライブラリの追従

`HashSet.self()` の「代入後にポインタを手動無効化する」回避策を `self.map = move m;` へ置き換える（診断のセルフチェックを兼ねる）。

## テスト計画

- regression（`tests/regression/dtor_copy_diag_test.cpp` + `cases/dtor_copy_diag/`）: let/代入/return/フィールド初期化の各サイトで警告1件、move使用・一時値・dtor無し構造体で警告0件
- integration（`tests/common/basic/destructor/move_assign.cm`）: move代入・move初期化でdtorが移動先の1回だけ走ることの実行検証
- 既存スイートの警告ゼロ確認（libs・tests/commonに暗黙コピーが残っていないこと）
