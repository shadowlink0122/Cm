---
title: for-inイテレータプロトコルの検査穴（Q1）
parent: v0.17.0 Design
---

# for-inイテレータプロトコルの検査穴（Q1）

## 概要

for-inのイテレータ発見（`iter()`メソッド検出）に2つの検査穴がある。

1. **has_next欠如が無診断**: `iter()`と`next()`だけ実装した型をfor-inに使うと、checkerは受理するがMIRが`Type__has_next`呼び出しを発行し、未解決シンボルでJITエラー/リンクエラーになる（診断なし）。
2. **Option返しnextの要素型が未処理**: `Option<int> next()`のイテレータでは、ループ変数の型が`Option<int>`のまま（checkerの`element_type = next()の戻り値型`）となり、要素利用が型エラーになる。has_next/next（非Option）が実プロトコルだが、Option形を受理してしまい原因の分かりにくいエラーになる。

## 再現コード

```cm
struct Counter { int cur; int max; }
struct CI { int cur; int max; }
impl Counter {
    CI iter() {
        return CI { cur: self.cur, max: self.max };
    }
}
impl CI {
    Option<int> next() { ... }
    // has_next無し
}
for (v in c) { sum = sum + v; }
// → checker: 「Add operator requires numeric operands」（vがOption<int>）
// has_nextを消してint next()にすると → JIT: Symbols not found: [ _CI__has_next ]
```

## 修正方針

1. for-in検査（check_for_in）で`iter()`発見時に、イテレータ型へ`has_next(): bool`と`next()`の両方が存在することを検査し、欠如は診断で停止する（「iterator type 'CI' must implement has_next(): bool」）。
2. `next()`の戻り値型が`Option<T>`の場合の扱いを仕様決定する: (a) プロトコル外として診断（has_next/next形を案内）、または (b) Option形をサポートし要素型をTへunwrap（loweringでis_some/unwrap展開）。チュートリアル（iterator頁）の記載と揃える。
3. エラーテストを追加する（has_next欠如・next欠如・シグネチャ不一致）。

## 検出経緯

未修正バグ調査（Q系）で検出。最小再現は `.tmp/bughunt5/q2x3.cm`（Option形）・`q2x4.cm`（has_next欠如の未解決シンボル）。

## 実装記録（修正済み）

1. `check_for_in`（src/internal/types/checking/stmt.cpp）のiter()発見直後に、イテレータ型のプロトコル検査を追加した: `has_next`の存在と`bool`戻り値、`next`の存在（void不可）、`next`の戻り値がOptionでないことを検査し、違反はi18n診断（TcIteratorMissingHasNext / TcIteratorHasNextMustReturnBool / TcIteratorMissingNext / TcIteratorNextMustNotReturnOption、ja訳付き）で停止する。
2. 修正方針2の仕様決定: Option返しnextは(a)プロトコル外として診断を採用した。has_next+非Option nextが実プロトコルであり、Option形の暗黙unwrapは全バックエンドのlowering追加を要する新機能で、従来もOption形は一度も動作していなかった（破壊的変更にならない）ため。診断文で正プロトコル（`bool has_next()` + 非Option `next()`）を案内する。
3. エラーテスト4本（has_next欠如・next欠如・has_next非bool戻り・Option返しnext）をtests/common/errors/forin_iterator_*.cmへ追加し、i18n E2E（q1-forin-iter-en/ja）でen/ja両表示を固定した。既存イテレータスイート12件・errorsスイート70件は全通過。
4. チュートリアルのfor-in節（basics/control-flow.md ja/en）へ「独自イテレータ（iter()プロトコル）」を新設し、プロトコル要件と診断を明文化した（従来はイテレータプロトコル自体が未記載だった）。
