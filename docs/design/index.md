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

## v0.17.2 設計（実装済み・アーカイブ済み）

| ドキュメント | 内容 |
|---|---|
| [セルフホスティング向け標準ライブラリ整備計画](../archive/v0.17.2/selfhosting-stdlib.html) | コンパイラ実装に必要なstdの棚卸しとArena・文字分類・parse・Set等の実装計画（実装完了） |
| [スマートポインタ](../archive/v0.17.2/smart-pointers.html) | UniquePtr/SharedPtr（std::mem::smart）のRAII設計・move/clone所有規律・実測に基づく制約（実装完了） |
| [バグ修正: ジェネリック特殊化とメソッドレシーバ](../archive/v0.17.2/bugfix-generic-mir.html) | 標準ライブラリ実装で顕在化した3件のコンパイラバグ（フィールドレシーバのコピー・関数ポインタ引数の未置換・スライス要素幅ずれ）の記録（修正完了） |
| [バグ修正: HashSetの二重解放](../archive/v0.17.2/bugfix-hashset-double-free.html) | dtor持ち構造体の暗黙コピーによる非決定的クラッシュの真因と修正・言語側の設計課題の記録（修正完了） |
| [バグ修正: ユーザ定義Optionの衝突波及](../archive/v0.17.2/bugfix-option-namespace-collision.html) | 選択importの即時型検査とプレリュードOptionのフラット上書きの合成問題の記録（ライブラリ構成で回避） |
| [バグ修正: ポインタ経由参照のみの構造体のDCE誤削除](../archive/v0.17.2/bugfix-aot-struct-dce.html) | AOT限定で不正IRになるプログラムDCEの使用中struct収集漏れの記録（修正完了） |
| [バグ記録: JSのスライスポインタ引数未対応](../archive/v0.17.2/bugfix-js-slice-pointer-arg.html) | K[]*引数の再帰渡しがJSバックエンドで壊れる制限の記録（ライブラリ側は反復走査で回避・バックエンド側は残課題） |

## v0.17.1 設計（実装済み・アーカイブ済み）

| ドキュメント | 内容 |
|---|---|
| [ネスト型宣言](../archive/v0.17.1/nested-type-declarations.html) | struct/enum本体内のstruct/enum宣言と `Outer::Inner` / `Outer::Inner::MEM` 修飾アクセスチェーンの設計（実装完了） |

## v0.17.0 設計（全件完遂・アーカイブ済み）

| ドキュメント | 内容 |
|---|---|
| [v0.17.0 調査・設計索引](../archive/v0.17.0/design-README.html) | 大規模開発ボトルネック監査への対応と各調査系列の設計文書一覧（全件処置完了） |
| [v0.17.0 リファクタリング提案索引](../archive/v0.17.0/refactoring-README.html) | 提案4件（スライス実体化一本化・特殊化後正準化・エラー境界集約・大規模ソース分割）の索引（全件完遂） |

## アーカイブ

実装済み: [archive/v0.16.0/](../archive/v0.16.0/)（モジュールパラメータ・物理制約生成・トライステート/CDC・ビットスライス/don't careマッチ・SV回路検証フレームワーク・#[test]属性・ツーリングUX・SVコード生成監査・バックエンドギャップ解消・#[derive]属性・ユーザー定義derive前方検討・ユニオン実行時型判別・Rust準拠Result/Option・v0.16.0ロードマップ）、[archive/v0.17.0/](../archive/v0.17.0/)（JS/npm相互運用・未初期化構造体フィールドのゼロ初期化）、[archive/v0.15.1/](../archive/v0.15.1/)（SVバックエンド式ツリー化・interface動的ディスパッチ・最適化オプション等）、[archive/v0.15.0/](../archive/v0.15.0/)（SVバックエンド初期設計）ほか各バージョン。未実装・破棄提案: [archive/unimplemented/](../archive/unimplemented/)
