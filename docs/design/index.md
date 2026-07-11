# 設計ドキュメント

Cm言語コンパイラの設計文書の一覧。実装が完了した設計文書は[アーカイブ](../archive/)の対応バージョンフォルダへ移動される。

## 言語仕様（living documents）

| ドキュメント | 内容 |
|---|---|
| [正式言語仕様（CANONICAL_SPEC）](CANONICAL_SPEC.html) | 言語の完全な仕様（実装準拠） |
| [文法定義（cm_grammar）](cm_grammar.html) | BNFベースの文法定義 |
| [エラー処理方針](error_handling_policy.html) | 例外/Result/診断APIの使い分け方針 |

## ロードマップ

| ドキュメント | 内容 |
|---|---|
| [v1.0.0ロードマップ](roadmap_v1.0.0.html) | v1.0.0までのマイルストーン別計画 |
| [v0.16.0 SVバックエンド拡充](v0.16.0/roadmap.html) | 現行開発版のロードマップ（Verilogギャップ分析・tcl/cst統合検討） |

## v0.16.0 実装設計

| ドキュメント | 内容 |
|---|---|
| [01 モジュールパラメータ](v0.16.0/01_module_parameters.html) | #(parameter) と階層Stage 3 |
| [02 物理制約ファイル生成](v0.16.0/02_constraints_emission.html) | #[sv::pin] / --emit-constraints（.cst/.tcl） |
| [03 トライステートとCDC](v0.16.0/03_tristate_cdc.html) | 'z リテラル・#[sv::sync] |
| [04 ビットスライスとcasez](v0.16.0/04_bitslice_casez.html) | [hi:lo] / [base +: W] 構文・unique casez |

## アーカイブ

実装済み: [archive/v0.15.1/](../archive/v0.15.1/)（SVバックエンド式ツリー化・interface動的ディスパッチ・最適化オプション等）、[archive/v0.15.0/](../archive/v0.15.0/)（SVバックエンド初期設計）ほか各バージョン。未実装・破棄提案: [archive/unimplemented/](../archive/unimplemented/)
