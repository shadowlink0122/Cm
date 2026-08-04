---
title: 固定長配列→スライス変換の手書き要素サイズ残存（Z2）
parent: v0.17.0 Design
---

# 固定長配列→スライス変換の手書き要素サイズ残存（Z2）

## 概要

固定長配列をレシーバとするスライスメソッド呼び出し（`arr.contains(x)`等）のHIR脱糖に、`cm_array_to_slice(ptr, len, elem_size)`へ渡す要素サイズの手書きswitchが残っている（`src/internal/hir/lowering/expr_member.cpp:1110-1119`）。
layout-query-unification（実装済み・archive）が「型→要素ストライドの手書きスイッチ10箇所を2意味論APIへ置換」した際の取り残しで、C4（Short/UShortの取りこぼしでヒープ破壊）と同型の欠落が再発している。

現状のswitchは char/bool=1・long/ulong/double=8・Pointer/String=8・その他=4 のみで、以下が誤る。

| 要素型 | 手書きswitchの値 | 正しい値 |
|---|---|---|
| `short`/`ushort` | 4（default） | 2 |
| `tiny`/`utiny` | 4（default） | 1 |
| `isize`/`usize` | 4（default） | 8（64bit） |
| 構造体・ユニオン | 4（default） | layout_size |
| 固定長配列要素 | 4（default） | N×要素サイズ |
| `string`/ポインタ（wasm32） | 8（固定） | 4（target_pointer_size） |

変換後のスライスヘッダのelem_sizeが誤るため、受け手側の全操作（ビルトイン走査・境界チェック・get系）が崩れる。Z1（検索ビルトインの_i32固定）と重なって顕在化するため単独症状の切り分けは難しいが、`short[4].map(...)`が`4 20 20`（期待`4 20 80`）を返す誤値にはこの経路も寄与している。

## 併記: 死コード union_slice_elem_size

`src/internal/mir/lowering/context.hpp:23-58` の `union_slice_elem_size` は呼び出し元ゼロの死コードである。
`layout_size`（Union分岐・構造体バリアント再帰対応）へ置換済みだが関数が残っており、バリアントサイズを手書きで再計算する古い実装（構造体バリアントは一律8と誤る）を誰かが再利用する危険がある。削除する。

## 修正方針

1. `expr_member.cpp`の手書きswitchを削除し、HIR層から参照できる共通の要素サイズAPI（MIRのelem_size_of/layout_sizeと同一の定義。layout-query-unificationのAPIをhir層へ公開するか、HIR脱糖では要素サイズを埋め込まずMIR側で解決する形へ変更）で置換する。
2. `String`/`Pointer`のサイズはターゲット依存（`cm::target_pointer_size()`）とし、wasm32/baremetal-armの4バイトを正しく反映する。
3. `union_slice_elem_size`を削除する。
4. 回帰: `short[4]`/`tiny[3]`/`string[3]`（wasm含む）/構造体配列のレシーバメソッド（map/forEach/contains）で値検証（Z1修正とセットで確認する）。

## 検出経緯

第4ラウンド追補（ユニオン・文字列要素の配列/スライス整合性調査）で検出。最小再現は `.tmp/bughunt4/z10_short_map.cm`。

## 実装記録（2026-08-05）

- 修正方針の選択肢のうち「HIR脱糖では要素サイズを埋め込まずMIR側で解決する」を採用した。HIRの手書きswitchはプレースホルダ0の発行に縮退させ、MIRのcm_array_to_slice引数lowering（expr_call.cpp）が第1引数のポインタ要素型から`layout::array_elem_stride`（typedef解決・集約レイアウト・ターゲット依存ポインタ幅）で第3引数を再計算する。
- これによりHIR層へのレイアウトAPI公開が不要になり、サイズ決定はMIRのlayout 1系統に閉じる。return・スライスリテラル・push（X3）の直接構築サイトは従来からlayout APIを使用しており、全サイトが同一APIに揃った。
- 死コード`union_slice_elem_size`（context.hpp、呼び出し元ゼロ・構造体バリアントを一律8と誤る旧実装）を削除した。
- 実害の確認: wasm32で`string[3]`の`impl string[]`メソッドが`aa;cc;;`（1つ飛び読み）になる再現を修正前に取得し、修正後にnative/jit/wasm一致（`aa;bb;cc;`）を確認した。short/tinyの合計は型ベース読みとの偶然一致で値は合うが、変換時の過剰memcpy（スタック外読み）も同時に解消している。
- 回帰テスト`tests/common/impl/fixed_array_elem_stride.cm`（short/tiny/string×impl T[]メソッド）を追加し、全スイート（unit/regression/interpreter/llvm/llvm-wasm/js/sv）通過を確認した。
