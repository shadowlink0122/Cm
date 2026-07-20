---
title: チェーンレシーバ解決の任意式対応
parent: v0.17.0 Design
---

# チェーンレシーバ解決の任意式対応

## 対象所見

| # | 領域 | 所見 | 状態 |
|---|------|------|------|
| H10 | スライス | チェーンレシーバ（`m[0].push(x)`・`make().len()`）が黙って欠落またはリンクエラー（レシーバ解決がVarRef/Memberのみ） | 一部実装（第1-2段+第4段: レシーバ解決を共通ヘルパ`resolve_receiver_place`へ統合しpush/len/capの黙殺を診断化、`make().len()`等の呼び出し戻り値レシーバは一時ローカルへ実体化して解決。第3・5段=スライス要素の添字レシーバはMIRで`int[][3]`の固定長情報が失われる問題とランタイム経由のアドレス取得が必要なため未対応） |

## 背景と根本原因

メソッド呼び出しのレシーバ（`obj`.method()の`obj`部分）を「場所（lvalue / MirPlace）」として解決する経路が、ベースオブジェクトを`HirVarRef`（変数参照）に限定しており、途中の連結は`HirMember`（フィールドアクセス）しか辿らない。添字式`HirIndex`（`m[0]`）や関数呼び出し`HirCall`（`make()`）をレシーバ基点・中間に含むと解決に失敗する。

### 場所解決の中核 get_member_place

src/internal/mir/lowering/expr/access.cpp:259-338 の`get_member_place`は、以下の2種類のノードしか扱わない。

- 中間の連結は`HirMember`のみを辿る（access.cpp:270、`while`で`std::get_if<HirMember>`）。
- ベースオブジェクトは`HirVarRef`のみサポート（access.cpp:277、コメント「変数参照のみサポート」）。
- どちらにも該当しなければ末尾で`return false`（access.cpp:337、診断なし）。

self（`*Struct`ポインタ）のDeref挿入（access.cpp:294-298）や、フィールドチェーンの逆順プロジェクション構築（access.cpp:306-331）は実装されているが、いずれも起点が`HirVarRef`である前提に立つ。`HirIndex`・`HirCall`に対する分岐は存在しない。

### スライスbuiltinのレシーバ解決

スライスメソッド（push/pop/remove/clear/len/cap）はHIR段でbuiltin呼び出しへ脱糖され（src/internal/hir/lowering/expr_member.cpp:597-639、レシーバ式を第0引数`args[0]`へ積む）、MIR段の`try_lower_slice_builtin`（src/internal/mir/lowering/expr_slice.cpp:19）で処理される。ここでも各builtinがレシーバ`call.args[0]`を`HirVarRef`分岐か`HirMember`分岐のみで解決する。

| builtin | VarRef分岐 | Member分岐 | 解決失敗時の挙動 |
|---|---|---|---|
| slice_len/cap | expr_slice.cpp:33 | :39 | :71 診断なしで空tempを返し黙殺（`make().len()`がここ） |
| slice_push | :84 | :93 | :160 診断なしで空void tempを返し黙殺（`m[0].push(x)`がここ） |
| slice_pop | :171 | :180 | :225-227 Error診断あり（C11で対策済み） |
| slice_delete | :240 | :246 | :277-279 Error診断あり |
| slice_clear | :292 | :298 | :326-328 Error診断あり |

pop/delete/clearは監査所見C11の「黙殺禁止」対応で診断を出すようになっているが、**len/cap（expr_slice.cpp:71）とpush（:160）は依然として診断なしで空tempを返し黙って欠落する**。加えてMember分岐は`get_member_place`（VarRef基点限定）に依存するため、`obj.field[i].push()`のような添字を含む混合チェーンも解決できない。

### 関連ノード型

- AST: `CallExpr`（src/internal/syntax/ast/expr.hpp:228）, `IndexExpr`（:244）, `MemberExpr`（:269、`is_method_call`・`args`を持つ）。
- HIR: `HirVarRef`（src/internal/hir/nodes.hpp:38）, `HirCall`（:100）, `HirIndex`（:114、`object`・`index`・`indices`）, `HirMember`（:122、`object`・`member`）。

HIR段（expr_member.cpp:51）ではレシーバは`lower_expr(*mem.object)`で任意式としてlowerされ、`m[0]`は`HirIndex`、`make()`は`HirCall`として正しく保持される。壊れるのはMIR段の場所解決だけである。

## 設計方針

レシーバ場所解決を「変数起点のフィールドチェーン」から「任意式が生成した値の場所」へ一般化する。核心は、基点が変数でない場合でも一時ローカルへ実体化（materialize）し、そのローカルを場所として扱うことである。

### 1. get_member_placeのベースオブジェクトを拡張

access.cpp:277の`HirVarRef`分岐に加え、以下を扱う。

- `HirIndex`（`m[0]`）: 添字式をlowerして要素の場所（`MirPlace` + index projection）を構築する。既存の`lower_index`（access.cpp:341）が要素の値を取り出す経路を持つため、その「場所版」（要素アドレスのplace化、`PlaceProjection::index`相当）を用意して連結する。
- `HirCall`（`make()`）: 呼び出しをlowerして戻り値を一時ローカルへ格納し、そのローカルを基点placeにする。戻り値がスライス/構造体のヘッダ値であれば、以降のフィールド/メソッドは一時ローカル上で解決する。

`while`の中間辿り（access.cpp:270）も`HirMember`だけでなく`HirIndex`を許容し、ネストした`a.b[i].c`形を解決できるようにする。

### 2. try_lower_slice_builtinのレシーバ解決を共通ヘルパ化

expr_slice.cpp内の各builtinが独立に持つ「VarRef分岐 / Member分岐 / 失敗時処理」を、レシーバ式`args[0]`から`(MirPlace, TypePtr)`を得る単一ヘルパ`resolve_receiver_place`へ統合する。このヘルパが`HirVarRef`・`HirMember`・`HirIndex`・`HirCall`を網羅的に扱い、解決不能な式に対しては必ず診断付きハードエラー（黙殺禁止インバリアント、監査ロードマップ第2段）を返す。

- これによりlen/cap（:71）とpush（:160）の黙殺を解消し、pop/delete/clearと同じError診断へ揃える。
- ケース漏れをコンパイル時に検出できるよう、式種別のswitchを網羅的（default到達でハードエラー）に書く。

### 3. 一時オブジェクトの寿命

`make().len()`のように戻り値が一時スライス/構造体の場合、その一時オブジェクトはメソッド呼び出し後に解放されるべきである。dropパス（監査C12/C13、別途H14系文書の範囲外）が未整備な現状では、少なくとも一時ローカルへ実体化してメソッドが正しく動くことを優先し、解放は既存の一時オブジェクト方針に従う（本文書では新規リークを増やさないことを目標とし、解放自体は別所見の範囲とする）。

## 構文例・出力例

対応後に動作すべきコード（設計目標。現時点では黙って欠落またはリンクエラー）。

```cm
import std::collections::vector::*;

// 添字レシーバ: スライスの要素に対するメソッド呼び出し
Vector<int>[] rows = ...;
rows[0].push(42);          // 現状: 黙って欠落 → 対応後: 要素の場所へpush

// 呼び出し戻り値レシーバ
int n = make_slice().len();  // 現状: 診断なし空temp → 対応後: 正しい長さ

// 混合チェーン
grid.cells[i].push(v);     // 現状: get_member_placeがfalse → 対応後: 解決
```

解決不能な式（設計上場所を持てないもの）に対しては、黙って空値を返さず診断を出す。

```
error: メソッドレシーバの場所を解決できません: この式は左辺値になりません
```

## 実装の段階分割

1. 第1段: expr_slice.cppのlen/cap（:71）とpush（:160）の黙殺を、pop/delete/clearと同じError診断へ揃える（被害の即時停止、局所修正）。
2. 第2段: `resolve_receiver_place`共通ヘルパを新設し、5つのbuiltin全てをそれへ寄せる（VarRef/Member網羅、診断一元化）。
3. 第3段: `get_member_place`のベースオブジェクトに`HirIndex`を追加（`m[0].field`・`m[0].push()`）。要素アドレスのplace化を実装。
4. 第4段: `HirCall`戻り値の一時実体化を追加（`make().len()`・`make().field`）。
5. 第5段: 中間連結（access.cpp:270のwhile）へ`HirIndex`を許容し、`a.b[i].c`混合チェーンを解決。

## テスト計画（tests/common/配下）

既存の`tests/common/chaining/`（`method_chain.cm`+`.expect`, `composite_chain.cm`+`.expect`等の形式）に追加する。

- `tests/common/chaining/index_receiver_method_test.cm` + `.expect`: `rows[0].push(x)`後に`rows[0].len()`・要素値を検証。jit/native/wasm/js/tsの全バックエンドで一致確認（監査分裂を潰す）。
- `tests/common/chaining/call_return_receiver_test.cm` + `.expect`: `make_slice().len()`・`make_struct().field`が正しい値を返すことを確認。
- `tests/common/chaining/mixed_chain_receiver_test.cm` + `.expect`: `grid.cells[i].push(v)`の混合チェーン。
- `tests/common/chaining/unresolvable_receiver_test.cm` + `.expect`（またはerrorsスイート）: 場所を持てない式へのメソッド呼び出しが黙って通らず診断で停止することを確認（negative check）。

## リスクと非互換性

- 従来「黙って欠落」していたコードが、対応後に正しく動くか、または診断でエラーになる。これまでビルドが通っていた（が結果が誤っていた）プログラムがエラーになる可能性があり、実質的なバグ顕在化である。リリースノートで挙動変更を明記する。
- `HirCall`戻り値の一時実体化は、dropパス未整備下では一時オブジェクトのリークを増やしうる。新規リークを最小化し、必要なら実体化対象をスライス/構造体ヘッダに限定する。
- 添字の場所化（`PlaceProjection::index`）はバックエンドのplace lowering全経路に影響する。既存の`lower_index`（値取得）と整合を取り、stride計算（監査C4のelem_size一元化と同じテーブル）を再利用する。

## 関連

- 監査レポート: docs/design/v0.17.0/large-scale-bottleneck-audit.md（H10、および黙殺パターンのC11・ロードマップ第2段「黙殺禁止インバリアント」）
- 場所解決: src/internal/mir/lowering/expr/access.cpp:259-338（get_member_place）, :341（lower_index）
- スライスbuiltin: src/internal/mir/lowering/expr_slice.cpp:19（try_lower_slice_builtin）
- HIR脱糖: src/internal/hir/lowering/expr_member.cpp:43-51, :597-639

## 追加対応（H8実装時に発見・修正済み）

enum（Tagged Union）メソッドの呼び出し戻り値レシーバのチェーン（`map.get(k).is_none()`・`parse_int(s).unwrap_or(0)` 等）が誤った値を返していた。
組み込みResult/Optionメソッドの脱糖がレシーバをclone_hir_exprで複製するため、呼び出しレシーバではタグ比較とペイロード取得が別評価になっていた。
matchのscrutinee退避と同じASTプリパス（match_hoist.cpp）で、呼び出しを含むレシーバを一時変数へ退避して単一評価を保証するよう修正した。
