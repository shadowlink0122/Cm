# v0.17.2 バグ記録: JSバックエンドのスライスポインタ引数（K[]*）未対応

セルフホスティング向け標準ライブラリの実装中に発見した7件目の不具合の記録。
`TreeMap.keys_in_order()` の内部再帰ヘルパー `collect_keys(int node, K[]* out)` が最初の顕在化点で、v0.17.2ではライブラリ側の書き換えで回避した（バックエンド側は未修正の既知制限として記録）。

## 症状

スライスへのポインタ（`K[]*`）を関数引数として渡し、呼び出し先で `(*out).push(...)` すると、JSバックエンドだけ実行時エラーになる。

```
TypeError: Cannot read properties of undefined (reading 'undefined')
    at TreeMap__int__bool__collect_keys (...:776:38)
```

native（JIT/AOT）・interpreterでは正常動作する。

## 真因

JSバックエンドのポインタ表現は `{__arr, __idx}`（配列＋添字のセル参照）だが、スライス（JS配列）そのものを指すポインタのデリファレンス＋メソッド呼び出し（`__cm_unwrap(out.__arr[out.__idx]).push(...)`）で参照セルの解決が壊れる。
ローカル変数のアドレス渡し一般では動くが、再帰呼び出しへ引き回されるスライスポインタで `__arr` が未定義になる。

## 回避（ライブラリ側）

`TreeMap.keys_in_order()` を再帰ヘルパー＋出力ポインタ方式から、明示スタックの反復in-order走査へ書き換えた（`libs/std/collections/treemap.cm` に制約コメントあり）。
挙動・計算量（O(n)）は同一で、全バックエンドで同一結果になる。

## 残課題（バックエンド側）

JSバックエンドのスライスポインタ引数の参照セル解決の修正は今後の課題として記録する。
それまで、libsおよびユーザコードでJS対象のコードは「スライスを関数で育てる」場合に出力ポインタ引数でなく戻り値返し・反復走査を使うこと。

## 回帰テスト

- `tests/common/stdlib/collections/treeset_basic.cm` / `treemap_remove.cm` / `treeset_balance.cm` — jsスイートで反復走査版keys_in_orderの動作を担保
