# VSCode拡張のコードナビゲーション（ホバー・定義ジャンプ・アウトライン）

**ステータス:** 実装完了（プロバイダ層はその後 [vscode-lsp-server.md](vscode-lsp-server.md) でLSPサーバ構成へ移行。シンボル抽出ロジックは本文書のまま）

## 目的

VSCode拡張は構文ハイライトと非アクティブコード表示のみを提供しており、関数・構造体などの定義をカーソルホバーで確認する手段や定義箇所へのジャンプ手段がなかった。
Cmには言語サーバ（LSP）が存在しないため、拡張単体で完結する軽量なシンボルインデックスを導入し、ホバー・定義ジャンプ・アウトライン・ワークスペースシンボル検索を提供する。

## 設計方針

- **LSPサーバは導入しない**: 別プロセスの言語サーバは起動・同期・クラッシュ回復の複雑さを持ち込むため、拡張プロセス内の正規表現ベース抽出で完結させる（コンパイラ本体への依存もなし）。
- **抽出ロジックはVSCode API非依存**: `src/navigation/symbols.ts` に純粋ロジックとして分離し、`node --test` でユニットテストする（inactiveCode.tsと同じ構成）。
- **エディタ連携は薄い橋渡しのみ**: `src/navigation/providers.ts` がインデックス維持とHover/Definition/DocumentSymbol/WorkspaceSymbolの各プロバイダ登録を担う。

## シンボル抽出（symbols.ts）

コメント・文字列リテラルの中身を長さ維持で空白化（sanitize）した行に対し、波括弧の深さとコンテキスト（struct/enum/interface/impl/use）をスタックで追跡しながら宣言を正規表現でマッチする。

抽出対象:

| 種別 | 例 | 所属（container） |
|------|-----|------------------|
| struct/enum/interface/union | `export struct HashMap<K, V> {` | なし |
| typedef | `typedef Number = int \| double;` | なし |
| トップレベル関数 | `export <T: Eq> void assert_eq(T left, T right)` | なし |
| implメソッド・コンストラクタ・デストラクタ | `void insert(K key, V value)` / `self()` / `~self()` | implの対象型（`impl I for T` はT） |
| インターフェースメソッド | `bool has_next();` | インターフェース名 |
| structフィールド・enumバリアント | `int cap;` / `Custom(int),` | 型名 |
| macro・トップレベルconst | `macro int MAX = 1024;` | なし |
| use FFI宣言 | `void* malloc(int size);` | ライブラリ名（libc等） |
| module宣言 | `module std.collections.hashmap;` | なし |

各シンボルは複数行宣言を1行へ連結したシグネチャと、直前の連続コメント（`///` / `//`。`====`等の区切り線は除外、`#[...]`属性行は透過）をドキュメントとして保持する。

## インデックスとプロバイダ（providers.ts）

- 初回参照時にワークスペースの `**/*.cm`（node_modules/.tmp/.git/build/out除外）を一括走査し、以後はFileSystemWatcherと編集イベント（500msデバウンス）でファイル単位に差分更新する。
- 実測: リポジトリ全体1,107ファイル・4,458シンボルの全量抽出が約200msで完了するため、エディタ操作への影響はない。
- **ホバー**: カーソル位置の識別子を名前一致で検索し、シグネチャ（cmハイライト付きコードブロック）・ドキュメント・定義位置を最大3件表示する。キーワード・プリミティブ型は対象外。
- **定義ジャンプ**: 同じ検索結果をLocation配列で返す（複数定義はVSCode標準のピーク表示に委ねる）。
- **アウトライン/ワークスペースシンボル**: 同じ抽出結果をDocumentSymbol/SymbolInformationへ変換して提供する。
- フィールド・バリアントは同名の型・関数が存在する場合は候補から除外し、ノイズを抑える（rankMatches）。

## 制約（将来課題）

- 名前ベースの解決であり型解決は行わないため、同名メソッドが複数の型にある場合は全候補を提示する。
- ワークスペース外のファイルはインデックスしないため、インストール済みcmのlibs同梱標準ライブラリへはジャンプできない（Cmリポジトリを開いている場合はlibs/がワークスペース内のため解決される）。
- 参照検索（Find All References）・リネームは未対応。必要になった時点でLSP化を含めて再検討する。

## テスト

`src/test/symbols.test.ts`（node:test）で宣言種別ごとの抽出・コンテキスト所属・複数行シグネチャ連結・ドキュメント抽出・コメント/文字列の無害化・誤検出防止（制御構文・関数本体ローカル）を検証する。
