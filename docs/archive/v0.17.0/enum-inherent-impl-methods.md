---
title: enumへのinherent implメソッドが未サポート（Q5）
parent: v0.17.0 Design
---

# enumへのinherent implメソッドが未サポート（Q5）

## 概要

単純enumへのinherent implメソッド（`impl Color { string name() { return match(self) {...}; } }`）が、呼び出し時に「Unknown method 'name' for type 'int'」で拒否される。
単純enumの値がint表現へ正規化された後にメソッド解決が行われるため、enum名でのメソッド表引きに到達しない。機能として未サポートなら「enumにはimplできない」旨をimpl宣言時点で診断すべきで、現状はimpl宣言が黙って受理され呼び出し側で的外れな型名（int）のエラーになる。

## 修正方針

1. 仕様決定: enumへのinherent implを (a) サポートする（メソッド解決でenum元型名を保持しint正規化前にディスパッチ）か、(b) 非サポートとしてimpl宣言時に診断するか。matchベースのname()等は実用価値が高く(a)を推奨。
2. (a)の場合、self型はenum（int表現）で渡し、`match (self)`のタグ比較がそのまま機能することを確認する。Tagged Union enum（ペイロード付き）への拡張も同時に検討する。
3. 回帰: 単純enum・ペイロード付きenumそれぞれのメソッド呼び出し（値レシーバ・match内・補間内）。

## 検出経緯

未修正バグ調査（Q系）で検出。再現は `.tmp/bughunt5/q3/s01_enum_methods.cm`。

## 実装記録（修正済み・方針(a)サポートを採用）

enum正規化の全面遅延（method-resolution-unificationの最高リスク項目）は行わず、「int解決時にenum名をnameへ保持する」名前ピギーバック方式で(a)を実装した。型表示（type_to_string）・互換判定・codegenは全てkind駆動のため、名前保持は既存動作へ影響しない。修正は5箇所:

1. **checker/型解決**: `resolve_typedef`（utils/compat.cpp）の値enum→int解決2経路で、返すint型のnameへ元のenum名を保持する。
2. **checkerメソッド解決**: `infer_member`（call/method.cpp）のメソッド検索型名リストへ、Int kind×enum名保持のレシーバのenum名を追加する（既存の汎用検索がprivate検査・引数検査・戻り値型をそのまま処理する）。
3. **HIRメソッド呼び出し**: `lower_member`（expr_member.cpp）で同条件のレシーバのマングル対象型名をenum名へ差し替える（`Color__name`が生成される）。値enumメソッドのselfはポインタでなく素の値（int）のパラメータにする（decl.cpp。ポインタ形はjs等で呼び出し側の値渡しと表現が割れる——LLVMはself特別処理で偶然整合していた）。
4. **MIR呼び出し**: `is_method_call`判定（expr_call.cpp）へenum名prefixを追加し（タグ付きenumのself（`*__TaggedUnion_`）がアドレス渡しになる）、値enumレシーバのselfは常に値渡しへ正規化する（宣言戻り値型経由では未解決のStruct kind名で届き構造体扱いのアドレス渡しに乗っていた）。
5. **MIRアクセス/レイアウト**: 値enumの`__tag`アクセスを恒等化（expr/access.cpp。従来はenum名を一律タグ付き扱いしfield(0)射影でint値を壊していた）、MIR letの`__TaggedUnion_`変換をタグ付きenum限定に修正（stmt/let.cpp。他2サイトと同条件へ整合）、タグ付きenumメソッド内matchのペイロード射影へselfポインタのderefを追加（expr/construct.cpp）。

この実装でタグ付きenum（ペイロード付き）のメソッドも動作する（修正方針2の拡張検討を包含。selfポインタ渡し+match/ペイロード束縛が全経路一致）。回帰は`tests/common/enum/inherent_impl_methods.cm`（変数/関数戻り値/ループ変数レシーバ・引数付き・self比較・self as int・int互換性の不変・タグ付きmatch束縛）で、jit/native O0/O2/js/wasm出力一致を確認した。
既知の制約: 値enumメソッド内の`self`再代入はコピーに閉じる（値意味論。チュートリアルに明記）。enum正規化の構造的遅延・メソッド表キーの正準化は[method-resolution-unification.md](../../design/v0.17.0/method-resolution-unification.md)が引き続き扱う。
