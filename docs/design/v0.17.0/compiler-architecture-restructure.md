---
title: コンパイラ全体構成の再編（ステージ分離とサブコマンドの切り分け）
parent: v0.17.0 Design
---

# コンパイラ全体構成の再編（ステージ分離とサブコマンドの切り分け）

## 概要

現在のCmコンパイラはほぼ全機能が `src/internal/` の単一ビルド単位に同居し、ステージ間の依存が暗黙的で、サブコマンド（check/lint/fmt/compile/run）はドライバ側（build.cpp 701行・check.cpp 297行）がパイプラインを都度手書きして重複している。
本文書はRustコンパイラ（rust-lang/rust の compiler/ 配下のクレート分割）を参照モデルに、Cmをステージ指向のライブラリ群へ再編し、サブ機能を独立した薄いドライバへ切り分ける全体像を示す。
個別領域の簡素化（補間・モジュール・診断・型付きHIR等）は各設計文書に分離し、本文書は配置と依存規律のみを扱う。

## 現状の実測

| ディレクトリ | 行数 | 内容 |
|---|---|---|
| src/internal/codegen | 48,161 | LLVM/js/sv各バックエンド+ランタイムC（native/wasm二重実装含む） |
| src/internal/mir | 30,638 | MIR定義・lowering・最適化パス・モノモーフ化 |
| src/internal/syntax | 9,586 | lexer/parser/AST |
| src/internal/types | 8,938 | 型検査（Lint検査も enable_lint_warnings_ フラグで同居） |
| src/internal/hir | 6,531 | HIR定義とAST→HIR lowering |
| src/internal/base | 4,464 | span/診断/i18n/デバッグ（基盤だが他層への逆依存なしの保証がない） |
| src/internal/preprocessor | 4,241 | importテキスト展開（module-system-structural-imports.mdで別途扱う） |
| src/internal/fmt | 1,391 | フォーマッタ（54KBの単一formatter.cpp） |
| src/internal/lint | 530 | Lint固有部（大半のLintはtypes/checking内に実装） |
| src/internal/diagnostics | 682 | 診断カタログ（発行機構は各層に分散） |

問題の中心は行数でなく依存の無規律で、(1) fmtやcheckが理論上不要な層（MIR・codegen）と同一ビルド単位でリンクされる、(2) LintがTypeCheckerのフラグ分岐として実装されサブ機能として取り出せない、(3) ドライバがステージ配線と診断表示を複製している（X5修正でbuild.cppとcheck.cppに同一のsource_map適用コードを2度書いた実績）。

## 参照: rustcのクレート分割との対応

| rustc | 役割 | Cmの対応（現状 → 提案） |
|---|---|---|
| rustc_span / rustc_errors | 位置情報・診断発行 | base/に断片 → `cm_span` + `cm_diag`（最下層、全層から利用） |
| rustc_lexer / rustc_parse / rustc_ast | 字句・構文・AST | syntax/ → `cm_syntax`（ASTを`cm_ast`として分離可） |
| rustc_expand | マクロ展開 | macro/ → `cm_macro`（syntaxのみに依存） |
| rustc_resolve | 名前解決 | preprocessor+checkerに分散 → `cm_resolve`（構造化importとともに新設） |
| rustc_hir / rustc_hir_analysis | HIR・型検査 | hir/ + types/ → `cm_hir` + `cm_typeck` |
| rustc_lint | Lint | types/内のフラグ分岐 → `cm_lint`（型付きHIRを入力とする独立visitor群） |
| rustc_middle / rustc_mir_transform | MIR・最適化 | mir/ → `cm_mir` |
| rustc_codegen_ssa / rustc_codegen_llvm | コード生成 | codegen/ → `cm_codegen_llvm` / `cm_codegen_js` / `cm_codegen_sv` |
| rustfmt | フォーマッタ | fmt/ → `cm_fmt`（syntaxのみに依存、typeck以降とリンクしない） |
| rustc_driver / rustc_session | セッション・サブコマンド配線 | cmd/cm各所に手書き → `cm_driver`（Sessionオブジェクト+ステージ実行API） |

rustcのクエリシステム（オンデマンド計算・インクリメンタル）は規模に対して過剰なため非目標とし、「ステージ境界の成果物を明示する直列パイプライン」までを採用する。

## 提案する構成と依存規律

```
cm_span ← cm_diag ← cm_syntax(cm_ast) ← cm_macro
                          ↑                ↑
                     cm_fmt(終端)     cm_resolve ← cm_hir ← cm_typeck ← cm_lint(終端)
                                                                ↑
                                                            cm_mir ← cm_codegen_{llvm,js,sv}(終端)
                                                                ↑
                                                            cm_driver（全ステージを配線）
```

- 依存は左から右への一方向のみとし、CMakeのターゲット分割で強制する（`cm_fmt` が `cm_typeck` のヘッダをincludeしたらビルドエラーになる状態を作る）。
- サブコマンドは停止ステージの宣言に還元する: fmt=構文まで、check=型検査まで、lint=型検査+Lint visitorまで、compile/run=codegenまで。
- `Session`（cm_driver）がオプション・SourceMap・DiagCtxt・ターゲット情報を保持し、build.cpp/check.cpp/fmt.cppのパイプライン手書きと診断表示の複製を廃止する。
- LintはTypeCheckerから分離し、「型付きHIRを読むvisitor群」として `cm_lint` に移す（enable_lint_warnings_ フラグ分岐の廃止）。警告IDカタログはdiagnostics/catalogへ統合する。

## 段階分割

1. 第1段（物理分割の準備）: CMakeでinternal配下をターゲット分割し、現状の依存を可視化する（コード移動なし、逆依存の棚卸しのみ）。
2. 第2段（基盤層の独立）: base/をcm_span+cm_diagへ再編し、全層の診断発行をcm_diag経由へ寄せる（diagnostics-engine-unification.mdと同時実施）。
3. 第3段（fmtの隔離）: cm_fmtをsyntaxのみに依存させ、リンク時に層違反を検出できる最初の実例にする。
4. 第4段（Lintの分離）: types/内のLint検査（W001系・命名規則・const警告等）をcm_lintのvisitorへ移設する。
5. 第5段（driver統合）: Session導入とcmd/cm配下のパイプライン手書き排除（options.cppの手書きif連鎖のテーブル化を含む）。
6. 第6段（resolve新設）: module-system-structural-imports.mdの実装と同時にcm_resolveを新設する。

各段は独立にマージ可能で、既存の全スイートが緑のまま進める（機能変更を含まない移設が原則）。

## テスト計画

- 各段で全12スイート+O0検証を完走させる（挙動不変の証明）。
- 層違反の回帰防止として、CMakeターゲットのINTERFACE include境界と、fmt/lintバイナリがcodegenシンボルを含まないことのリンク検査をCIへ追加する。
- driver統合後、check/lint/fmt/compileの診断出力（行番号・source_map適用・i18n）が同一機構経由であることをi18nスイートで検証する。
