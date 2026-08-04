---
title: 型検査のエラー検出漏れ3件（Z4）
parent: v0.17.0 Design
---

# 型検査のエラー検出漏れ3件（Z4）

## 概要

エラーパターンテストの整備（`tests/common/errors/`へ13本追加）に伴い、明らかに不正なプログラムをcheckerが受理する穴を3件検出した。
いずれも「エラーテストとして追加しようとしたら受理された」ことで発覚しており、修正後にエラーテストへ昇格させる。

## 穴1: スライスpush引数の型不一致が無診断（Critical）

```cm
int[] xs = [];
xs.push("s");
// checker受理 → MIR以降でエラー型が漏れ「internal error: unresolved type artifact '__error__len'」
```

`push`の型検査（`checking/call/method.cpp`）は引数を`infer_type_expecting`で推論するが、推論結果と要素型の`types_compatible`検査を行っていない。
エラー型の下流漏れを`__error__`成果物検査（diagnostics-engine-unification）が内部エラーとして捕捉するため無言のゴミ値にはならないが、ユーザー向け診断（expected int, got string）を出すべき箇所である。
`insert`等の他の値引数APIも同時に監査する。

## 穴2: ユニオンの非変種型への`as`が無診断でゴミ値（Critical)

```cm
typedef V = int | string;
V v = 1;
double d = v as double;
// checker受理 → 実行時ペイロードのビット再解釈で 5e-324 等のゴミ値
```

`is`の非変種検査（「the target type 'bool' is not a variant of the union」）は実装済みだが、`as`ダウンキャストに同じ変種集合検査が無い非対称。
実行時のタグ検査（invalid union cast）にも掛からず（doubleは変種に無いためタグ比較自体が構成されない）、ペイロードのビット再解釈が素通りする。
`as`にも`is`と同一の変種集合検査を追加して静的に拒否する。

## 穴3: ループ外の`break`/`continue`が黙って無視される（Medium）

```cm
println("before");
break;
// checker受理・MIRで文ごと消滅
println("after");
// 両方出力される
```

checkerにループ深度の追跡が無く、ループ外の`break`/`continue`が受理される。MIR loweringはターゲットブロックが無いため文を捨てており、制御フローの誤りが無言で消える。
checkerにループ/switchネスト深度を持たせ、外側での`break`/`continue`を診断する。

## 整備済みのエラーパターンテスト

添字型（`a["i"]`・`a[1.5]`）・初期化型不一致・const再代入・未知フィールド・引数個数/型・戻り値型・リテラル型許容外・ユニオン非変種`is`・スカラへのメソッド・非boolへの論理演算子の13本を`tests/common/errors/`へ追加した（`.error`には代表診断メッセージを記録。ランナーは非ゼロ終了を検証する）。
本文書の3穴は修正後に同形式でエラーテストへ追加する。

## 検出経緯

第4ラウンド追補（エラーパターンテスト整備）で検出。当初のプローブは`grep "error"`が集計行`errors: 0`へ誤マッチして穴を見逃しており、終了コード判定へ修正して発覚した（検査スクリプトの教訓として記録）。

## 実装記録（2026-08-05）

- 穴1（push引数型不一致）: method.cppのpush検査で`infer_type_expecting`の結果と要素型を`types_compatible`で照合し、不一致は新設i18n診断（TcSlicePushTypeMismatch）で拒否する。ユニオン要素への変種値push（Y3のユニオン構築対象）は変種集合との互換で許可する。
- 穴2（非変種as）: primary.cppのCastExpr検査（非type_check側）で、オペランドの解決型がユニオンの場合に`is`と同一の変種集合検査を追加した（メッセージはTypeTheTargetTypeIsNotを共用）。typedef別名経由を含む同一ユニオンへの恒等キャストは許可する。
- 穴3（ループ外break/continue）: checkerへ`loop_depth_`を追加し、while/for/for-in本体で増減させ、深度0でのbreak/continueを新設i18n診断で拒否する。Cmのswitchは自動break（明示breakはループ専用）のためswitch深度の追跡は不要。defer内のbreak（defer_break.cm）はループ内チェックのため影響しない。
- エラーテスト4本（push_elem_type_mismatch・union_as_nonvariant・break_outside_loop・continue_outside_loop）をtests/common/errorsへ追加した。正当パターン（ユニオン変種push・有効なasダウンキャスト・ループ内break/continue・deferとの併用）の非退行を確認し、全スイート通過。
