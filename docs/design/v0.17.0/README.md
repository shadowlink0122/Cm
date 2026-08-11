# v0.17.0 リファクタリング提案

v0.17.0の品質調査・修正作業から導出したリファクタリング提案の索引。
各文書は概要・実測・方針・段階分割・テスト計画・検出経緯で構成する。
実装が完了した文書は`docs/archive/v0.17.0/`へ移動する。

| 文書 | 主題 | 起点 |
|---|---|---|
| [cli-error-boundary-consolidation.md](cli-error-boundary-consolidation.md) | フェーズ包括tryの整理と内部エラー境界の一本化（第1段=境界集約は実装済み。第2段=ユーザー起因throwのDiagnostic化・第3段=内部エラーへのMIR文脈付与が残件） | tryの範囲整理の提案募集 |
| [large-source-splitting.md](large-source-splitting.md) | 1,500行超ファイル・700行超関数の責務分割（段階1〜3実装済み。hir/lowering/expr.cpp・llvm/native/codegen.cpp・SV convert系が残件） | 機能分割の提案募集 |
| [specialization-canonicalization-framework.md](../../archive/v0.17.0/type-system/specialization-canonicalization-framework.md) | 特殊化後正準化パスの一般機構化と$Uユニオン正準エンコード（**完遂・archive済み**。ユニオン型引数deriveを正常系へ昇格） | 総称derive特殊化SIGSEGV修正 |
| [slice-materialization-unification.md](../../archive/v0.17.0/arrays-slices/slice-materialization-unification.md) | スライス実体化ヘルパと型パラメータ置換の一本化（**完遂・archive済み**） | 同上（真因調査） |

継続中の既存提案は`docs/archive/v0.17.0/`の各設計文書の実装記録を参照（変換統一ドライバ・フラット名逆算全廃・メソッド解決一元化・HIR解決引き渡し・モジュールグラフAST化・derive整理）。
