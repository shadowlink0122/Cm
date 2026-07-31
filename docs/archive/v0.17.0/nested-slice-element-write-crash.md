---
title: スライスofスライス要素への直接代入がSIGSEGV（W2）
parent: v0.17.0 Design
---

# スライスofスライス要素への直接代入がSIGSEGV（W2）

## 概要

多次元スライスの要素への直接代入 `rows[0][1] = v` が、JIT・native全最適化レベル（-O0〜-O3）でSIGSEGVする。
同じ場所の読み出し（`int x = rows[0][1];`）はN1修正（interp-nested-slice-index.md）以降正しく動作しており、書き込み経路だけが未修正で残っている。
内側スライスを一度変数へ受けてから書く回避（`int[] r = rows[0]; r[1] = v;`）と `rows[0].push(v)` は正常に動作する。

## 再現コード

```cm
import std::io::println;
int main() {
    int[][] rows = [];
    int[] r0 = [];
    r0.push(1);
    r0.push(2);
    rows.push(r0);
    println("before-write");
    rows[0][1] = 22;
    // ここでJIT・native -O0〜-O3すべてSIGSEGV（rc=139）
    println("after-write");
    return 0;
}
```

回避パターン（正常動作）:

```cm
int[] r = rows[0];
r[1] = 22;
// ヘッダ共有のためrows[0][1]にも反映され、読み出しは22になる
rows[0].push(9);
// pushによる変異も正常
```

## 原因

`src/internal/mir/lowering/stmt/assign.cpp` の `build_projections`（169行〜）が、多次元インデックスの左辺値を外側・内側とも生の `PlaceProjection::index` として積む。
スライスofスライスでは外側要素は「インライン格納された内側スライスヘッダ」であり、生index投影はヘッダ構造体を要素配列とみなしてアドレス計算するため、不正アドレスへの書き込みになる。
これはN1が読み経路（補間ミニパイプライン）で `cm_slice_get_subslice_ref` 呼び出しへ置換して修正したのと同じ誤りで、代入文の左辺値経路に同じ修正が入っていない。

## 修正方針

1. `build_projections` でインデックス投影を積む際に現在型を追跡し、スライス（`Array` かつ `array_size` なし）に対するindexは生投影ではなく `cm_slice_get_subslice_ref`（内側スライス降下）または `cm_slice_get_element_ptr`（要素書き込み）呼び出しへ脱糖する。
2. 最内レベルの書き込みは要素ポインタ取得後のderef storeとし、`cm_slice_set_*` 系ランタインと幅整合を取る（slice_dispatch.hppのslice_scalar_infoを使用）。
3. 複合代入（`rows[i][j] += v`）とインクリメントも同経路のため同時に検証する。

## テスト計画

- regression: `rows[i][j] = v` / `+=` / `++` の書き込み後読み出し一致（int・string・構造体要素）。3次元 `cube[i][j][k]` も追加する。
- integration（native/jit両スイート）: 再現コードの正常終了と回避パターンとの結果一致。

## 検出経緯

native/jit網羅検証第2ラウンドで検出。最小再現は `.tmp/nativejit-bughunt2/min/m_d07b.cm`、回避確認は同 `min/m_d07c.cm`。

## 解決記録（実装済み）

書き込み経路の実体はexpr/binary.cppの代入正規化ループで、中間スライス段をcm_slice_get_element_ptr＋derefコピー（ヘッダ先頭のdataポインタを値として読む誤り）で辿っていたのが真因だった。
中間段（要素がスライス）はcm_slice_get_subslice_ref（インライン格納の内側ヘッダ参照）で降下し、最終段のみ要素ポインタ経由のデリファレンス格納にするよう修正した（3次元は再走査開始位置の補正も実施）。
stmt/assign.cppのbuild_projectionsにも同型のsubslice降下を追加した。
回帰テスト tests/common/dynamic_array/nested_slice_write.cm（2D/3D・複合代入・変数添字・string要素）を7モードで検証した。
