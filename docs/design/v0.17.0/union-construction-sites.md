---
title: ユニオン構築（タグ書き込み）の消費サイト欠落（Y1〜Y3）
parent: v0.17.0 Design
---

# ユニオン構築（タグ書き込み）の消費サイト欠落（Y1〜Y3）

## 概要

ユニオン型（`typedef Val = int | string` 等）の値構築は「変種値をタグ＋ペイロードへ包むCast」をloweringが挿入することで成立するが、この挿入が一部の値消費サイトにしか実装されておらず、欠落サイトではタグ未初期化のままペイロードだけが書かれる。
結果として `is` 判定が全変種でfalseになり、`as` ダウンキャストは実行時パニック（invalid union cast）になる。
checkerは全ケースを無診断で受理し、全バックエンド・全最適化レベルで同一に誤るため差分検証でも見えない。
W1/X3/X4（無名リテラルの期待型伝播欠落）と同じ「消費サイトごとの個別実装が揃っていない」構造であり、修正は期待型機構（infer_type_expecting／coerce系）への一元化で行うべきである。

## サイト別の現状

| 消費サイト | 例 | 現状 |
|---|---|---|
| let初期化 | `Val v = 7;` | 正常（タグ構築される） |
| ローカルへの再代入 | `v = "s";` | 正常（assign.cppのユニオン特別処理） |
| フィールドへの代入 | `h.v = 7;` | 正常 |
| 関数引数 | `f(v)` / `f(7)` | 正常 |
| 三項演算子の腕 | `Val t = c ? 1 : 2.5;` | 正常 |
| 固定長配列リテラル要素 | `Val[2] a = [1, 2.5];` | 正常 |
| **構造体リテラルのフィールド（Y1）** | `H h = {v: 7};` | **タグ喪失**（`h.v is int`=false、`as`でパニック） |
| **return文（Y2）** | `Num f() { return 10; }` | **タグ喪失**（全変種の`is`がfalse） |
| **スライスpush（Y3）** | `xs.push(5);` | **タグ喪失** |
| **スライスリテラル要素（Y3）** | `Val[] ys = [7];` | **タグ喪失** |

## 再現コード

```cm
import std::io::println;
typedef Num = int | double;
struct Box { Num n; }
Num give_int() {
    return 10;
}
int main() {
    Box b = {n: 7};
    println("{b.n is int}");
    // Y1: 期待true → false
    Num r = give_int();
    println("{r is int} {r is double}");
    // Y2: 期待true false → false false
    Num[] xs = [];
    xs.push(5);
    println("{xs[0] is int}");
    // Y3: 期待true → false
    int v = r as int;
    // 実行時パニック: invalid union cast
    return 0;
}
```

## 原因

MIRのlet初期化・代入loweringはユニオン型宛先を検出して `MirRvalue::cast(operand, union_type)`（タグ＋ペイロード書き込み）を発行するが、この検出が以下の経路に存在しない。

1. return文のlowering: 戻り値型がユニオンでも変種値をそのままreturn localへコピーする。
2. 構造体リテラルのフィールド書き込み（construct.cpp）: フィールド型がユニオンでも変種値をそのままフィールドへstoreする。
3. スライスpush/スライスリテラル要素: 要素型がユニオンでも変種値のビットをblobとして格納する（`slice_elem_dispatch`はUnionをBlobクラスにするが、Blob格納の前段でユニオン構築が行われない）。

## 修正方針

1. 「宛先型がユニオンで値が変種型なら構築Castを挿入する」共通ヘルパ（`ctx.coerce_to_union(value, dest_type)` 相当）をLoweringContextへ追加し、let/assignの既存実装をこれに置き換える。
2. return lowering・構造体リテラルフィールド・push引数・配列/スライスリテラル要素・（網羅のため）match腕とdefault引数の各サイトで、宛先型を渡して同ヘルパを通す。第3段の期待型API（infer_type_expecting）が各サイトへ期待型を届けているため、MIR側は届いた宛先型を使うだけにする。
3. 消費サイトの網羅は本文書のサイト別マトリクスを回帰テスト化して保証する（正常サイトも含めて固定し、将来のサイト追加時の欠落を検出する）。

## テスト計画

- regression: サイト別マトリクスの全行について `is` 判定と `as` ダウンキャストの値検証（int/double/string/null変種、構造体フィールド・ネスト構造体を含む）。
- integration（native/jit/wasm/js）: 再現コードの期待値実行。ユニオンのタグはバックエンド共通表現のため4系で一致を確認する。

## 検出経緯

v0.17.0全修正後のレイヤー別レビュー（第4ラウンド）で検出。最小再現は `.tmp/bughunt4/min_u_field.cm` / `min_u_ret.cm` / `min_u_sites.cm`、網羅バッテリーは同 `u/`。
