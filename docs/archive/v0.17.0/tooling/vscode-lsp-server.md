# VSCode拡張のLSPサーバ化（ホバー・定義ジャンプ・シンボル一覧）

**ステータス:** 実装完了

## 目的

コードナビゲーション（[vscode-code-navigation.md](vscode-code-navigation.md)）は拡張プロセス内のプロバイダ登録で実装していたが、実行時依存モジュールが `.vscodeignore` の除外に巻き込まれて `activate()` がMODULE_NOT_FOUNDで停止し、インストール済み拡張でホバー・定義ジャンプが一切効かなくなる不具合が発生した。
この「パッケージ構成の破れが機能全滅として現れる」クラスの不具合を構造的に防ぐため、ナビゲーション機能を標準のLanguage Server Protocol構成へ移行し、配布物をesbuildで自己完結バンドル化する。

## 設計方針

- **シンボル抽出ロジックは移設のみ**: 正規表現ベースの抽出（`src/navigation/symbols.ts`）・組み込み関数定義（`builtins.ts`）は変更せず、LSPサーバから再利用する。判定挙動は従来と同一。
- **構文ハイライトは現状維持**: TextMate文法（`syntaxes/cm.tmLanguage.json`）はLSP化の対象外で、宣言的コントリビューションのまま変更しない。
- **クライアントは薄く**: `src/extension.ts` はLanguageClientの起動・停止と、LSPに対応概念が無い非アクティブコード減光デコレーションのみを担う。
- **配布は自己完結バンドル2本**: esbuildでクライアント（`dist/extension.js`）とサーバ（`dist/server/main.js`）を各1ファイルへバンドルし、vsixにnode_modulesを同梱しない。外部requireは`vscode`（クライアントのみ）とNode組み込みに限定され、除外漏れによるMODULE_NOT_FOUNDが構造的に発生しない。

## 構成

| ファイル | 役割 |
|---------|------|
| `src/extension.ts` | クライアント: LanguageClient起動（`TransportKind.ipc`）・`**/*.cm`ファイルウォッチ中継・減光デコレーション |
| `src/server/main.ts` | LSPサーバ: initialize・ドキュメント同期・hover/definition/documentSymbol/workspaceSymbol応答 |
| `src/server/indexer.ts` | ワークスペース全.cmのfsベース走査インデックス（初回一括・以後イベント差分、上限20,000ファイル） |
| `src/server/words.ts` | カーソル位置の識別子抽出・キーワード除外・メソッドアクセス判定（純ロジック） |
| `src/paths.ts` | バンドル配置定数（クライアント起動とパッケージングテストで共有） |

サーバ機能はhover（シグネチャ・docコメント・定義位置、最大3件）・definition（F12/Cmd+クリック/右クリック、最大20件）・documentSymbol（アウトライン）・workspaceSymbol（Cmd+T、最大500件）で従来と同一。
開いているドキュメントはdidChange（500msデバウンス）で、未オープンファイルはクライアントのファイルウォッチ通知でインデックスを差分更新する。
ホバー・定義ジャンプのクエリ時は対象ドキュメントを即時再抽出するため、デバウンス待ちなしで編集直後のシンボルへジャンプできる。

## テスト

- **E2E（`src/test/lsp.test.ts`）**: vsixに同梱される `dist/server/main.js` そのものを `--stdio` で子プロセス起動し、initialize→didOpen→hover/definition/documentSymbol/workspaceSymbolの応答をLSPプロトコル越しに検証する。クロスファイル定義・組み込み関数ホバー・キーワード無応答・didChange即時反映を含む。
- **パッケージング整合性（`src/test/packaging.test.ts`）**: 両バンドルが `@vscode/vsce` の `listFiles`（同梱判定の実装そのもの）に含まれること、バンドル内の外部requireが許容集合（`vscode`・Node組み込み）に収まることを検証する。
- **純ロジックユニット（`src/test/words.test.ts` ほか既存）**: 識別子抽出・シンボル抽出・組み込み定義の各ユニットテストは従来どおり `node --test` で実行する。

## 移行に伴う変更点

- `package.json` の `main` を `./dist/extension.js` へ変更し、`build:bundle` スクリプト（esbuild）を追加した。`npm test` はバンドル生成後にE2Eを実行する。
- `tsconfig.json` を `module: Node16` へ更新した（`vscode-languageclient/node` 等のsubpath exports解決に必要）。
- 旧 `src/navigation/providers.ts`（拡張プロセス内プロバイダ）は削除した。
- 依存追加はすべてdevDependencies（vscode-languageclient / vscode-languageserver / vscode-languageserver-textdocument / esbuild / vscode-jsonrpc）で、バンドルにより実行時のnode_modules依存はない。

## 採用しなかった選択肢

- **拡張プロセス内プロバイダの継続**: `.vscodeignore` の除外例外で当座は直せるが、依存追加のたびに同梱リストの手動整合が必要で、破れたときの症状が「機能全滅」と重い。バンドル化とE2Eで構造的に塞ぐ方を選んだ。
- **コンパイラ本体（cm）をLSPバックエンドに使う**: 型解決に基づく正確なナビゲーションが得られるが、コンパイラへのLSPモード実装が必要で今回のスコープを超える。サーバプロセス境界を今回確立したため、将来の置き換えはサーバ実装の差し替えで済む。
