---
title: 補間内の多次元スライス添字の誤読
parent: v0.17.0 Design
---

# 補間内の多次元スライス添字の誤読

## 概要

文字列補間内で多次元スライスを二重添字で読む（`{rows[i][j]}`）と、外側添字が0以外の場合にnative/jitがガベージ値を、jsが別要素の値を出力する。
`println(rows[1][0])`のような直接式は全バックエンドで正しく、補間経路（`__println__`のMIR lowering）に限定して壊れている。
バックエンド横断の構文網羅検証（syntax-audit-bugfixes.md B1〜B9のnative/jit監査）ではカバーされていない未記載バグ。

## 再現コード

```cm
import std::io::println;

int main() {
    int[][] rows = [];
    int[] r0 = [];
    r0.push(1);
    rows.push(r0);
    rows[0].push(2);
    int[] r1 = [];
    r1.push(10);
    rows.push(r1);
    println(rows[1][0]);       // 直接式: 全バックエンド 10（正しい）
    println("{rows[0][1]}");   // jit: 2（正しい） js: 10（誤要素）
    println("{rows[1][0]}");   // jit/native: ガベージ（例: 1527840816） js: 10
    return 0;
}
```

## 現象（バックエンド別）

| バックエンド | `{rows[0][1]}`（期待2） | `{rows[1][0]}`（期待10） |
|---|---|---|
| jit O0/O3 | 2 | ガベージ（実行ごとに変動） |
| native O0 | 2 | ガベージ |
| js | 10（誤要素） | 10 |
| wasm | 0等の不定値 | 不定値 |

## 根因候補

補間のMIR lowering（`src/internal/mir/lowering/expr_println.cpp`）は`{式}`を独自のミニパイプラインで解析しており、`arr[i][j]`のネスト添字は「最初の添字はスライスならcm_slice_get_element_ptr、追加の添字はPlaceProjection::index」という合成で処理している。
多次元スライスの内側は外側dataバッファへCmSlice値がインライン格納されている（H10の設計）ため、2段目の添字はコピーではなく内側ヘッダ参照（cm_slice_get_subslice_ref相当）を経由して要素getへ還元する必要があるが、補間経路は生のindexプロジェクションを積むため、スライスヘッダをメモリブロックとして添字アクセスした不定位置を読む。
通常式の`rows[1][0]`はlower_indexがget_subslice連鎖へ還元済み（H10完結）で正しく、補間経路だけが未追従。
jsバックエンドはMIRのplace投影を独自に解釈するため壊れ方が異なり、誤った要素を返す。

## 修正方針

補間ミニパイプラインのネスト添字処理を、通常式と同じlower_index（get_subslice連鎖→要素get）への委譲に置き換える。
最小修正としては、`expr_println.cpp`の「追加のインデックス」経路で現在型がスライスの場合にPlaceProjection::indexを積まず、cm_slice_get_subslice_ref＋要素幅別cm_slice_get_*の呼び出し列を発行する。
中期的には補間の`{式}`解析を文字列ベースの独自解析からパーサ・型チェッカー経由の式loweringへ寄せ、この種の経路分裂を根絶する（既知の同型バグ: 変数添字の定数0フォールバックは5a725e0で修正済み）。

## テスト計画

- `tests/common/`へ多次元スライスの補間読み（外側添字0/非0、変数添字、`{rows[i].len()}`併用）の回帰テストを追加し、直接式との出力一致を全バックエンドで検証する
- 3次元（`int[][][]`）の補間読みも境界ケースとして追加する

## 解決記録（実装済み）

expr_println.cppの補間ミニパイプラインを修正した。
第1添字で要素型がスライスの場合はcm_slice_get_subslice_ref（内側ヘッダ参照）で降下し、追加添字で現在型がスライスの場合は生のindex投影を積まずcm_slice_get_<幅>系ビルトイン呼び出し（さらにネストする場合はsubslice_ref、集約要素はelement_ptr+deref）へ置き換えた。
MIRレベルの修正のため全バックエンド共通で解消し、回帰テスト tests/common/basic/interp_nested_slice_index.cm（外側添字0/非0・変数添字・len()併用）をjit/native/js/wasmで検証した。
