# R14: 構文・プリプロセッサ診断の品質（行番号欠落・"imported module"誤表記・誤誘導メッセージ）

**ステータス:** 未修正（構文網羅バグ調査で検出）
**重大度:** Medium（横断）

構文網羅バグ調査の全カテゴリで繰り返し観測された診断品質の横断課題。X5（syntax-error-position-and-token-display）で意味解析エラーの行番号は改善されたが、パーサ/プリプロセッサ段のエラーが取り残されている。個別バグ（R1〜R13）の再現時、原因特定を一貫して妨げていた。

## 症状（実測: cm 0.17.0、プローブ横断）

1. **構文エラーに行番号・桁がない。** プリプロセッサ/パーサ経由の構文エラーは`preprocessor error: syntax error in imported module '<メインファイルの絶対パス>': Expected ...`のみで位置情報がない。意味解析エラー（型不一致等）は`--> file:line:col`付きで高品質なのと対照的。R5/R11/R13の未実装構文がどの行かも分からない。
2. **自ファイルを「imported module」と誤表記。** import経由でない直接コンパイルでも「in imported module」と表示される。R9のstdlib入力再exportでは診断位置が無関係な`output.cm:1:1`を指した。
3. **誤誘導メッセージ。** `#endif`で後半消滅時（R6）の`entry point 'main' not found (the file is a module...)`はmainが存在するのに「モジュールです」と誤誘導。未定義型（R10）のメソッド呼び出しは`Unknown method`で「型が未定義」と言わない。const generic（R10）の`get_n<5>()`は`Empty parentheses without lambda body`という無関係なエラー。SV構文native流入（E2）では`assign x = 2;`が`expected 'assign', got 'int'`と`assign`をユーザー型名扱い。
4. **表記の乱れ。** async拒否の英語診断に全角閉じ括弧混入（`(function: main）`）。platformタイポ（R7関連）は`warning:`ラベルなのにrc=1、ヒントの`--target=svv`は実行不能な提案。

## 修正方針

- パーサ/プリプロセッサのエラーもDiagnosticEmitter（診断エンジン統一済み）経由へ寄せ、トークンのspan（行・桁・元ファイル名）を必ず付与する。「imported module」表記はimport経由の場合のみに限定。
- 頻出の未実装・誤用パターン（タプル・参照型・未定義型・const generic・SV構文のnative流入）へ専用メッセージを用意し、汎用`Expected ...`や型名誤解釈をなくす。
- 診断ラベル（error/warning）と終了コードの整合、ヒントで提示するコマンドの実行可能性、全角/半角の統一を点検する。

## テスト計画

代表的な構文エラー（未終端・未実装構文・SV流入）に対する診断の行番号有無・ファイル名正当性をregression（tests/regression/ 診断スナップショット）で固定する。i18n E2Eと整合。X5で導入した位置検査をパーサ段へ拡張。
