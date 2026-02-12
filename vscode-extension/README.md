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
npm run package
code --install-extension cm-language-*.vsix
```

### 開発モードで実行

1. VSCodeでこのディレクトリを開く
2. `F5` を押してExtension Development Hostを起動
3. `.cm` ファイルを開いて構文ハイライトを確認

## バージョン管理

バージョンはルートの `cm_config.json` から自動同期:

```bash
npm run update-version   # バージョンを同期
npm run verify-version   # バージョンの一致を確認
```

## ライセンス

MIT
