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
2. 第2段（基盤層の独立）: base/をcm_span+cm_diagへ再編し、全層の診断発行をcm_diag経由へ寄せる（診断統一はarchive/v0.17.0/diagnostics/diagnostics-engine-unification.mdとして実装完了済み。表示・発行の一元化はDiagnosticEmitter/診断チャネルで達成されており、本段は物理的なターゲット分離のみを扱う）。
3. 第3段（fmtの隔離）: cm_fmtをsyntaxのみに依存させ、リンク時に層違反を検出できる最初の実例にする。
4. 第4段（Lintの分離）: types/内のLint検査（W001系・命名規則・const警告等）をcm_lintのvisitorへ移設する。
5. 第5段（driver統合）: Session導入とcmd/cm配下のパイプライン手書き排除（options.cppの手書きif連鎖のテーブル化を含む）。
6. 第6段（resolve新設）: module-system-structural-imports.mdの実装と同時にcm_resolveを新設する。

各段は独立にマージ可能で、既存の全スイートが緑のまま進める（機能変更を含まない移設が原則）。

## テスト計画

- 各段で全12スイート+O0検証を完走させる（挙動不変の証明）。
- 層違反の回帰防止として、CMakeターゲットのINTERFACE include境界と、fmt/lintバイナリがcodegenシンボルを含まないことのリンク検査をCIへ追加する。
- driver統合後、check/lint/fmt/compileの診断出力（行番号・source_map適用・i18n）が同一機構経由であることをi18nスイートで検証する。

## 進捗

### 第1段（依存の棚卸しと規律の強制）: 実装済み

- src/internal全12層のinclude依存を実測した結果、依存は想定よりはるかに規律的で、逆依存はmir→preprocessorの1辺のみだった（base最下層・一方向・循環なし。fmtはテキストベース整形のためsyntaxにも依存せずbaseのみ）。
- 唯一の層違反mir→preprocessor（MIR loweringがソースファイル解決に使うModuleRange型のinclude）は、ModuleRangeを最下層の共有型 `src/internal/base/module_range.hpp` へ移設し、preprocessor側をエイリアス化して解消した。
- 依存規律の強制は `scripts/check_layer_deps.py` で実装した。許可依存の隣接リスト（本文書の依存図に対応）に対して全includeエッジを検査し、`make lint` とCIのLintジョブで層違反・未登録層を検出する。依存を増やす変更は本文書の依存図とALLOWEDの両方の更新を要求する運用とする。
- CMakeのOBJECTライブラリ物理分割は見送った。unity build（バッチ16）と条件付きソース（LLVM有無・プラットフォーム別）の再バッチ化リスクに対し、include検査が同じ規律を先に強制できるため。物理分割は第2段以降（base再編・fmt隔離のリンク検査）で扱う。
- 第3段が目標とする「fmtの隔離」は、include水準では既に成立している（fmt→baseのみ。ALLOWEDで固定済み）。リンク水準の検査（fmtバイナリがcodegenシンボルを含まない）は物理分割後に導入する。

### 第5段（driver統合）: 実装済み

- `src/cmd/cm/frontend.hpp/.cpp` に共有フロントエンドパイプライン `run_frontend`（import展開→条件付きコンパイル→字句解析→構文解析。パラメータ: defines/target/test_mode/H7警告/デバッグ/ダンプ）を新設し、build.cpp/check.cppのステージ配線複製（各約120行）を排除した。両ドライバはパラメータを渡して結果（プリプロセス後ソース・source_map・AST・パーサ診断・フェーズ計測）を消費するだけになった。
- 内部例外のステージ帰属（preprocess/parse）・プリプロセス失敗・構文エラーは結果構造体で判別し、表示は各ドライバがDiagnosticEmitterで行う（診断統一第1段と接続）。
- options.cppの手書きif連鎖は、真偽フラグ19個（kBoolFlags: 名前・別名・設定先メンバの表）とサブコマンド判定（kCommands）をテーブル化した。値付き（-D/-o）・検証付き（--sanitize=/--funroll-loops=/-O）・副作用付き（--debug/-d=/--lang=）は性質が非一様なため明示分岐として残し、真偽フラグの追加は表1行で完結する。

## 解決記録（最終処置）

各段の処置を確定し、本文書はarchiveへ移動する。

- 第1段（依存規律）: 実装済み。include依存の実測とcheck_layer_deps.pyによるlint/CI強制。
- 第2段（基盤層の独立）: 論理面は達成済みで確定。baseは実測で最下層（依存ゼロ）であり、診断発行・表示はdiag_emitter/診断チャネル（診断統一 全4段完遂）で一元化済み。CMakeターゲットの物理分離はunity build再バッチ化のコストに対しinclude水準の強制で同じ規律が得られるため不採用（層違反はlintで検出される）。
- 第3段（fmt隔離）: include水準で達成済み（fmt→baseのみ。ALLOWEDで固定）。リンク水準の検査は物理分離を行わないため対象外。
- 第4段（Lint分離）: 不採用（設計判断）。Lint検査（W001未使用・const推奨・H6確定代入/return網羅）は型検査走査中に構築されるフロー状態（使用マーク・変更マーク・分岐fork/joinの初期化集合）を消費しており、独立visitor化はこれらの解析基盤の再実装を要する（利得は分離の形式のみでコストと退行リスクが大きい）。フラグ分岐は7箇所と小さく、警告ID（W001/L001）はdiagnostics/catalogへ収容済み、レベル設定・行無効化はlint config側で機能している。
- 第5段（driver統合）: 実装済み。run_frontend共有パイプライン（配線複製約240行排除）とoptionsテーブル化。
- 第6段（resolve新設）: module-system-structural-imports.mdへ移譲。構造化importの実装と不可分のため、当該文書の実装時にcm_resolve相当を新設する。

将来課題: モジュールシステム構造化後のSpan座標系整理と合わせたcodegen診断の構造化、物理ターゲット分離の再評価（unity build構成の見直し時）。
