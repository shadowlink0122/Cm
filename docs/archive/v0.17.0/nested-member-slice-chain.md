---
title: B4 ネストメンバスライスのチェーン変異でSIGSEGV
parent: v0.17.0 Design
---

# B4: ネストメンバスライスのチェーン変異でSIGSEGV

## 対象バグ

| # | 領域 | 概要 | 重大度 | 状態 |
|---|------|------|--------|------|
| B4 | スライス | ネストしたメンバスライスのチェーン変異がnativeでSIGSEGV | Critical | 修正済み |

## 再現コード

```cm
struct Bag { int[] items; }

int main() {
    Bag[] bags = [];
    bags.push(Bag { items: [] });
    bags[0].items.push(7);  // O0/O3ともnativeでSIGSEGV
    return 0;
}
```

## 現象

スライス格納構造体のスライス要素を経由したメンバスライスへのpushがクラッシュする。
構文→LLVM IR対訳リファレンスのstrings-slices検証で検出した（単段のメンバスライス操作・添字レシーバ操作は正常）。

## 根因（確定）

`bags[0].items.push(7)`のレシーバは「メンバ式のベースが添字式」という形だが、get_member_place（src/internal/mir/lowering/expr/access.cpp）がベースとして変数参照しかサポートしておらず、resolve_receiver_placeが解決失敗して非致死のErrorログのみでpush文ごと黙って欠落していた。
後続の読み出しが未変異の空ヘッダ経由で野良ポインタを読み、SIGSEGVまたはゴミ値になる。
さらに読み出し側にも隣接バグがあり、LLVMコード生成（operand.cpp）のIndexプロジェクションがスライス型フィールドに固定長配列用のフラットGEPを適用してCmSliceヘッダポインタのスロットを要素列として誤読していた。

## 実装内容

- get_member_placeにベースが添字式の場合を追加サポートし、添字対象を再帰的に場所化して、固定長配列要素はindexプロジェクション、スライス要素は`cm_slice_get_element_ptr`+Derefプロジェクションで場所化する（多次元の中間レベルは既存の`cm_slice_get_subslice_ref`連鎖）
- convertPlaceToAddress（src/internal/codegen/llvm/core/operand.cpp）のIndexプロジェクションで、現在型がスライスの場合はCmSliceヘッダ→dataポインタ経由で要素アドレスを計算する分岐を追加
- コピーされた一時への変異は発生せず、解決できない合成は従来どおり診断で停止する

## テスト

- tests/common/chaining/nested_member_slice_test.cm — bags[0].itemsへのpush/pop/delete/clear、要素間の独立性、補間経由の読み出し、固定長配列要素のメンバスライス
- 検証済み: 修正前の全レベルゴミ値/SIGSEGVが解消し、native O0/O2/O3・jit・wasm・jsすべてPASS、chaining/slice/array/structs等の既存回帰94件（llvm）・87件（jit）・34件（wasm）・16件（js）全PASS、2段ネスト（grid[0][1].items.push）も正常
- 補足: `Bag[][]`直接構文のパーサ未対応はB4と別件の既存制限
