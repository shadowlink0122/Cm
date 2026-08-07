# Cm Language Support for VSCode

Cm言語の構文ハイライト・コードナビゲーション・ファイルアイコンをVSCodeに追加する拡張機能です。

## 機能

- **構文ハイライト**: Cm言語のキーワード、型、関数、文字列補間などのシンタックスハイライト
- **ホバー表示**: 関数・構造体・enum・メソッド等にカーソルを合わせるとシグネチャとドキュメントコメントを表示
- **組み込みメソッドのホバー**: 配列/スライスの`map`/`filter`/`reduce`、文字列の`substring`/`split`、`Option`/`Result`の`unwrap`等、コンパイラが提供するデフォルトメソッドのシグネチャと説明を表示（ソース定義を持たないためコードジャンプは対象外）
- **定義ジャンプ**: F12/Cmd+クリックで定義箇所へ移動（アウトライン表示・ワークスペースシンボル検索にも対応）
- **ファイルアイコン**: `.cm`ファイルにCmアイコンを表示
- **言語設定**: ブラケットマッチング、折りたたみ、インデント支援
- **非アクティブコードのトーンダウン**: 未定義シンボルの`#ifdef`等で無効なスコープを薄く表示

ホバー・定義ジャンプは言語サーバ（LSP）ではなく拡張内の正規表現ベースの軽量シンボルインデックスで動作します（ワークスペースの`.cm`ファイルを初回走査し、以後は差分更新）。

## 対応する構文

- キーワード: `if`, `else`, `for`, `while`, `match`, `switch`, `struct`, `enum`, `interface`, `impl`, `with`, `module` 等
- 型: `int`, `uint`, `float`, `double`, `string`, `bool`, `char`, `isize`, `usize`, `tiny`, `short`, `long` 等
- 文字列補間: `"Hello, {name}"` （ダブルクォート・バッククォート）
- プリプロセッサ: `#ifdef`, `#ifndef`, `#define`, `#end` 等
- プラットフォームディレクティブ: `//! platform: js`
- インラインアセンブリ: `__asm__ { ... }`
- FFI宣言: `use js { ... }`, `use "package" { ... }`
- 非同期: `async`, `await`

## インストール

### VSIXファイルからインストール

```bash
cd vscode-extension
pnpm install
pnpm run package
code --install-extension cm-language-*.vsix
```

> **必須**: Node.js v20+、pnpm（またはnpm）

### 開発モードで実行

1. VSCodeでこのディレクトリを開く
2. `F5` を押してExtension Development Hostを起動
3. `.cm` ファイルを開いて構文ハイライトを確認

## 開発

### 技術スタック

| 項目 | ツール |
|------|--------|
| 言語 | TypeScript (strict mode) |
| Lint | ESLint v9+ (Flat Config) + typescript-eslint |
| フォーマット | Prettier |
| パッケージ | vsce |

### コマンド一覧

```bash
pnpm install            # 依存関係インストール
pnpm run compile        # TypeScriptコンパイル
pnpm run build:grammar  # 文法ソース(src/grammar/)からsyntaxes/cm.tmLanguage.jsonを再生成
pnpm run verify:grammar # 文法JSONがソースと一致しているか検証（CIと同じチェック）
pnpm test               # ユニットテスト（文法の構造検証 + 非アクティブコード判定）
pnpm run lint           # ESLintチェック
pnpm run lint:fix       # ESLint自動修正
pnpm run format         # Prettier自動フォーマット
pnpm run format:check   # Prettierフォーマットチェック
pnpm run package        # VSIXパッケージ作成
```

### 構文ハイライトの保守

`syntaxes/cm.tmLanguage.json` は手で編集せず、`src/grammar/` のTypeScriptソースから生成する:

1. キーワード・型・組み込み関数などの語彙は `src/grammar/terms.ts` の配列に追加する（単一ソース）
2. パターン構造の変更は `src/grammar/repository/` の該当モジュール（directives / modules / literals / code / embedded）を編集する
3. `pnpm run build:grammar` で `syntaxes/cm.tmLanguage.json` を再生成し、生成後のJSONも一緒にコミットする

トップレベル・バッククォートブロック・文字列補間の3文脈で共通のinclude並びは `terms.ts` の `codeIncludes()` に一元化されている。生成JSONとソースの一致はCI（`verify:grammar`）で強制される。

### バージョン管理

バージョンはルートの `VERSION` ファイルから自動同期:

```bash
pnpm run update-version   # バージョンを同期
pnpm run verify-version   # バージョンの一致を確認
```

### CI

`ci.yml` の `extension-lint` ジョブで以下を自動チェック:

1. TypeScript compile (`tsc`)
2. ESLint check (`eslint .`)
3. Prettier format check (`prettier --check`)
4. Grammar sync check (`verify:grammar`)
5. Unit tests (`npm test`)

## プロジェクト構成

```
vscode-extension/
├── scripts/                 # ビルドスクリプト (TypeScript)
│   ├── update-version.ts    # バージョン同期
│   └── verify-version.ts    # バージョン検証
├── src/
│   ├── extension.ts         # 拡張本体（エディタ連携のみ）
│   ├── inactiveCode.ts      # 非アクティブコード判定（VSCode API非依存・テスト可能）
│   ├── navigation/          # コードナビゲーション
│   │   ├── symbols.ts       # シンボル抽出（VSCode API非依存・テスト可能）
│   │   ├── builtins.ts      # コンパイラ組み込みメソッド/関数のレジストリ（ホバー用・テスト可能）
│   │   └── providers.ts     # ホバー・定義ジャンプ・アウトラインのプロバイダ登録
│   ├── grammar/             # 文法の単一ソース（ここからtmLanguage.jsonを生成）
│   │   ├── terms.ts         # キーワード・型・正規表現断片の一元定義
│   │   ├── tmTypes.ts       # TextMate文法の型定義
│   │   ├── grammar.ts       # 文法の組み立て
│   │   ├── build.ts         # 生成/検証CLI
│   │   └── repository/      # repositoryを責務別に分割したモジュール群
│   └── test/                # ユニットテスト (node:test)
├── syntaxes/
│   └── cm.tmLanguage.json   # TextMate文法定義（src/grammar/から生成。手編集しない）
├── images/
│   ├── icon.png             # 拡張機能アイコン
│   └── icon.svg             # ファイルアイコン
├── eslint.config.mjs        # ESLint設定
├── .prettierrc              # Prettier設定
├── tsconfig.json            # TypeScript設定
├── package.json             # マニフェスト
├── language-configuration.json
└── iconTheme.json
```

## ライセンス

MIT
