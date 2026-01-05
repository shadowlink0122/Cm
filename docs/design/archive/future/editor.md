# エディタ拡張・シンタックスハイライト

## 概要

Cm言語のエディタサポートを提供します。

## 対応エディタ

| エディタ | 拡張形式 | 状態 |
|---------|---------|------|
| VSCode | `.vsix` | 📋 予定 |
| Vim/Neovim | `.vim` / Treesitter | 📋 予定 |
| Emacs | `.el` | 📋 予定 |
| JetBrains | Plugin | 📋 予定 |

---

## VSCode拡張

### ディレクトリ構造

```
tools/vscode-cm/
├── package.json           # 拡張メタ情報
├── syntaxes/
│   └── cm.tmLanguage.json # TextMate文法
├── language-configuration.json
└── README.md
```

### package.json

```json
{
  "name": "cm-language",
  "displayName": "Cm Language",
  "description": "Cm programming language support",
  "version": "0.1.0",
  "engines": { "vscode": "^1.80.0" },
  "categories": ["Programming Languages"],
  "contributes": {
    "languages": [{
      "id": "cm",
      "aliases": ["Cm", "cm"],
      "extensions": [".cm", ".cm.native", ".cm.web", ".cm.baremetal"],
      "configuration": "./language-configuration.json"
    }],
    "grammars": [{
      "language": "cm",
      "scopeName": "source.cm",
      "path": "./syntaxes/cm.tmLanguage.json"
    }]
  }
}
```

### TextMate文法 (cm.tmLanguage.json)

```json
{
  "name": "Cm",
  "scopeName": "source.cm",
  "patterns": [
    { "include": "#comments" },
    { "include": "#keywords" },
    { "include": "#types" },
    { "include": "#strings" },
    { "include": "#numbers" },
    { "include": "#attributes" }
  ],
  "repository": {
    "comments": {
      "patterns": [
        { "match": "//.*$", "name": "comment.line.cm" },
        { "begin": "/\\*", "end": "\\*/", "name": "comment.block.cm" }
      ]
    },
    "keywords": {
      "match": "\\b(if|else|for|while|match|return|break|continue|struct|interface|impl|import|export|async|await|new|delete|const|static|extern|inline|private|mutable|volatile)\\b",
      "name": "keyword.control.cm"
    },
    "types": {
      "match": "\\b(int|uint|tiny|utiny|short|ushort|long|ulong|float|double|bool|char|void|string|Option|Result|Future|shared)\\b",
      "name": "storage.type.cm"
    },
    "strings": {
      "begin": "\"",
      "end": "\"",
      "name": "string.quoted.double.cm",
      "patterns": [
        { "match": "\\\\.", "name": "constant.character.escape.cm" },
        { "match": "\\$\\{[^}]*\\}", "name": "variable.other.cm" }
      ]
    },
    "numbers": {
      "match": "\\b(0x[0-9A-Fa-f]+|0b[01]+|[0-9]+\\.?[0-9]*)\\b",
      "name": "constant.numeric.cm"
    },
    "attributes": {
      "match": "#\\[[^\\]]+\\]",
      "name": "entity.other.attribute-name.cm"
    }
  }
}
```

### language-configuration.json

```json
{
  "comments": {
    "lineComment": "//",
    "blockComment": ["/*", "*/"]
  },
  "brackets": [
    ["{", "}"],
    ["[", "]"],
    ["(", ")"],
    ["<", ">"]
  ],
  "autoClosingPairs": [
    { "open": "{", "close": "}" },
    { "open": "[", "close": "]" },
    { "open": "(", "close": ")" },
    { "open": "<", "close": ">" },
    { "open": "\"", "close": "\"" },
    { "open": "'", "close": "'" }
  ]
}
```

---

## LSP (Language Server Protocol)

### 機能

| 機能 | 説明 |
|------|------|
| 補完 | 変数・関数・型の補完 |
| 定義ジャンプ | Go to Definition |
| ホバー | 型情報表示 |
| エラー表示 | リアルタイム診断 |
| リファクタ | 名前変更 |
| フォーマット | コード整形 |

### 実装予定

```
tools/cm-lsp/
├── src/
│   ├── main.cpp
│   ├── server.cpp
│   └── analysis.cpp
└── CMakeLists.txt
```

---

## インストール

### VSCode

```bash
# 開発版
cd tools/vscode-cm
npm install
npm run package
code --install-extension cm-language-0.1.0.vsix

# 将来: マーケットプレイス
ext install cm-language
```

---

## TODO

- [ ] tools/vscode-cm/ ディレクトリ作成
- [ ] TextMate文法定義
- [ ] LSP基本実装
- [ ] 補完・定義ジャンプ
- [ ] Treesitter文法（Neovim用）
