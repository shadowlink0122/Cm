---
title: push(配列リテラル)が壊れた要素をpushする（X3）
parent: v0.17.0 Design
---

# push(配列リテラル)が壊れた要素をpushする（X3）

## 概要

多次元スライスへの配列リテラル直接push（`xs.push([1, 2])`・`xs.push([])`）は受理・実行されるが、pushされた要素のスライスヘッダが壊れており、後続アクセスで要素値ゼロ・len()ゴミ値・SIGSEGV・SIGABRT（不正ポインタのrealloc/free）を引き起こす。
`string[][]`への`rows.push(["a", "b"])`は最初の要素読みで即SIGSEGVする。
リテラルを一度変数に受けてからpushする形（`int[] row = [1, 2]; xs.push(row);`）は完全に正常で、リテラル直渡しだけが壊れる。
checkerは無診断で、O2ではlen()の観測値がO0と異なる（ゴミの読み方が変わる）ためバックエンド間差分も生じる。

## 再現コード

```cm
import std::io::println;
int main() {
    int[][] xs = [];
    xs.push([1, 2]);
    int v = xs[0][0];
    println("v={v}");
    // 期待1 → 0（要素データが失われている）
    println("{xs[0].len()}");
    // 期待2 → ゴミ値（例: 2131968096）
    return 0;
}
```

空リテラルpush後の内側pushはアボートする:

```cm
int[][] xs = [];
xs.push([]);
xs[0].push(7);
// SIGABRT（壊れたヘッダに対するrealloc）
```

```cm
string[][] rows = [];
rows.push(["a", "b"]);
string x = rows[0][1];
// SIGSEGV（全経路）
```

W系調査のd03（`d.bags[0].leaves.push(...)`後のゴミ値・クラッシュの一部）・c05/c06（メソッド戻りスライス経由の誤値）も、本バグでpushされた壊れ要素の二次被害と判明した（変数経由pushに置き換えるとチェーン読み書きはすべて正常）。

## 原因

`src/internal/mir/lowering/expr_slice.cpp` の `__builtin_slice_push` lowering（69行〜）は、push引数を期待型なしの `lower_expression(*call.args[1], ctx)` で下ろし、要素型が`Array`（スライス）の場合そのまま `cm_slice_push_slice` へ渡す。
配列リテラルは文脈の期待型が伝わらないと固定長配列blob（または未初期化一時）としてlowerされるため、CmSliceヘッダを期待する`cm_slice_push_slice`にヘッダでないメモリが渡り、壊れたヘッダがそのまま格納される。
変数経由では変数の宣言型（`int[]`）からスライスとしてlowerされるため正常になる。

## 修正方針

1. push引数のloweringにレシーバ要素型を期待型として伝播し、配列リテラルをスライス（ヘッダ付き）として構築してから`cm_slice_push_slice`へ渡す。
2. 空リテラル`[]`は要素型付きの空スライス生成（len=0/cap=0の正規ヘッダ）に脱糖する。
3. 期待型を決定できないリテラル引数は診断付きエラーにして黙殺を排除する。
4. `insert`等、値引数を取る他のスライスAPIにも同じ期待型伝播を適用する。

## テスト計画

- regression: `push([...])`（int/string/ネスト）・`push([])`後の内側push・len/要素読みの期待値検証、変数経由pushとの結果一致。
- integration（native/jit両スイート）: 再現3種の正常動作とO0/O2出力一致。

## 検出経緯

native/jit網羅検証（X系）で検出。最小再現は `.tmp/nativejit-bughunt3/min_push_arr.cm` / `min_push_empty.cm` / `push/c09_push_string_array_literal.cm`。

## 解決記録（実装済み）

方針どおり2層で修正した。
(1) 型検査のpush引数へレシーバ要素型を期待型として伝播（propagate_literal_expected_type）。
(2) MIR loweringの__builtin_slice_pushで、要素型がスライスかつ引数が固定長配列blobの場合にcm_array_to_sliceでヒープスライスへ実体化してからcm_slice_push_sliceへ渡す（空リテラルはlen=0の正規ヘッダ、要素サイズはスカラ/ポインタ/構造体/ネストスライスをディスパッチ）。
回帰テスト tests/common/dynamic_array/push_array_literal.cm（int/空/string・push後の内側push・変数経由との一致）を7モードで検証した。
