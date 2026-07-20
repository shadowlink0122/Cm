# 設計ドキュメント

Cm言語コンパイラの設計文書の一覧。実装が完了した設計文書は[アーカイブ](../archive/)の対応バージョンフォルダへ移動される。

## 言語仕様（living documents）

| ドキュメント | 内容 |
|---|---|
| [正式言語仕様（CANONICAL_SPEC）](CANONICAL_SPEC.html) | 言語の完全な仕様（実装準拠） |
| [文法定義（cm_grammar）](cm_grammar.html) | BNFベースの文法定義 |
| [バックエンド対応マトリクス](backend_support_matrix.html) | 構文・機能×バックエンドの対応可否とテスト方針 |
| [エラー処理方針](error_handling_policy.html) | 例外/Result/診断APIの使い分け方針 |

## ロードマップ

| ドキュメント | 内容 |
|---|---|
| [v1.0.0ロードマップ](roadmap_v1.0.0.html) | v1.0.0までのマイルストーン別計画 |
| [v0.16.0 SVバックエンド拡充（アーカイブ）](../archive/v0.16.0/roadmap.html) | v0.16.0開発時のロードマップ（Verilogギャップ分析・tcl/cst統合検討） |

## v0.17.0 設計（大規模開発ボトルネック監査への対応）

| ドキュメント | 内容 |
|---|---|
| [v0.17.0 設計索引](v0.17.0/README.html) | 監査所見に対する残りの実装設計文書の一覧（型同一性・dropパス・集約コピー等） |

## アーカイブ

実装済み: [archive/v0.16.0/](../archive/v0.16.0/)（モジュールパラメータ・物理制約生成・トライステート/CDC・ビットスライス/don't careマッチ・SV回路検証フレームワーク・#[test]属性・ツーリングUX・SVコード生成監査・バックエンドギャップ解消・#[derive]属性・ユーザー定義derive前方検討・ユニオン実行時型判別・Rust準拠Result/Option・v0.16.0ロードマップ）、[archive/v0.17.0/](../archive/v0.17.0/)（JS/npm相互運用・未初期化構造体フィールドのゼロ初期化）、[archive/v0.15.1/](../archive/v0.15.1/)（SVバックエンド式ツリー化・interface動的ディスパッチ・最適化オプション等）、[archive/v0.15.0/](../archive/v0.15.0/)（SVバックエンド初期設計）ほか各バージョン。未実装・破棄提案: [archive/unimplemented/](../archive/unimplemented/)
