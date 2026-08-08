---
title: 診断エンジンの統一（発行・表示・エラー型隔離）
parent: v0.17.0 Design
---

# 診断エンジンの統一（発行・表示・エラー型隔離）

## 概要

診断（エラー・警告）の発行と表示が層ごとに別実装になっており、表示品質・座標系・i18n適用・致命性の扱いが揃っていない。
rustc_errorsのDiagCtxt（全層が共有する単一の診断コンテキストと構造化診断）を参照モデルに、発行APIと表示経路を一本化し、「エラー型が下流へ流れて誤コンパイルになる」クラスのバグを構造的に塞ぐ。

## 現状の実測と問題

- 発行機構が最低4系統ある: (1) Parserのdiagnostics()、(2) TypeCheckerのdiagnostics()、(3) MIR loweringの `debug::log(Stage::Mir, Level::Error, ...)`（ログであり診断でない）、(4) codegen各所のstd::cerr直書き。
- 表示側も分散しており、source_map写像はドライバの各消費地点で手書きされる（X5修正でbuild.cppとcheck.cppへ同一の写像+ファイル読込コードを複製した実績。将来のドライバ追加でまた漏れる構造）。
- MIRレベルの問題検出（「レシーバのスライス場所を解決できませんでした」等）はログ出力のみでコンパイルが続行し、黙って壊れたコードを出す（C11の黙殺禁止方針と矛盾する実装状態）。
- 型検査のエラー回復で `<error>` 型が下流へ流れ、`__error__len` のような未解決シンボル発行やptrtointフォールバックとして顕在化した（B6/B7/W5(d)/N2はいずれもこの族）。
- i18n（MsgId表）は一部の診断のみで、パーサ診断・MIR診断は英語直書きが混在する。
- 既存のsrc/internal/diagnostics/（682行）はLintカタログのみで、発行エンジンではない。

## 簡素化方針

1. `DiagCtxt` を新設し（cm_diag、compiler-architecture-restructure.mdの基盤層）、Parser/TypeChecker/MIR lowering/codegenの全発行をこの単一コンテキストへ集約する。診断は {severity, MsgId or message, Span, notes} の構造化データで持つ。
2. 表示はDiagCtxtのemitterが一元的に行い、Span→ファイル座標の解決（構造化import後はsource_map自体が不要）とi18nをemitter内で1回だけ適用する。ドライバは「emitterを接続する」だけになる。
3. MIRレベルの問題検出をログからDiagCtxtのエラーへ昇格し、エラー発生後はcodegenへ進まない（現在のtype_check_okゲートをMIR loweringにも延長）。
4. エラー型の隔離: `<error>` 型はTypeChecker内でのみ許容し、HIR→MIR境界で `<error>` を含む宣言・式を検査してその場で診断+停止する（`__error__*` シンボル発行の構造的禁止。type-resolution-simplification.mdの補間脱糖と相補的）。
5. 診断IDカタログ（E001/W001系）をdiagnostics/catalogへ統合し、`--explain` 相当の将来拡張の受け皿にする。

## 段階分割

1. 第1段: DiagCtxtとemitter導入。Parser/TypeCheckerのdiagnostics()をDiagCtxtへ委譲し、build.cpp/check.cppの表示複製を削除する。
2. 第2段: MIR loweringのError級debug::logをDiagCtxt発行へ置換し、エラー時はcodegen前に停止する。
3. 第3段: HIR→MIR境界のエラー型検査を導入し、`__error__` プレフィックスのシンボルがMIRに存在しないことをregressionで固定する。
4. 第4段: 残る英語直書き診断のMsgId化（i18n表への収容）。

## テスト計画

- i18nスイートで全サブコマンドの診断が同一emitter経由（--lang=ja適用・座標表示形式統一）であることを検証する。
- `__error__` シンボル不在のMIRスナップショット検査をregressionへ追加する。
- MIRエラー昇格後も既存のerrorsスイート（.errorファイル照合）が全通過することを確認する。

## 進捗

### 第1段（表示の一元化）: 実装済み

- `src/internal/base/diag_emitter.hpp/.cpp` に `DiagnosticEmitter` を新設し、診断表示（source_map写像・参照ファイル読込・重大度ラベル・import_chainトレース）を一元化した。SourceMapEntry/SourceMapは `base/source_map.hpp` の共有型へ移設し（preprocessor側はエイリアス）、表示側がpreprocessorへ依存しない層構成にした。
- build.cpp（パーサ診断・型検査診断）とcheck.cpp（パーサ診断・型検査診断）の表示複製4ブロック（計約170行）をemitter呼び出しへ置換した。X5で複製されたsource_map適用+ファイル読込コードは1実装に集約された。
- 置換により、`cm check` の型検査診断がsource_map未適用で展開後の行番号を表示していた既存の不整合（`cm run` は元ソースの4行目・`cm check` は展開後の23行目を表示する等）が解消され、全サブコマンドがRustスタイルの統一表示になった。
- check/lintのルールレベル設定（error/warning/hint昇格・無効化コメント・設定ファイル）はemitterのlabel引数と `location_manager()` で維持した。
- 検証: i18n E2E 42件・linter 10件・全12スイート通過。
- 発行側の統合（Parser/TypeCheckerが単一DiagCtxtへ直接発行する形）は未実施。現状は両者が同一の `cm::Diagnostic` 型を各自のベクタへ収集しドライバがemitterへ渡す構成で、表示の複製は解消済み。DiagCtxt導入は第2段（MIRエラー昇格で発行元が増える時点)で行う。

### 第2段（MIRエラーの診断昇格）: 実装済み

- MirLoweringBaseへ診断チャネル（`report_error(Span, message)`・`mir_diagnostics()`・`has_diagnostic_errors()`）を追加し、MirLoweringのctorでexpr/stmt loweringへ診断ベクタを共有配線した。
- MIR段階で唯一のError級ログ（スライス組み込みのレシーバ場所未解決。C11/H10の黙殺禁止ガード）を`report_error`によるエラー診断へ昇格し、build.cppはMIR lowering後に診断をDiagnosticEmitterで表示してエラー時はcodegenへ進まない。
- MIR診断のメッセージは新設時からMsgId（i18n表）で定義した（MirSliceReceiverUnresolved・MirErrorSymbol）。

### 第3段（エラー型成果物の検査）: 実装済み

- MirLowering::lower()の最終段に`check_error_artifacts`を追加し、MIRの関数名・呼び出し先FunctionRefに`__error__`プレフィックスのシンボル（未解決型のマングリング成果物。B6/B7/W5(d)/N2族）が存在しないことを検査、検出時はエラー診断として報告しドライバがcodegen前に停止する。
- regression: `tests/regression/mir_lowering_test.cpp` にジェネリックメソッドチェーン・補間内呼び出し・スライス組み込みを含む代表プログラム（cases/mir_lowering/error_artifact_free.cm）で__error__シンボル不在とMIR診断空を固定するテストを追加した。
- HIR側の宣言・式単位での`<error>`型検査（コンパイル停止をより早い段階へ移す形）は、型検査のエラー回復設計（typed-hir-single-source.md）と合わせて扱う。

### 第4段（英語直書き診断のMsgId化）: 実装済み

- 型検査・パーサの英語直書き診断207呼び出し（一意メッセージ183件）を全てMsgId（i18n表のen/ja対）へ収容した。変換は呼び出しの機械抽出（バランス括弧+文字列連結の分解）で行い、enテンプレートは既存文面の完全再構成のため英語出力はバイト同一を維持する（既存の.error照合・i18n E2Eの英語期待に影響しない）。ja訳183件を新規執筆した。
- codegenに残っていた生文字列診断（SV002/SV005/SV008の4箇所）もMsgId化した。SV008は従来enでも日本語文面だったため英語文面を新設した（照合テストなし）。SVの型幅エラー1件は日本語のみの既存文面をen/ja共通として維持した。
- ルールID付きメッセージ（[W001]等）はja訳でも末尾IDを維持し、check/lintのルールID抽出・レベル設定と互換。

## 解決記録（設計判断）

- 発行APIは「Parser/TypeChecker/MIR loweringが共通の`cm::Diagnostic`型を各自のベクタへ収集し、ドライバが単一の`DiagnosticEmitter`で表示する」構成で確定した。原案の単一DiagCtxtオブジェクト共有は、発行元が3系統で確定しており表示・座標系・i18nの一元化が emitter 側で達成できるため導入しなかった（発行元がさらに増える場合の将来課題として残す）。
- codegen段の診断（バックエンド検証エラー: SVnnn・Codegen系）はMIRにSpanが運搬されないため構造化Diagnosticではなくi18n化されたcerr出力で確定した。Span運搬はmodule-system-structural-imports実装後の座標系整理と合わせて将来検討する。
- 診断IDカタログ（E001/W001系）の`--explain`拡張向け統合（方針5）は受け皿（diagnostics/catalog）を維持したまま将来課題とする。
- 中核の4目標（表示一元化・MIRエラー昇格とcodegen前停止・エラー型成果物の構造的検査・MsgId収容）は全て実装完了し、本文書はarchiveへ移動する。
