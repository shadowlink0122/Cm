---
title: v0.17.0 Design
nav_order: -3
has_children: true
---

# v0.17.0 設計文書

大規模開発ボトルネック監査（[large-scale-bottleneck-audit.md](../../archive/v0.17.0/large-scale-bottleneck-audit.md)、全57所見対応完了・archiveへ移動）で検出した所見に対する実装設計文書の索引。局所修正で対応済みの所見はリリースノート（`docs/releases/v0.17.0.md`）に記録し、残りの構造的リファクタリング項目を項目ごとの設計文書としてまとめている。

## 対応済み（リリースノート参照）

C1・C2・C3・C4・C5・C6・C7・C8・C9・C10・C11・C12(文字列・スライス一時)・C13・C14(Phase1)・C15・C16・M2・M6・M7・H1・H2・H3・H4・H8・H11・H13・H15・L8・M4・M5・M1・M8・M9・M11・M15・M16・M17・M18・L1・L2・L3・L6(assert_eq)、およびスライス要素型ディスパッチの一元化は実装・テスト済み。実装完了した設計文書は [archive/v0.17.0/](../../archive/v0.17.0/) へ移動済み（H4: uninitialized-struct-fields、C7/C8/C9: type-identity-recursive-keys、C16: mangling-collision-detection、H15/L8: generic-instantiation-diagnostics、M8/M9: numeric-output-and-cast-consistency、H8/M17: collections-option-api-and-errors、M1: bounds-checking-policy、C6: closures-multi-capture、H1/H2: interface-values-in-aggregates）。

## 残りの設計文書（テーマ別）

### 構造的リファクタリング（第1〜3段実装済み・第4段のみ未実装）
- [型解決とチェーンloweringの単純化](type-resolution-simplification.md) — B/N/V/W/X系バグの原因分析に基づく重複実装の統合。第1段（lower_place一本化）・第2段（スライスディスパッチ表化）・第3段（期待型伝播のinfer_type_expecting正式API化）は実装済み。第4段（補間ミニパイプラインのパース時脱糖、削除見込み2,500行超）が未実装で残る

### 簡素化提案（全体設計レビュー第2弾、未実装）

type-resolution-simplification.mdの4領域に続き、コンパイラが複雑なことをしている箇所の網羅調査（実測: ソース行数・重複実装数・バグ修正履歴との紐付け）から、簡素化可能な8領域を機能単位の設計文書として起票した。
筆頭のcompiler-architecture-restructure.mdはRustコンパイラのクレート分割を参照モデルに、src/internal単一ビルド単位のステージ分離とサブコマンド（check/lint/fmt）の切り分けを扱う全体再編で、他文書はその各論として独立に実施できる。

- [コンパイラ全体構成の再編](compiler-architecture-restructure.md) — rustc対応表に基づくステージ指向ライブラリ分割・依存規律のビルド強制・Lint/fmtの独立ドライバ化・Session導入（ドライバ複製の解消）
- [モジュールシステムの構造化](module-system-structural-imports.md) — importのテキストインライン展開（preprocessor 4,241行）を廃止しファイル単位パース+シンボル解決へ（source_map・テキスト改名・行番号ずれ・重複展開の根絶、名前空間形式import対応を含む）
- [診断エンジンの統一](diagnostics-engine-unification.md) — 4系統の発行機構をDiagCtxtへ集約、MIRエラーのログ→診断昇格、エラー型の下流流出（__error__*シンボル）の構造的禁止
- [型付きHIRの単一情報源化](typed-hir-single-source.md) — 「型検査後のHIRは全式が型付き」の不変条件確立と下流の型再推論禁止（B6/B7/N2/W5族の根治、対症療法コードの削除）
- [モノモーフ化の型駆動化](monomorphization-typed-instantiation.md) — 特殊化の同定・書き換えを型ノードへ統一し、マングル名の解析・逆算（82箇所）を廃止
- [ランタイムビルトインのレジストリ化](runtime-builtin-registry.md) — 名前・シグネチャの7箇所同期（LLVM宣言117分岐・js写像322件・native/wasmランタイムC二重実装8,088行）を単一表からの導出へ
- [レイアウト計算の一元化](layout-query-unification.md) — elem_size手書きスイッチ13箇所とマジック値をレイアウトAPI1箇所へ集約（wasm32幅バグ族の根絶）
- [最適化パスの共有解析基盤](optimizer-shared-analysis.md) — no_opt・グローバルクロバー等の効果モデルを一元化し、B3/W4型の「パスごとに同じガードを足す」構造を解消
- [自動実装のソース展開化](derive-as-source-expansion.md) — with/deriveの手組みMIR生成4,408行をAST合成+通常パイプラインへ置換

### native/jit網羅検証 第2・第3ラウンド（W1〜W5・X1〜X6、全件修正済み・archiveへ移動）

move・クロージャ・深いネスト・最適化・static・private・複合pushの網羅検証で検出した11件は全件修正し、個別文書を [archive/v0.17.0/](../../archive/v0.17.0/) へ移動した。
W1/X4=無名リテラルの期待型伝播、W2=多次元スライス書き込みのsubslice降下、W3=構造体popのblob脱糖、W4=最適化パス（LICM/SCCP/folding）のグローバル・静的ガード、W5=補間チェーンの式パイプライン委譲＋再帰型補完、X1=static初期化ガード、X2=privateフィールド検査、X3=push配列リテラルのスライス実体化、X5=構文エラーのsource_map写像と予約語表示、X6=可視性仕様の確定（構造体単位）を参照。

## 状態

監査全57所見・セルフホスト準備（S1〜S9）・構文網羅検証で検出したバグ（B1〜B9・N1〜N8・V1〜V8・W1〜W5・X1〜X6）は全て実装完了し、[archive/v0.17.0/](../../archive/v0.17.0/) へ移動済み（各文書に将来課題を記録）。
型解決とチェーンloweringの単純化（type-resolution-simplification.md、第4段のみ未実装）と、簡素化提案9本（compiler-architecture-restructure.md他）が未実装の構造的リファクタリング提案として本ディレクトリに残っている。
セルフホスト本体（CmコンパイラのCm実装）は1.0以降に別設計文書で扱う。
