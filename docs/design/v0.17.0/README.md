# v0.17.0 リファクタリング提案

v0.17.0の品質調査・修正作業から導出したリファクタリング提案の索引。
各文書は概要・実測・方針・段階分割・テスト計画・検出経緯で構成する。
実装が完了した文書は`docs/archive/v0.17.0/`へ移動する。

| 文書 | 主題 | 起点 |
|---|---|---|
| [cli-error-boundary-consolidation.md](cli-error-boundary-consolidation.md) | フェーズ包括tryの整理と内部エラー境界の一本化（診断/throw二重系の解消） | tryの範囲整理の提案募集 |
| [large-source-splitting.md](large-source-splitting.md) | 1,500行超ファイル・700行超関数の責務分割（SV→checker/fmt→lower系の順） | 機能分割の提案募集 |
| [specialization-canonicalization-framework.md](specialization-canonicalization-framework.md) | 特殊化後正準化パスの一般機構化とtypekeyユニオン正準エンコード（ユニオン型引数derive解禁の前提） | 総称derive特殊化SIGSEGV修正 |
| [slice-materialization-unification.md](slice-materialization-unification.md) | スライス実体化ヘルパと型パラメータ置換の一本化（8箇所の複製解消） | 同上（真因調査） |

継続中の既存提案は`docs/archive/v0.17.0/`の各設計文書の実装記録を参照（変換統一ドライバ・フラット名逆算全廃・メソッド解決一元化・HIR解決引き渡し・モジュールグラフAST化・derive整理）。
