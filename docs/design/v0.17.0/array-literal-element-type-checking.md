# 配列リテラル要素の型検査

## 背景

配列リテラルによる変数初期化（`int[3] a = [...]`）では、各要素の型が宣言した要素型と一致しているかを検査していなかった。このため `int[3] a = [1, "hello", 3]` や、string変数をint配列へ入れる `int[2] a = [n, s]` がいずれも無診断で素通りし、後段でゴミ値・LLVM検証エラー・バックエンド間の挙動差に分裂していた。scalar変数宣言（`int x = "hello"`）は正しく型不一致エラーを出しており、配列リテラルだけが検査から漏れていた。

## 方針

配列リテラル要素の型検査を、scalar変数宣言と同じ規則（`types_compatible`）で行う。要素ごとに期待型（宣言配列の要素型）を伝播しつつ、推論後の要素型が要素型と非互換ならエラーにする。これにより挙動がscalar代入と一致する。

- 拡大変換（`tiny`/`short` → `int` 等）は許可する（`types_compatible` が数値間互換を許容するため、追加実装は不要）。
- 型名を明示しない構造体リテラル（`{x:1, y:2}`）は、期待型伝播でその要素型にコアースされるため、要素型に一致すれば許可される。逆に非互換な要素（`int` を構造体配列に入れる等）はエラーになる。
- union typedef（`typedef Val = int | string`）の要素型は、メンバ互換判定のため検査前に `resolve_typedef` で解決する（scalarと同じ経路。未解決のままだと `types_compatible` のunion分岐が typedef 解決前に評価されメンバ互換を取りこぼす）。
- 数値どうしの縮小変換（`int[] = [3.14]`）は、scalar同様に許可し警告で扱う（型不一致エラーにはしない）。

## void* によるエスケープハッチ

汎用ポインタ型 `void*` の配列は要素型検査を免除する。`void*` は任意のポインタと互換であり、加えて非ポインタ値の格納も許容する「何でも入る」格納庫として使う。取り出しは `auto`、格納した実体の型判定は `typeof` で行い、利用側が適切な型へキャストして扱うことを想定する。

## 実装

`src/internal/types/checking/stmt.cpp` の変数宣言処理で、初期化子が配列リテラルかつ宣言型が配列型の分岐に要素検査を追加する。要素型が `void*` の場合は検査をスキップする。エラーメッセージは変数宣言の型不一致（`TcTypeMismatchVariableDeclarationExpected`）を要素位置で流用する。

## テスト

- 正常系（`tests/common/arrays/element-types/`）: 拡大変換（widening）・無名構造体リテラル（struct_literal）・void\*配列とauto/typeof（void_ptr、`//! platform: !js|sv`）。
- エラー系（`tests/common/errors/arrays-slices/element-type/`）: リテラル混在（literal）・変数混在（variable）・非構造体の混在（struct_mismatch）。

## 対象外

- 関数ポインタ配列（`int*(int,int)[N]`）はコード生成が未対応（JITでシンボル未解決）のため本変更の対象外とする。
- 型名を明示した構造体リテラルを異なる構造体型へ代入する場合（`Point p = Rect{...}`）の取りこぼしは、期待型伝播が明示型名を上書きする既存の挙動でありscalarと共通。本変更のスコープ外。
