# v0.17.2 バグ修正: ジェネリック特殊化とメソッドレシーバのMIR実体参照

セルフホスティング向け標準ライブラリの実装中に発見した3件のコンパイラバグの記録。
いずれもstdの新モジュール（TreeSet/HashSet/Vector.sortBy等）が最初の実利用者となって顕在化した潜在バグで、v0.17.2で修正済み。

## バグ1: フィールドレシーバのメソッド呼び出しがコピーに対して実行される

**症状**: `w.c.bump()` のように構造体フィールドをレシーバにしたメソッド呼び出しで、メソッド内の `self` フィールド変異が呼び出し元に反映されない（非ジェネリックでも発生）。
HashSet/TreeSetのような「内部にマップを持つラッパー型」が全滅する致命的な問題だった（`self.map.insert()` の件数更新が消え、ヒープ経由の書き込みだけが偶然残る）。

**真因**: MIRのメソッド呼び出しlowering（`expr/call.cpp` のself引数処理）が、レシーバ式の場所化を変数参照（HirVarRef）とポインタデリファレンスの2形だけ特別扱いし、フィールドアクセス（HirMember）等はフォールバックで「一時コピーへの参照」を渡していた。
場所化API `lower_place`（メンバ・インデックス・デリファレンスのチェーンをMirPlaceへ写す唯一の機構）は存在したが、self引数経路から使われていなかった（`resolve_receiver_place` は定義のみのデッドコードだった）。

**修正**: self引数処理へ場所化分岐を追加し、`lower_place` が解決できるレシーバは実体のアドレス（投影付きPlaceへの `MirRvalue::ref`）を渡すようにした。右辺値レシーバ（関数戻り値への直接呼び出し等）のみ従来のコピー渡しへフォールバックする。

## バグ2: ジェネリック関数の関数ポインタ引数でシグネチャが未置換

**症状**: `<T> void f(int*(T, T) cmp, ...)` やジェネリックimplメソッドの `int*(T, T)` 引数経由の間接呼び出しが、LLVM検証エラー「Call parameter type does not match function signature」で失敗する。
`Vector<T>.sortBy(比較ラムダ)` が最初の顕在化点。

**真因**: 型パラメータ置換が2実装とも関数型の内側を辿っていなかった。

- 正準API `ast::substitute_type_params` は element_type / type_args のみ再帰し、`param_types` / `return_type` を辿らず、さらに「リーフ共有」早期リターンの判定にも関数型フィールドが含まれていなかった（未置換のまま共有返却）。
- mono側の `substitute_type_in_type` にはFunction型の分岐自体が無かった。

**修正**: 両実装にFunction型の再帰（param_types / return_type）を追加した。

## バグ3: ジェネリックメソッドが返す `K[]` スライスの要素幅ずれ

**症状**: ジェネリックimplメソッド内で構築した `K[]`（ローカルまたはフィールド）を返すと、呼び出し側の要素読みが交互にゴミ値になる（`[1, ゴミ, 2, ゴミ]`）。メソッド内での読みは正常。
TreeMapの `keys_in_order()`（in-order走査の返却）が最初の顕在化点。

**真因**: ジェネリック関数のMIRはスライス生成の要素サイズを「未解決のK（構造体扱いの既定幅）」で**定数として焼き込む**（`cm_slice_new(elem_size, cap)` の第1引数・`cm_array_to_slice(ptr, len, elem_size)` の第3引数）。
mono特殊化はローカルの型は置換するが、この埋め込み定数と、そもそもターミネータ（呼び出しの引数・戻り値格納先）のplace型を置換対象にしていなかった。
その結果ヘッダの `elem_size` が過大なまま残り、blob規約のpush/getはヘッダ基準で整合する一方、呼び出し側の静的ストライド（int=4）と食い違って読みがずれていた。

**修正**（`mono/specialize.cpp`）:

1. ターミネータ（Call引数・destination・SwitchInt判別値）にも型置換を適用する（従来はstatementのみ）。
2. `cm_slice_new` / `cm_array_to_slice` の要素サイズ引数を、置換後の宛先スライス型から `layout::slice_elem_stride_of`（集約サイズは `calculate_specialized_type_size`）で再計算して差し替える。blob系push/getはヘッダの `elem_size` を参照するサイズ非依存の規約のため、ヘッダサイズの補正だけで全経路が整合する。

既存の `sizeof_for_T` マーカー（ジェネリックsizeofの遅延解決）と同じ「ジェネリック依存の定数はmonoで再計算する」方針への追従であり、スライス生成の2関数がこの方針から漏れていた。

## 回帰テスト

- `tests/common/structs/field_receiver.cm`: フィールドレシーバ変異（直接・main経由・非ジェネリックimpl経由・ジェネリックimpl経由）
- `tests/common/generics/functions/fnptr_param.cm`: ジェネリック自由関数・implメソッドの関数ポインタ引数
- `tests/common/generics/slices/method_return.cm`: `K[]` 返却（ローカル構築・フィールド構築・部分修飾）と要素値の完全一致
- TreeMap/TreeSetのin-order走査テスト（stdlib側）も同経路を常時検証する
