---
title: 負数の基数書式指定子の型幅無視とバックエンド分裂
parent: v0.17.0 Design
---

# 負数の基数書式指定子の型幅無視とバックエンド分裂

## 概要

補間の基数書式指定子（`{x:x}`/`{x:X}`/`{x:b}`/`{x:o}`）へ負の整数を渡すと、native/jit/wasmは宣言型の幅を無視して64ビットへ符号拡張したビットパターンを出力し、jsは`-`符号付きの絶対値表記を出力する。
同一プログラムの出力がバックエンド間で完全に分裂しており、どちらも「宣言型の幅の2の補数表現」という一般的な期待（C/Rust相当）と一致しない。
正の値は全バックエンドで一致するため、負数のみの問題。

## 再現コード

```cm
import std::io::println;

int main() {
    int neg = -255;
    println("{neg:x}");   // native/jit: ffffffffffffff01   js: -ff   期待: ffffff01
    println("{neg:b}");   // native/jit: 64桁の1埋め        js: -11111111
    println("{neg:o}");   // native/jit: 1777777777777777777401   js: -377
    tiny t = -1;
    println("{t:x}");     // native/jit: ffffffffffffffff   js: -1   期待: ff
    long ln = -255;
    println("{ln:x}");    // native/jit: ffffffffffffff01（これはlong幅としては正しい）   js: -ff
    return 0;
}
```

## 現象

| 型と値 | native/jit/wasm | js | 期待（型幅の2の補数） |
|---|---|---|---|
| `int -255` の`:x` | `ffffffffffffff01` | `-ff` | `ffffff01` |
| `tiny -1` の`:x` | `ffffffffffffffff` | `-1` | `ff` |
| `long -255` の`:x` | `ffffffffffffff01` | `-ff` | `ffffffffffffff01` |

## 根因候補

nativeランタイムの基数変換（cm_format_hex等）が引数をi64で受けて符号拡張後のビットパターンをそのまま基数変換しており、呼び出し側も宣言型の幅情報を渡していない。
jsランタイムは`Number.prototype.toString(radix)`相当の実装で、負数は`-`＋絶対値表記になるJSの素の挙動が漏れている。
どちらも「書式化対象の宣言型幅でマスクしてから符号なしとして基数変換する」という正規化を持たない。

## 修正方針

基数書式（x/X/b/o）の意味論を「宣言型幅の2の補数ビットパターンを符号なしとして基数表記する」（C/Rustの`%x`/`{:x}`相当）に統一する。
MIR loweringで書式化対象の型幅（8/16/32/64）をランタイム関数へ伝え、ランタイム側で`値 & ((1 << 幅) - 1)`のマスク後に変換する（64ビットはマスク不要）。
jsバックエンドは同じ幅情報で`BigInt.asUintN(幅, 値)`へ正規化してからtoString(radix)する（H5のBigInt化とも整合する）。
10進の`{x}`（既定書式）は現状の符号付き表記のまま変更しない。

## テスト計画

- tiny/short/int/long × 正・負・最小値（INT_MIN等）× x/X/b/o の行列テストを追加し、全バックエンドで同一出力を検証する
- 既存のformattingカテゴリの期待値に負数ケースが無いことを確認し、あれば新意味論へ更新する（挙動変更のためリリースノートに互換性注記を書く）

## 解決記録（実装済み）

意味論を「int以下は昇格幅32bit・long系は64bitの2の補数を符号なし基数表記」（Cのprintf %xの既定昇格に相当）に統一した。
native/wasmのcm_format_replace_intの基数分岐を`(unsigned int)`マスク経由へ、jsの__cm_format基数分岐をbigint=asUintN(64)・number整数=>>>0の正規化ヘルパー経由へ変更した。
uintの基数書式がnativeで未対応（10進のまま）だった分裂と、wasmのlong 16進の0xプレフィックス付与の分裂も同時に修正した。
定数畳み込みでlong定数がNumber化して幅情報が消えるjs経路は、書式化関数（cm_format_string等）へのwide64静的型引数を__cm_bigで包んで保存する。
tinyの`{t:x}`は昇格幅の`ffffffff`となる（8bit幅マスクではない。文書当初の期待値からこの点を意味論決定として変更）。
回帰テスト tests/common/formatting/negative_radix.cm（int/tiny/long/uint × x/X/b/o）をjit/native/js/wasmで検証した。
