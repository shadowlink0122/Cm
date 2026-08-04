---
title: v0.17.0 Design
nav_order: -3
has_children: true
---

# v0.17.0 設計文書

大規模開発ボトルネック監査（[large-scale-bottleneck-audit.md](../../archive/v0.17.0/large-scale-bottleneck-audit.md)、全57所見対応完了・archiveへ移動）で検出した所見に対する実装設計文書の索引。局所修正で対応済みの所見はリリースノート（`docs/releases/v0.17.0.md`）に記録し、残りの構造的リファクタリング項目を項目ごとの設計文書としてまとめている。

## 対応済み（リリースノート参照）

C1・C2・C3・C4・C5・C6・C7・C8・C9・C10・C11・C12(文字列・スライス一時)・C13・C14(Phase1)・C15・C16・M2・M6・M7・H1・H2・H3・H4・H8・H11・H13・H15・L8・M4・M5・M1・M8・M9・M11・M15・M16・M17・M18・L1・L2・L3・L6(assert_eq)、およびスライス要素型ディスパッチの一元化は実装・テスト済み。実装完了した設計文書は [archive/v0.17.0/](../../archive/v0.17.0/) へ移動済み（H4: uninitialized-struct-fields、C7/C8/C9: type-identity-recursive-keys、C16: mangling-collision-detection、H15/L8: generic-instantiation-diagnostics、M8/M9: numeric-output-and-cast-consistency、H8/M17: collections-option-api-and-errors、M1: bounds-checking-policy、C6: closures-multi-capture、H1/H2: interface-values-in-aggregates）。

## 簡素化提案（全体設計レビュー第2弾、全件処置完了）

type-resolution-simplification.mdの4領域に続き、コンパイラが複雑なことをしている箇所の網羅調査（実測: ソース行数・重複実装数・バグ修正履歴との紐付け）から、簡素化可能な8領域を機能単位の設計文書として起票した。
全体再編のcompiler-architecture-restructure.mdは全段の処置を確定し [archive/v0.17.0/compiler-architecture-restructure.md](../../archive/v0.17.0/compiler-architecture-restructure.md) へ移動した（依存規律のlint/CI強制・fmt隔離・run_frontend共有化・optionsテーブル化を実装、物理分離とLint分離は実測に基づく不採用判断、resolve新設はmodule-system-structural-imports.mdへ移譲）。


モジュールシステムの構造化は全4段（モジュールグラフ・AST駆動の選択的包含・可視性の診断昇格・既定切替とテキスト展開系約2,800行の削除）を完了し [archive/v0.17.0/module-system-structural-imports.md](../../archive/v0.17.0/module-system-structural-imports.md) へ移動した。型付きHIRの単一情報源化は全段の処置を確定し（不変条件の機械的検証と違反6クラスの上流修正・単一walk化の不採用判断） [archive/v0.17.0/typed-hir-single-source.md](../../archive/v0.17.0/typed-hir-single-source.md) へ移動した。モノモーフ化の型駆動化は全3段（特殊化要求の型キー化と構造的単一化・呼び出しサイト表書き換え・置換完了検査と逆算ヘルパ削除）を完了し [archive/v0.17.0/monomorphization-typed-instantiation.md](../../archive/v0.17.0/monomorphization-typed-instantiation.md) へ移動した。このうちランタイムビルトインのレジストリ化は全段を完了し [archive/v0.17.0/runtime-builtin-registry.md](../../archive/v0.17.0/runtime-builtin-registry.md) へ移動した（レジストリ表188件・宣言表引き化・シグネチャ検査lint/CI化・slice系ランタイム一本化。format系はアーキテクチャ差のため二重実装維持+検査防衛の設計判断を記録）。診断エンジンの統一も全4段を実装完了し [archive/v0.17.0/diagnostics-engine-unification.md](../../archive/v0.17.0/diagnostics-engine-unification.md) へ移動した（DiagnosticEmitter表示一元化・MIRエラー昇格とcodegen前停止・__error__検査・診断207呼び出しのMsgId化）。レイアウト計算の一元化と最適化パスの共有解析基盤も実装完了し、[archive/v0.17.0/layout-query-unification.md](../../archive/v0.17.0/layout-query-unification.md)（スライス格納/配列実ストライドの2意味論API・MIR/LLVM共有コア）と [archive/v0.17.0/optimizer-shared-analysis.md](../../archive/v0.17.0/optimizer-shared-analysis.md)（効果モデルeffects.hppへの8パス統合）へ移動した。

### native/jit網羅検証 第2・第3ラウンド（W1〜W5・X1〜X6、全件修正済み・archiveへ移動）

move・クロージャ・深いネスト・最適化・static・private・複合pushの網羅検証で検出した11件は全件修正し、個別文書を [archive/v0.17.0/](../../archive/v0.17.0/) へ移動した。
W1/X4=無名リテラルの期待型伝播、W2=多次元スライス書き込みのsubslice降下、W3=構造体popのblob脱糖、W4=最適化パス（LICM/SCCP/folding）のグローバル・静的ガード、W5=補間チェーンの式パイプライン委譲＋再帰型補完、X1=static初期化ガード、X2=privateフィールド検査、X3=push配列リテラルのスライス実体化、X5=構文エラーのsource_map写像と予約語表示、X6=可視性仕様の確定（構造体単位）を参照。

## 状態

監査全57所見・セルフホスト準備（S1〜S9）・構文網羅検証で検出したバグ（B1〜B9・N1〜N8・V1〜V8・W1〜W5・X1〜X6）は全て実装完了し、[archive/v0.17.0/](../../archive/v0.17.0/) へ移動済み（各文書に将来課題を記録）。
型解決とチェーンloweringの単純化は全段（lower_place一本化・スライスディスパッチ表化・期待型伝播の正式API化・補間のパース時脱糖とミニパイプライン完全削除）を完了し [archive/v0.17.0/type-resolution-simplification.md](../../archive/v0.17.0/type-resolution-simplification.md) へ移動した。簡素化提案は全件の処置が完了し、本ディレクトリに残る設計文書は無い（本READMEが索引として残る）。自動実装のソース展開化は全3段を完了し [archive/v0.17.0/derive-as-source-expansion.md](../../archive/v0.17.0/derive-as-source-expansion.md) へ移動した（非ジェネリック全トレイトの通常パイプライン化・波括弧エスケープ修正・死んだ生成器約1,700行の削除。ジェネリックの単一総称化は演算子mono対応後）。
セルフホスト本体（CmコンパイラのCm実装）は1.0以降に別設計文書で扱う。
