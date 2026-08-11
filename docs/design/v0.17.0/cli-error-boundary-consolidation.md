# CLIフェーズ包括tryの整理と内部エラー境界の一本化（リファクタリング提案）

## 概要

`src/cmd/cm/build.cpp`はコンパイルの各段階（ターゲットフィルタ+derive展開・型検査・HIR/MIR変換）をそれぞれ広域の`try { ... } catch (const std::exception&)`で包み、捕捉した例外を`CliInternalError`（internal error (parse/typecheck/mir)）として整形している。
一方で下層（codegen・ランタイム準備・モジュール解決）は約40箇所が`throw std::runtime_error("文字列")`で失敗を報告しており、位置情報（Span）も診断コード（i18n MsgId）も持たない文字列がフェーズ境界のcatch-allまで素通りする構造になっている。
この構造には3つの問題がある。
1. ユーザー起因のエラー（未対応構文・環境不備）と真の内部バグが同じ「internal error」に合流し、報告を受けても切り分けできない。
2. tryの範囲がフェーズ全体のため、どの処理段が投げたかはメッセージ文字列に依存し、握り潰し（catch後にreturnで正常系継続する断片）の混入を防げない。
3. 診断機構（Diagnostic+i18n+Span）とthrow経路の二重系になっており、新しいエラーはどちらで報告すべきかの規約がない。

## 実測

throw std::runtime_errorの分布（src/internal、2026-08-11時点）:

| ファイル | 件数 | 性質 |
|---|---|---|
| codegen/llvm/native/codegen.cpp | 13 | 未対応MIR構造・型不一致（内部バグ系） |
| codegen/sv/testbench.cpp | 6 | 期待値ファイル形式・SV変換制約（ユーザー起因系が混在） |
| codegen/llvm/native/target.cpp | 6 | ターゲット環境不備（ユーザー起因系） |
| codegen/sv/codegen.cpp | 4 | SV変換制約（SV009等、診断コード付き文字列を手組み） |
| codegen/sv/analyze.cpp | 3 | 同上 |
| codegen/llvm/monitoring/*.cpp | 6 | 監視バッファの内部不整合 |
| preprocessor/module_resolve.cpp | 2 | モジュール解決失敗（ユーザー起因系） |

catch側はbuild.cppの3箇所（各フェーズ包括try）に加え、parser_decl・lexer・jit_engine・options等に局所catchが散在する（grep実測: 計15箇所前後）。
SVバックエンドは`error[SV009]`のような診断コード付き文字列をthrowメッセージに手組みしており、i18nテーブル（MsgId×Lang）を経由しないため言語切替が効かない。

## リファクタリング方針

- エラー報告の規約を「ユーザーに行為可能な失敗はDiagnostic（Span+MsgId）・到達したら実装バグの不変条件違反のみthrow」に定める。
- ユーザー起因系のthrow（target環境不備・モジュール解決失敗・SV変換制約）をDiagnostic発行+失敗戻り値へ置換し、i18nテーブルへ収容する（msgf断片連結は行わない）。
- フェーズ包括tryはCLI最外周の1箇所（run_buildの呼び出し元）へ集約し、フェーズ名は例外型（`CmInternalError{phase, detail}`）で運ぶ。
- 局所catchは「回復して正常系を継続する正当な理由があるもの」だけ残し、理由をコメントで明記する（黙殺catchの禁止）。

## 段階分割

1. 例外型`CmInternalError`の導入とbuild.cpp 3箇所のtry集約（挙動同一・メッセージ形式維持）。
2. ユーザー起因系throwのDiagnostic化（target.cpp・module_resolve.cpp・SV系。エラーテストの期待文字列を新診断へ更新）。
3. 内部バグ系throwのメッセージ統一（MIR文脈＝関数名+block番号を付与し、バグ報告から再現位置を特定可能にする）。

## リスク

- エラーメッセージ文字列が変わるため、`.error`期待値テストの更新を伴う（段階2で対象を列挙してから着手する）。
- 局所catchの削除は握り潰しに依存した偶然の動作を顕在化させる可能性がある（削除前に発火条件をテストで固定する）。

## テスト計画

- 各フェーズの代表的な内部エラー（未対応MIR等の人工ケース）でフェーズ名付きメッセージが出ることをregressionで固定する。
- Diagnostic化した各エラーの`.error`テストを日本語/英語の両言語で追加する。
- 全バックエンドスイートで正常系の挙動同一を確認する。

## 検出経緯

総称derive特殊化のSIGSEGV調査（2026-08-11）で、クラッシュがcatch-allに到達せずプロセス死する一方、仮に例外化されていても「internal error (mir)」では原因段の特定ができない構造を確認した。
ユーザーからのリファクタリング提案募集（tryの範囲を適切にまとめる）を受けて実測・起案した。

## 実装記録（第1段・2026-08-11）

- run_buildの3つのフェーズ包括try（parse/typecheck/lowering）を、単一実装の例外境界ヘルパ`run_protected(stage, fn)`（本体は継続=nullopt/終了=終了コードを返すラムダ）へ集約した。catch-allの重複3箇所が1箇所になり、段階名はヘルパ引数で運ぶ。
- 従来例外境界を持たなかったバックエンドディスパッチ区画（sanitize検査+emit_jit_run/emit_sv/emit_js/emit_llvm）も`codegen`段として包んだ。従来はcodegen層のthrow（native 13箇所・SV 13箇所等）がmain.cppの最外周まで素通りし「internal error (main)」と報告され段階情報が失われていた。
- 挙動変更はcodegen段のエラー表記（main→codegen）のみで、正常系・診断系の出力は同一。全バックエンドスイートで確認。
- 第2段（ユーザー起因系throwのDiagnostic化: target.cpp・module_resolve.cpp・SV系のi18n収容と`.error`テスト整備）と第3段（内部バグ系メッセージへのMIR文脈付与）は未着手の残件。
