# v0.17.2 バグ修正: ユーザ定義Optionとプレリュード衝突のライブラリ波及（選択importの即時型検査）

セルフホスティング向け標準ライブラリの実装中に発見した5件目の不具合の記録。
パース関数の `std::strings::parse` への移設に伴い、ローカルで `enum Option { None, Some(int) }` を再定義している既存プログラム4件（`tests/common/types/enum/`）が「libs内のコード」の型エラーでコンパイル不能になった。
v0.17.2ではライブラリ側の構成で回避し、コンパイラ側の2つの潜在問題は設計課題として記録する。

## 症状

`import std::io::println;` しかしていないプログラムで、ユーザが `Option` を独自enumとして再定義すると、`libs/std/strings/parse.cm` 内部の `Option::Some(true)` が「expected int, got bool」の型エラーになりコンパイルが失敗する。
ユーザのコードにもimportにも `parse` は登場しないため、エラー位置がlibs内部を指し原因が追いにくい。

## 真因（2つの潜在問題の合成）

1. **プレリュードOptionのフラット上書き**: ユーザ定義の `Option` / `Result` は組み込み型をプログラム全体のフラット名前空間で上書きする仕様（`typedecl.cpp` のenum登録）。そのため、コンパイル対象に含まれたlibsモジュールの `Option<T>` 参照もユーザ型へ解決される。これはHEAD時点から存在する挙動で、`import std::io::*;` とローカルOption再定義の組み合わせでは移設前から同じエラーになる（今回顕在化しただけの既存問題）。
2. **選択importの即時型検査**: `import std::strings::parse::{parse_int, ...};` のような選択importは、対象関数の本体をその場で型検査する。一方 `import std::strings::parse::*;`（ワイルドカード）と `export import` は利用時まで検査を遅延する。移設時にinput.cmへ内部利用向けの選択importを書いたため、printlnしか使わないプログラムにまでparse.cmの本体検査（＝衝突1の顕在化）が波及した。

## 修正（ライブラリ構成での回避）

- `libs/std/io/console/input.cm` の内部importを選択importからワイルドカード（`import std::strings::parse::*;`）へ変更した。`input_int` 等の解決は維持され、parse本体の検査は実際に使うプログラムまで遅延される。
- `libs/std/strings/mod.cm` のファサード（`export import std.strings.chars / parse`）は遅延のため安全であり維持する。両ファイルに「chars/parseを内部利用する際は選択importを使わない」制約コメントを残した。

## 言語側のフォローアップ（未実装・設計課題）

- 型解決のモジュール分離: libsモジュール内の `Option` 参照はユーザ再定義の影響を受けずプレリュード定義へ解決されるべき（`__prelude` 属性による区別は登録済みで、解決側の分離が未実装）。
- 選択importとワイルドカードimportで型検査のタイミングが異なる非一貫性の解消。

## 回帰テスト

- `tests/common/types/enum/{simple_compare, variable, guard_condition, match_extract}.cm` — ローカルOption再定義プログラムが引き続きコンパイル・実行できることを担保する既存テスト（本件の検出元）
- `tests/common/stdlib/core/parse_option_test.cm`・`tests/common/stdlib/strings/parse_radix.cm` — 移設後のparse APIの機能検証
