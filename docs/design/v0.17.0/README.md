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

### 構造的リファクタリング（未実装）
- [型解決とチェーンloweringの単純化](type-resolution-simplification.md) — B/N/V/W/X系バグの原因分析に基づく重複実装の統合。補間ミニパイプラインのパース時脱糖・場所解決4系統の`lower_place`一本化・期待型伝播の正式API化・スライスディスパッチの表化（4段階、削除見込み2,500行超）

### native/jit網羅検証 第2・第3ラウンド（W1〜W5・X1〜X6、全件修正済み・archiveへ移動）

move・クロージャ・深いネスト・最適化・static・private・複合pushの網羅検証で検出した11件は全件修正し、個別文書を [archive/v0.17.0/](../../archive/v0.17.0/) へ移動した。
W1/X4=無名リテラルの期待型伝播、W2=多次元スライス書き込みのsubslice降下、W3=構造体popのblob脱糖、W4=最適化パス（LICM/SCCP/folding）のグローバル・静的ガード、W5=補間チェーンの式パイプライン委譲＋再帰型補完、X1=static初期化ガード、X2=privateフィールド検査、X3=push配列リテラルのスライス実体化、X5=構文エラーのsource_map写像と予約語表示、X6=可視性仕様の確定（構造体単位）を参照。

## 状態

監査全57所見・セルフホスト準備（S1〜S9）・構文網羅検証で検出したバグ（B1〜B9・N1〜N8・V1〜V8・W1〜W5・X1〜X6）は全て実装完了し、[archive/v0.17.0/](../../archive/v0.17.0/) へ移動済み（各文書に将来課題を記録）。
型解決とチェーンloweringの単純化（type-resolution-simplification.md）は未実装の構造的リファクタリング提案として本ディレクトリに残っている。
セルフホスト本体（CmコンパイラのCm実装）は1.0以降に別設計文書で扱う。
