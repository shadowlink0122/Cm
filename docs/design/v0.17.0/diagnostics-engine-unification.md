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
