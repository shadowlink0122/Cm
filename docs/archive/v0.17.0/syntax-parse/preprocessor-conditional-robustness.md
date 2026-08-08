# R6: 条件付きコンパイルディレクティブの堅牢性（#endif非認識・閉じ忘れ黙殺・診断位置欠落）

**ステータス:** 修正済み
**重大度:** High

## 症状（実測: cm 0.17.0、プローブ `.tmp/bughunt6/attrs/`）

1. **`#endif`が認識されず後続コードが無診断で消滅する。** 実装（`src/internal/preprocessor/conditional.cpp` parse_directive）は`#end`のみ認識し、`#endif`はDirective::None扱いで通常行として素通りする。偽条件の`#ifdef __NEVER_DEFINED__ ... #endif`では条件ブロックが閉じないままEOFまで全行を飲み込み、ファイル後半の関数定義が丸ごと消える。`cm run`は「entry point 'main' not found (the file is a module...)」とmainが存在するのに誤誘導し、**`cm check --strict`はerrors:0で緑**になる。mainを持たないモジュールなら関数消滅が完全に無症状になる。
2. **`#ifdef`の閉じ忘れがEOFで無診断**（processのEOF時にスタック非空検査なし）。真条件なら動いてしまい、偽条件なら1と同じ全消滅になる。
3. **過剰な`#end`が黙って無視される**（空スタックpopを無視）。
4. **`#define`は文法書（cm_grammar.md）に定義があるが実装はディレクティブ非対応。** プリプロセッサが通常行として素通し→パーサが「Unknown or invalid directive after '#'」で拒否する。受理はされないが、ユーザー定義シンボルを立てる手段が言語内に存在せず、`#ifdef`は組み込みシンボル（`__macos__`等）とCLI定義専用である旨がどこにも明記されていない。
5. **プリプロセッサ/パーサ経由の構文エラー全般に行番号・桁がなく**、自ファイルなのに「syntax error in imported module '<メインファイルの絶対パス>'」と表記される（X5で修正した意味解析系の行番号品質と対照的）。

## 期待仕様（提案）

- `#endif`を`#end`の別名として認識する（cm_grammar.md・VSCode文法とも`#endif`を記載済みのため、実装側を合わせるのが互換的。`#end`のみを正とするなら文法書とVSCode文法から`#endif`を削除し、`#endif`使用時に専用診断を出す）。
- EOF時にスタック非空なら「unclosed #ifdef (opened at line N)」をエラーにする。空スタックへの`#end`もエラーにする。
- `#define`は「未実装ディレクティブ」の専用診断にし、cm_grammar.mdの`#define`定義を実装状況（未実装・-D/組み込みシンボル参照のみ）へ追従させる。
- プリプロセッサ段のエラーへ元ファイル名・行番号を付与し、「imported module」表記はimport経由の場合に限定する。

## 修正方針

conditional.cppのparse_directiveへ`endif`分岐を追加し、process()へ行番号追跡（現在も行単位ループなので容易）と終端検査を追加する。診断はDiagnosticEmitter経由（診断エンジン統一済み）でMsgId追加・i18n対応する。

## テスト計画

`tests/common/preprocessor/`へ: `#endif`受理（真/偽条件×後続コード保存）・閉じ忘れエラー・過剰`#end`エラー・`#define`専用診断のエラーテスト。既存ifdef系テストの回帰確認。

## 実装記録（修正済み）

1. **#endif別名**: `parse_directive`（src/internal/preprocessor/conditional.cpp）へ`endif`分岐を追加し`#end`の別名として認識する（文法書・VSCode文法の記載に実装を合わせる互換案を採用）。偽条件`#ifdef __NEVER_DEFINED__ ... #endif`の後続コードが保存されることを真/偽/else/ネスト（#endと#endifの混在）で回帰固定した。
2. **構造検査の診断化**: `process()`へ行番号追跡とIssue出力（UnclosedConditional/UnmatchedDirective/DefineNotSupported）を追加した。EOF時のスタック非空は開始行・シンボル名付きの「unclosed conditional block」、空スタックへの`#end`/`#endif`/`#else`は「without a matching #ifdef/#ifndef」、`#define`は「not supported」+`-D<name>`と組み込みシンボルの案内を専用診断にした（従来は順に全消滅・黙殺・パーサ段の分かりにくいエラー）。
3. **エラー伝播**: module graphの`apply_conditional`がIssueをファイルパス・行番号付きのi18nメッセージ（en/ja）へ整形しgraph.errorへ格納、既存のCliPreprocessorError経路で表示される。importされたモジュール内の違反もそのモジュールのパスで特定できる。
4. **文法書の追従**: cm_grammar.mdのプリプロセッサ節から実装されていない`define`を削除して`end`を追記し、`#end`/`#endif`同義・`#define`未実装（-D/組み込みシンボル案内）・構造違反のエラー化を明記した。
5. **回帰**: tests/common/preprocessor/endif_alias.cm（真/偽/else/ネスト混在の後続コード保存）・エラーテスト3本（閉じ忘れ・過剰#end・#define）・i18n E2E 2ケース（en/ja）を追加し、既存ifdef系テストの非回帰を確認した。
6. **スコープ外（R14へ委譲）**: パーサ段の構文エラー全般の行番号・桁欠落と「imported module」誤表記（症状5）は[syntax-error-diagnostic-quality.md](syntax-error-diagnostic-quality.md)で修正済み。本修正で追加したプリプロセッサ診断自体はファイル・行番号付きで表示される。

