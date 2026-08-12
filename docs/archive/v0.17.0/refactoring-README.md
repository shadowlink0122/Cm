# v0.17.0 リファクタリング提案（全4件完遂）

v0.17.0の品質調査・修正作業から導出したリファクタリング提案の索引。
4件全ての実装が完了し、各文書は本archive配下へ移動済み（実装記録は各文書末尾）。

| 文書 | 主題 |
|---|---|
| [slice-materialization-unification.md](arrays-slices/slice-materialization-unification.md) | スライス実体化ヘルパと型パラメータ置換の一本化 |
| [specialization-canonicalization-framework.md](type-system/specialization-canonicalization-framework.md) | 特殊化後正準化パスの一般機構化と$Uユニオン正準エンコード（ユニオン型引数deriveを正常系へ昇格） |
| [cli-error-boundary-consolidation.md](diagnostics/cli-error-boundary-consolidation.md) | フェーズ例外境界のrun_protected集約とMIR文脈付与（ユーザー起因系は既存バックエンド境界で満足と精査記録） |
| [large-source-splitting.md](architecture/large-source-splitting.md) | 巨大ソース・巨大関数の責務分割とファイル配置規約の全体適用（graph.cppのみモジュールグラフAST化側へ委譲） |
