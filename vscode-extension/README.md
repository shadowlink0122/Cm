# Cm Language Support for VSCode

Cm言語の構文ハイライトとファイルアイコンをVSCodeに追加する拡張機能です。

## 機能

- **構文ハイライト**: Cm言語のキーワード、型、関数、文字列補間などのシンタックスハイライト
- **ファイルアイコン**: `.cm`ファイルにCmアイコンを表示
- **言語設定**: ブラケットマッチング、折りたたみ、インデント支援

## 対応する構文

- キーワード: `if`, `else`, `for`, `while`, `match`, `switch`, `struct`, `enum`, `interface`, `impl`, `with`, `module` 等
- 型: `int`, `uint`, `float`, `double`, `string`, `bool`, `char`, `isize`, `usize`, `tiny`, `short`, `long` 等
- 文字列補間: `"Hello, {name}"` （ダブルクォート・バッククォート）
- プリプロセッサ: `#ifdef`, `#ifndef`, `#define`, `#endif` 等
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
pnpm install          # 依存関係インストール
pnpm run compile      # TypeScriptコンパイル
pnpm run lint         # ESLintチェック
pnpm run lint:fix     # ESLint自動修正
pnpm run format       # Prettier自動フォーマット
pnpm run format:check # Prettierフォーマットチェック
pnpm run package      # VSIXパッケージ作成
```

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

## プロジェクト構成

```
vscode-extension/
├── scripts/                 # ビルドスクリプト (TypeScript)
│   ├── update-version.ts    # バージョン同期
│   └── verify-version.ts    # バージョン検証
├── syntaxes/
│   └── cm.tmLanguage.json   # TextMate文法定義
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
