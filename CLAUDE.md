# Cm リポジトリ開発ルール

## 記述スタイル（ドキュメント・コメント共通）

- **全てのコメント・ドキュメントに不自然な改行を含めない**: 文の途中で手動改行（折返し）せず、1文・1箇条書き・1コメント文は1行で書く（コードブロック・表・ネストした箇条書きは除く）
- この規則はMarkdownドキュメントだけでなく、C++ソース・Cmソース内のコメント、コミットメッセージ本文にも適用する

## ファイル・ディレクトリ構成

- **ファイル名にアンダースコアで階層を埋め込まない**: `backend_run.cpp` のような `*_*` 名にするならディレクトリ構成 `backend/run.cpp` にする（新規作成・改変時に適用。既存ファイルの一括改名は不要）

## 開発ステップ（全ての機能開発で必須）

機能開発は必ず以下の5ステップを順に実行し、すべて完了してからコミットを完結させること:

1. **実装計画（docs）作成** — `docs/design/v<バージョン>/` に実装設計文書を作成（構文例・出力例・設計方針・段階分割・テスト計画を含む）
2. **実装** — テスト（ユニット + 該当バックエンドスイート + 回帰テスト追加）込み。コミット前に `make format && make lint` と `make test` を実行
3. **チュートリアル追加** — `docs/tutorials/{ja,en}/` の該当ページを更新または新設（日英両方。セクションindexへの掲載と前後ナビの整合を保つ）
4. **VSCode拡張を更新** — 新しい構文・キーワード・属性・組み込み関数を文法ソース `vscode-extension/src/grammar/`（語彙は `terms.ts`）に反映し、`npm run build:grammar` で `syntaxes/cm.tmLanguage.json` を再生成する（生成JSONは手編集しない）
5. **リリースノート作成** — `docs/releases/v<バージョン>.md` の変更履歴に追記

機能が5ステップの一部に該当しない場合（例: 純リファクタリングでチュートリアル変更なし）は、該当しない理由をコミットメッセージに明記する。

## テスト規約（3層構成）

- **unitテスト（`tests/unit/`）は単一ビルドオブジェクト（特定の.cpp/.hpp）の単体検証**を対象とする（例: lexerのトークン列、エラー型、MIR最適化パスを手組みMIRで検証）。コンパイルパイプラインを通さず、Cmプログラムも使わない
- **regressionテスト（`tests/regression/`）はプロセス内でコンパイルパイプラインの段階を通すgtest回帰**（HIR/MIR lowering・最適化パイプライン・コード生成・フォーマッタ等）。Cmプログラムは `tests/regression/cases/<対象>/` の .cm ファイルに分割する（gtestからは case ディレクトリのマクロ経由で読み込む）
- **integrationテストはリリースビルドされたcmバイナリに対する機能テスト**（`tests/common` 等を unified_test_runner.sh で実行するバックエンドスイート。`make test-interpreter/-llvm/-llvm-wasm/-js/-sv` 系）
- 実行: `make test-unit` / `make test-regression`（ctestラベル `unit` / `regression` で分離）/ バックエンドスイート各種

### integrationテストのファイル配置規約

- **カテゴリ内はサブフォルダで自由に細分化してよい**（階層の深さは問わない）。unified_test_runnerはカテゴリ配下を再帰探索するため、`tests/common/<category>/<subcat>/.../foo.cm` のように何段ネストしても実行される
- **テスト本体は伴走ファイル（`.cm`と同basenameの `.expect`/`.error`/`.skip`/`.timeout`/`.expect.<backend>` 等）を持つ`.cm`**。期待値は必ず`.cm`と同じ場所にセットで置く（別置きの`expects/`フォルダ等は作らない）
- **importされるヘルパーモジュール**（他テストから`import`される`.cm`）は伴走ファイルを持たせない。ランナーは伴走ファイルの無い`.cm`をテストとして実行しない（ヘルパーと本体をこの規約で区別する）
- **ファイル名にはサブフォルダ名を重複させない**（フォルダでカテゴリ分けする前提。例: `enum/field.cm`であって`enum/enum_field.cm`にしない）
- 相対importでローカルのフィクスチャ`.cm`に依存するテスト（`modules`・`uefi/uefi_compile/uefi_cross_module_call`等）は、本体とフィクスチャの相対位置が変わらないよう、安易にサブフォルダへ移動しない
- **SVバックエンド（`tests/sv`）のテストはファイル名がそのままSVモジュール識別子になる**ため、ファイル名の単語区切りはハイフンではなくアンダースコアを使う（`nested-if.cm`は`module nested-if`という不正な識別子を生成する。`nested_if.cm`とする）。SV予約語（`priority`等）を単独名にしない

## バージョン運用

- ブランチ名 `feature/vX.Y.Z` と `VERSION` ファイルは一致させる（CIが検証）
- バージョン更新時は `make update-docs-version` でドキュメントバッジを追従
- リリース手順: feature → PR → mainへsquashマージ → マージコミットに `git tag vX.Y.Z`

## ドキュメント運用

- 実装が完了した設計文書は `docs/archive/v<対応バージョン>/` へ移動
- 未実装のまま破棄した提案は `docs/archive/unimplemented/` へ移動
- リンクを張った場合は壊れリンクゼロを維持する（移動時は参照元も更新）
