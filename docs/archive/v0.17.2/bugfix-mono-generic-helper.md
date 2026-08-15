# v0.17.2 バグ修正: 総称本体スキャンの既定int偽特殊化が総称ヘルパー連鎖を汚染する

セルフホスティング補強（stdlib-hardening）の実装中に発見した8件目の不具合の記録。
`std::slices::sort<string>`（importしたモジュール内の総称free関数から別の総称ヘルパー`sift_lt`を呼ぶ連鎖）が最初の顕在化点で、v0.17.2で修正済み。

## 症状

importしたモジュールの総称関数 `sort<T>` を文字列で使うと、内部ヘルパー `sift_lt` が**int版として特殊化・呼び出しされ、ソートが無言で無効になる**（配列が並び替わらない・エラーなし）。
同じコードをプログラムと同一ファイルに置くと正しく動くため、モジュール分割が引き金に見える。

## 真因（3要素の合成）

1. **総称本体のスキャンによる既定int偽リクエスト**: モノモーフィゼーションのpass 0は全関数（未特殊化の総称本体を含む）をスキャンする。総称 `sort` 本体内の `sift_lt(xs, ...)` は引数型が `T[]`（未確定）のため構造的単一化が失敗し、「推論できなかった場合の既定int」で `sift_lt__int` の特殊化リクエストが作られる（呼び出しサイトは総称`sort`本体）。
2. **生成直後の即時呼び出し書き換え**: `generate_generic_specializations` は各特殊化を生成した直後にそのリクエストの呼び出しサイトを書き換える。偽リクエストの書き換えが**まだcloneされていない総称 `sort` 本体を `sift_lt__int` 呼びへ汚染**する。
3. **生成順序が辞書順**: リクエスト表は特殊化名キーのmapで、モジュールprivate改名 `__cm_priv_mod_0_sift_lt__int` は `sort__string` より辞書順で先に処理される。汚染後に `sort__string` がcloneされ、int版ヘルパー呼びを引き継ぐ。同一ファイル版はヘルパー名 `sift__int` が `hsort__string` より後になり、偶然clone が先行して無事だった（＝アルファベット順の運で挙動が変わる）。

汚染されたclone内の呼び出しは具体名（`__int`）のため次パスのスキャン対象にならず、`sift_lt__string` は永遠に生成されない。

## 修正

`mono/driver.cpp` の不動点ループで、**総称本体そのものをスキャン対象から外した**。
総称本体内の総称呼び出しは、呼び出し元が特殊化された後（実型が確定したclone）を次パスで走査したときに正しい型で要求される（既存のpass反復機構がそのまま担う）。
これにより偽リクエスト（既定int）の発生源が消え、生成順序依存の汚染も起きなくなる。

デバッグの決め手は `-d=debug` のMONOログ（`WARNING: Could not infer T, defaulting to int` → `Scanned call in sort ... type args: int` の並び）と、pass 1のスキャンが `sort__string` 内で具体名 `__cm_priv_mod_0_sift_lt__int` を見ている（＝総称名が既に消えている）ことを確認する一時診断だった。

## 回帰テスト

- `tests/common/generics/functions/module_helper_chain.cm` — importモジュール内の総称free関数→総称ヘルパー連鎖をstring/long/比較ラムダで検証（本修正で新規追加）
- `tests/common/stdlib/slices/sort.cm` — string[]を含む全型のソート（顕在化点）
