---
title: スライスof固定長配列（int[2][]）の要素格納表現が未定義（Y6）
parent: v0.17.0 Design
---

# スライスof固定長配列（int[2][]）の要素格納表現が未定義（Y6）

## 概要

要素型が固定長配列のスライス（`int[2][] rows`）で、pushと読み出しが異なる格納表現を仮定しており、`rows.push([1, 2])`後の`rows[0][1]`等が全経路でゴミ値になる（実行ごと・最適化レベルごとに値が変動）。
checkerは型を無診断で受理する。`int[][]`（スライスofスライス）と`int[2][4]`（固定長多次元）はそれぞれ正常で、「スライスの要素が固定長配列」という組み合わせだけが壊れている。

## 再現コード

```cm
import std::io::println;
int main() {
    int[2][] rows = [];
    rows.push([1, 2]);
    rows.push([3, 4]);
    println("{rows.len()}");
    // 2（正しい）
    println("{rows[0][1]} {rows[1][0]}");
    // 期待2 3 → ゴミ値（例: -1563800512 9）
    return 0;
}
```

## 原因

要素格納クラスの表（`slice_elem_dispatch`）が要素kind＝Arrayを一律にInnerSlice（`cm_slice_push_slice`＝ヘッダ格納）へ分類する一方、場所化（`lower_place`）とcodegenのIndex投影は固定長配列要素（`array_size`あり）をインラインスライスとみなさず生オフセット計算で読む。
つまりpushは「スライスヘッダ」を格納し、読みは「固定長blob」を仮定するという表現の不一致が起きている（X3のリテラル→ヒープスライス実体化も、固定長要素の場合はかえって不一致を広げる）。
根本原因は、スライス要素としての固定長配列の格納表現（インラインblobか、ヘッダ経由か）が仕様として未定義のまま各所が別解釈をしていることにある。

## 修正方針

1. 仕様確定: スライス要素の固定長配列は「要素サイズ＝N×elem_sizeのインラインblob」として格納する（構造体blobと同じBlobクラス。ヘッダを介さないため`rows[i][j]`の読みが生オフセットで成立し、既存のcodegen Index投影と整合する）。
2. `slice_elem_dispatch`にFixedArrayの判別を追加し、`array_size`を持つArray要素はBlobクラス（アドレス渡しのblob push/pop/get）へ分類する。表の分類キーがTypeKindだけでは判別できないため、シグネチャを`slice_elem_dispatch(const hir::TypePtr&)`へ変更してkindと`array_size`の両方を見る。
3. `lower_place`・codegenのIndex投影・`elem_size_of`（固定長要素はN×要素サイズ）を同じ判別に揃え、pushリテラル（X3経路）は固定長要素の場合ヒープスライス化せずblobアドレス渡しにする。
4. 三者（push系・場所化・codegen）の表現一致を、本文書の再現とpop/delete/多重添字書き込みを含む回帰で固定する。

## テスト計画

- regression: `int[2][]`のpush/読み/書き/pop/len、`string[2][]`（要素内ポインタ）、`Point[2][]`（構造体内固定長）の値検証。
- integration（native/jit/wasm/js）: 再現コードの期待値実行。wasm（ポインタ幅4）はelem_size計算の差が出やすいため必須。

## 検出経緯

v0.17.0全修正後のレイヤー別レビュー（第4ラウンド）で検出。最小再現は `.tmp/bughunt4/a/a02_slice_of_fixed.cm`。
