# R14: 構文・プリプロセッサ診断の品質（行番号欠落・"imported module"誤表記・誤誘導メッセージ）

**ステータス:** 修正済み（構文網羅バグ調査で検出）
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
## 実装記録（2026-08-08）

4項目とも処置した。中核は「構文エラーを意味解析エラーと同水準の位置情報付き表示にする」で、モジュールグラフのパース失敗経路へSourceLocationManagerを接続した。

- 行番号・桁の欠落: module graph（graph.cpp）の各ファイルパースで、パーサ診断の先頭をSourceLocationManagerのformat_error_locationで整形するようにした（file:line:col + 該当行 + キャレット。条件コンパイルの無効化は行を空行化して保存するため行番号は原文と一致する）。GraphResult/frontendへerror_has_locationフラグを追加し、build/checkの表示側は従来の`preprocessor error:`でなく`syntax error:`（ja: `構文エラー:`）ラベルで表示する。
- 「imported module」誤表記: 直接コンパイルしたルートファイル（is_root）にはimported module表記を付けず、import先の構文エラーのみ`in imported module '<path>':`の行を付ける。
- 誤誘導メッセージ: SV構文のnative流入（`assign x = 2;`は`assign`が型名扱いされる）は、R10の未定義型診断を土台にassign/initial/genvar/endmodule/posedge/negedgeを`'{0}' is a SystemVerilog construct; --target=svでのみ使用可`の専用メッセージへ差し替えた。未定義型メソッド呼び出しの`Unknown method`誤誘導とconst genericの`Empty parentheses`誤誘導はR10の宣言時診断化で先行検出されるため実質解消済み。`entry point 'main' not found`の誤誘導もR6の閉じ忘れ診断が先行するため再現経路が消滅している。
- 表記の乱れ: 英語診断の全角閉じ括弧3件（await検出・step()制約・platform不一致）を半角へ修正。platform不一致は`warning:`ラベルなのにrc=1だったのを`error:`ラベルへ整合。`//! platform:`のタイポ（svv等）は従来ヒントがタイポをそのまま`--target=svv`と提案していたが、有効プラットフォーム一覧つきの専用エラー（CliUnknownPlatformDirective）にし、--targetヒントは正当名の不一致時のみ出す。

テスト: i18n E2Eへ5ケース追加（構文エラーの位置・ラベルen/ja、platformタイポ、SV流入en/ja。負のアサートで`imported module`/`preprocessor error`/`--target=svv`/`warning:`の不在も固定）。全13スイートPASS。

残課題: パーサ診断は先頭1件のみ表示（複数エラーの一括表示は将来課題）。表示形式は意味解析エラーの`-->`スタイルと完全一致ではない（軽量形式）。DiagnosticEmitterへの完全統合は行わずgraph.cpp内整形とした（表示経路が2系統のまま）。
