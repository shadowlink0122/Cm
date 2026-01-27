# Antigravity Extension for Cm Language
**Date**: 2026-01-15
**Version**: v0.12.0

## 概要

Antigravity IDE向けのCm言語拡張機能。LSP統合、高度なコード解析、デバッグ機能、インテリジェントな開発支援を提供します。

## プロジェクト構造

```
antigravity-cm/
├── manifest.json              # 拡張機能マニフェスト
├── package.json              # Node.js依存関係
├── README.md
├── LICENSE
├── src/
│   ├── extension.js          # メインエントリポイント
│   ├── lsp/
│   │   ├── client.js         # LSPクライアント
│   │   ├── protocol.js       # プロトコル定義
│   │   └── features/
│   │       ├── completion.js # 補完
│   │       ├── definition.js # 定義ジャンプ
│   │       ├── hover.js      # ホバー
│   │       ├── references.js # 参照検索
│   │       └── symbols.js    # シンボル
│   ├── debug/
│   │   └── adapter.js        # デバッグアダプター
│   ├── ui/
│   │   ├── panels.js         # UIパネル
│   │   ├── statusbar.js      # ステータスバー
│   │   └── quickpick.js      # クイックピック
│   └── utils/
│       ├── config.js         # 設定管理
│       └── logger.js         # ロギング
├── syntaxes/
│   └── cm.ag-syntax          # Antigravity文法定義
├── themes/
│   ├── cm-dark.ag-theme      # ダークテーマ
│   └── cm-light.ag-theme     # ライトテーマ
├── snippets/
│   └── cm.ag-snippets        # コードスニペット
├── icons/
│   ├── cm-icon.svg          # Cmアイコン
│   └── file-icons/          # ファイルアイコン
└── test/
    └── suite/
```

## manifest.json - Antigravity拡張マニフェスト

```json
{
  "id": "cm-language",
  "name": "Cm Language Support",
  "version": "0.12.0",
  "publisher": "cm-lang",
  "description": "Complete Cm language support with LSP integration",
  "icon": "icons/cm-icon.svg",
  "categories": ["Programming Languages", "Debuggers", "Linters"],
  "engines": {
    "antigravity": "^2.0.0"
  },
  "main": "./src/extension.js",
  "activationEvents": [
    "onLanguage:cm",
    "onWorkspaceContains:**/*.cm",
    "onWorkspaceContains:gen.toml"
  ],
  "contributes": {
    "languages": [{
      "id": "cm",
      "aliases": ["Cm", "cm"],
      "extensions": [".cm"],
      "configuration": "./language-config.json",
      "icon": {
        "light": "./icons/cm-icon.svg",
        "dark": "./icons/cm-icon.svg"
      }
    }],
    "grammars": [{
      "language": "cm",
      "scopeName": "source.cm",
      "path": "./syntaxes/cm.ag-syntax"
    }],
    "configuration": {
      "title": "Cm",
      "properties": {
        "cm.lsp.enabled": {
          "type": "boolean",
          "default": true,
          "description": "Enable Language Server Protocol"
        },
        "cm.lsp.serverPath": {
          "type": "string",
          "default": "cm-lsp",
          "description": "Path to cm-lsp server"
        },
        "cm.lsp.trace": {
          "type": "string",
          "enum": ["off", "messages", "verbose"],
          "default": "off",
          "description": "LSP trace level"
        },
        "cm.compiler.path": {
          "type": "string",
          "default": "cm",
          "description": "Path to Cm compiler"
        },
        "cm.gen.path": {
          "type": "string",
          "default": "gen",
          "description": "Path to gen package manager"
        },
        "cm.format.onSave": {
          "type": "boolean",
          "default": true,
          "description": "Format on save"
        },
        "cm.lint.onType": {
          "type": "boolean",
          "default": true,
          "description": "Lint while typing"
        },
        "cm.inlayHints.enabled": {
          "type": "boolean",
          "default": true,
          "description": "Show inlay hints"
        },
        "cm.hover.showType": {
          "type": "boolean",
          "default": true,
          "description": "Show type on hover"
        },
        "cm.hover.showDocs": {
          "type": "boolean",
          "default": true,
          "description": "Show documentation on hover"
        },
        "cm.completion.autoImport": {
          "type": "boolean",
          "default": true,
          "description": "Auto-import on completion"
        },
        "cm.completion.snippets": {
          "type": "boolean",
          "default": true,
          "description": "Show snippets in completion"
        }
      }
    },
    "commands": [
      {
        "command": "cm.goToDefinition",
        "title": "Go to Definition",
        "category": "Cm"
      },
      {
        "command": "cm.goToTypeDefinition",
        "title": "Go to Type Definition",
        "category": "Cm"
      },
      {
        "command": "cm.goToImplementation",
        "title": "Go to Implementation",
        "category": "Cm"
      },
      {
        "command": "cm.findReferences",
        "title": "Find All References",
        "category": "Cm"
      },
      {
        "command": "cm.rename",
        "title": "Rename Symbol",
        "category": "Cm"
      },
      {
        "command": "cm.format",
        "title": "Format Document",
        "category": "Cm"
      },
      {
        "command": "cm.organizeImports",
        "title": "Organize Imports",
        "category": "Cm"
      },
      {
        "command": "cm.build",
        "title": "Build Project",
        "category": "Cm",
        "icon": "$(tools)"
      },
      {
        "command": "cm.run",
        "title": "Run Project",
        "category": "Cm",
        "icon": "$(play)"
      },
      {
        "command": "cm.test",
        "title": "Run Tests",
        "category": "Cm",
        "icon": "$(beaker)"
      },
      {
        "command": "cm.debug",
        "title": "Debug Project",
        "category": "Cm",
        "icon": "$(debug)"
      },
      {
        "command": "cm.genAdd",
        "title": "Add Package",
        "category": "Cm (gen)"
      },
      {
        "command": "cm.genUpdate",
        "title": "Update Packages",
        "category": "Cm (gen)"
      },
      {
        "command": "cm.genInit",
        "title": "Initialize Project",
        "category": "Cm (gen)"
      },
      {
        "command": "cm.showAST",
        "title": "Show AST",
        "category": "Cm (Advanced)"
      },
      {
        "command": "cm.showMIR",
        "title": "Show MIR",
        "category": "Cm (Advanced)"
      },
      {
        "command": "cm.showLLVM",
        "title": "Show LLVM IR",
        "category": "Cm (Advanced)"
      },
      {
        "command": "cm.expandMacro",
        "title": "Expand Macro",
        "category": "Cm (Advanced)"
      },
      {
        "command": "cm.showCallGraph",
        "title": "Show Call Graph",
        "category": "Cm (Advanced)"
      }
    ],
    "keybindings": [
      {
        "command": "cm.goToDefinition",
        "key": "F12",
        "when": "editorTextFocus && editorLangId == cm"
      },
      {
        "command": "cm.goToTypeDefinition",
        "key": "Ctrl+Shift+F12",
        "mac": "Cmd+Shift+F12",
        "when": "editorTextFocus && editorLangId == cm"
      },
      {
        "command": "cm.findReferences",
        "key": "Shift+F12",
        "when": "editorTextFocus && editorLangId == cm"
      },
      {
        "command": "cm.rename",
        "key": "F2",
        "when": "editorTextFocus && editorLangId == cm"
      },
      {
        "command": "cm.format",
        "key": "Shift+Alt+F",
        "mac": "Shift+Option+F",
        "when": "editorTextFocus && editorLangId == cm"
      },
      {
        "command": "cm.build",
        "key": "Ctrl+Shift+B",
        "mac": "Cmd+Shift+B"
      },
      {
        "command": "cm.run",
        "key": "F5"
      },
      {
        "command": "cm.debug",
        "key": "Ctrl+F5",
        "mac": "Cmd+F5"
      }
    ],
    "menus": {
      "editor/context": [
        {
          "command": "cm.goToDefinition",
          "when": "editorLangId == cm",
          "group": "navigation@1"
        },
        {
          "command": "cm.findReferences",
          "when": "editorLangId == cm",
          "group": "navigation@2"
        },
        {
          "command": "cm.rename",
          "when": "editorLangId == cm",
          "group": "1_modification@1"
        },
        {
          "command": "cm.format",
          "when": "editorLangId == cm",
          "group": "1_modification@2"
        }
      ],
      "editor/title": [
        {
          "command": "cm.run",
          "when": "editorLangId == cm",
          "group": "navigation"
        },
        {
          "command": "cm.build",
          "when": "editorLangId == cm",
          "group": "navigation"
        }
      ]
    },
    "snippets": [{
      "language": "cm",
      "path": "./snippets/cm.ag-snippets"
    }],
    "debuggers": [{
      "type": "cm",
      "label": "Cm Debug",
      "program": "./src/debug/adapter.js",
      "runtime": "node",
      "languages": ["cm"],
      "configurationAttributes": {
        "launch": {
          "required": ["program"],
          "properties": {
            "program": {
              "type": "string",
              "description": "Path to the program to debug"
            },
            "args": {
              "type": "array",
              "description": "Command line arguments"
            },
            "env": {
              "type": "object",
              "description": "Environment variables"
            }
          }
        }
      }
    }],
    "taskDefinitions": [{
      "type": "cm",
      "required": ["task"],
      "properties": {
        "task": {
          "type": "string",
          "enum": ["build", "run", "test", "clean"]
        }
      }
    }],
    "problemMatchers": [{
      "name": "cm",
      "owner": "cm",
      "fileLocation": ["relative", "${workspaceFolder}"],
      "pattern": {
        "regexp": "^(.+):(\\d+):(\\d+):\\s+(error|warning|note)\\[([A-Z]\\d{3})\\]:\\s+(.+)$",
        "file": 1,
        "line": 2,
        "column": 3,
        "severity": 4,
        "code": 5,
        "message": 6
      }
    }],
    "views": {
      "explorer": [{
        "id": "cmPackages",
        "name": "Cm Packages",
        "icon": "$(package)",
        "contextualTitle": "Cm Package Explorer"
      }]
    },
    "viewsContainers": {
      "activitybar": [{
        "id": "cm",
        "title": "Cm",
        "icon": "./icons/cm-icon.svg"
      }]
    },
    "viewsWelcome": [{
      "view": "cmPackages",
      "contents": "No Cm packages found.\n[Initialize Project](command:cm.genInit)\n[Add Package](command:cm.genAdd)"
    }]
  }
}
```

## メインエクステンション実装

```javascript
// src/extension.js - Antigravity拡張機能のエントリポイント

const antigravity = require('antigravity');
const { LanguageClient } = require('./lsp/client');
const { DebugAdapter } = require('./debug/adapter');
const { PackageExplorer } = require('./ui/packageExplorer');
const { StatusBar } = require('./ui/statusbar');
const { ConfigManager } = require('./utils/config');
const { Logger } = require('./utils/logger');

let client = null;
let debugAdapter = null;
let packageExplorer = null;
let statusBar = null;

// 拡張機能のアクティベート
async function activate(context) {
    const logger = new Logger('Cm Extension');
    logger.info('Activating Cm language extension');

    // 設定マネージャー初期化
    const config = new ConfigManager();

    // LSPクライアント起動
    if (config.get('lsp.enabled')) {
        client = new LanguageClient({
            serverPath: config.get('lsp.serverPath'),
            trace: config.get('lsp.trace'),
            initOptions: {
                inlayHints: config.get('inlayHints.enabled'),
                hover: {
                    showType: config.get('hover.showType'),
                    showDocs: config.get('hover.showDocs')
                },
                completion: {
                    autoImport: config.get('completion.autoImport'),
                    snippets: config.get('completion.snippets')
                }
            }
        });

        await client.start();
        logger.info('Language server started');
    }

    // デバッグアダプター初期化
    debugAdapter = new DebugAdapter();
    context.subscriptions.push(debugAdapter);

    // UIコンポーネント初期化
    packageExplorer = new PackageExplorer();
    statusBar = new StatusBar();

    // コマンド登録
    registerCommands(context);

    // イベントリスナー登録
    registerEventListeners(context);

    // ワークスペース設定監視
    antigravity.workspace.onDidChangeConfiguration(e => {
        if (e.affectsConfiguration('cm')) {
            handleConfigurationChange(e);
        }
    });

    logger.info('Cm extension activated successfully');
}

// コマンド登録
function registerCommands(context) {
    // 定義へジャンプ
    context.subscriptions.push(
        antigravity.commands.registerCommand('cm.goToDefinition', async () => {
            const editor = antigravity.window.activeTextEditor;
            if (!editor) return;

            const position = editor.selection.active;
            const result = await client.goToDefinition(
                editor.document.uri,
                position
            );

            if (result) {
                await navigateToLocation(result);
            }
        })
    );

    // 型定義へジャンプ
    context.subscriptions.push(
        antigravity.commands.registerCommand('cm.goToTypeDefinition', async () => {
            const editor = antigravity.window.activeTextEditor;
            if (!editor) return;

            const position = editor.selection.active;
            const result = await client.goToTypeDefinition(
                editor.document.uri,
                position
            );

            if (result) {
                await navigateToLocation(result);
            }
        })
    );

    // 実装へジャンプ
    context.subscriptions.push(
        antigravity.commands.registerCommand('cm.goToImplementation', async () => {
            const editor = antigravity.window.activeTextEditor;
            if (!editor) return;

            const position = editor.selection.active;
            const result = await client.goToImplementation(
                editor.document.uri,
                position
            );

            if (result) {
                await navigateToLocation(result);
            }
        })
    );

    // 参照検索
    context.subscriptions.push(
        antigravity.commands.registerCommand('cm.findReferences', async () => {
            const editor = antigravity.window.activeTextEditor;
            if (!editor) return;

            const position = editor.selection.active;
            const references = await client.findReferences(
                editor.document.uri,
                position
            );

            if (references && references.length > 0) {
                await showReferencesPanel(references);
            }
        })
    );

    // シンボルリネーム
    context.subscriptions.push(
        antigravity.commands.registerCommand('cm.rename', async () => {
            const editor = antigravity.window.activeTextEditor;
            if (!editor) return;

            const position = editor.selection.active;
            const currentName = editor.document.getText(
                editor.document.getWordRangeAtPosition(position)
            );

            const newName = await antigravity.window.showInputBox({
                prompt: 'Enter new name',
                value: currentName,
                validateInput: (value) => {
                    if (!value || value.length === 0) {
                        return 'Name cannot be empty';
                    }
                    if (!/^[a-zA-Z_][a-zA-Z0-9_]*$/.test(value)) {
                        return 'Invalid identifier';
                    }
                    return null;
                }
            });

            if (newName && newName !== currentName) {
                const workspaceEdit = await client.rename(
                    editor.document.uri,
                    position,
                    newName
                );

                if (workspaceEdit) {
                    await antigravity.workspace.applyEdit(workspaceEdit);
                }
            }
        })
    );

    // フォーマット
    context.subscriptions.push(
        antigravity.commands.registerCommand('cm.format', async () => {
            const editor = antigravity.window.activeTextEditor;
            if (!editor) return;

            const formatted = await client.formatDocument(editor.document.uri);
            if (formatted) {
                await editor.edit(builder => {
                    const fullRange = new antigravity.Range(
                        editor.document.positionAt(0),
                        editor.document.positionAt(editor.document.getText().length)
                    );
                    builder.replace(fullRange, formatted);
                });
            }
        })
    );

    // ビルド
    context.subscriptions.push(
        antigravity.commands.registerCommand('cm.build', async () => {
            const terminal = antigravity.window.createTerminal('Cm Build');
            terminal.show();
            terminal.sendText('cm build');
        })
    );

    // 実行
    context.subscriptions.push(
        antigravity.commands.registerCommand('cm.run', async () => {
            const terminal = antigravity.window.createTerminal('Cm Run');
            terminal.show();
            terminal.sendText('cm run');
        })
    );

    // テスト実行
    context.subscriptions.push(
        antigravity.commands.registerCommand('cm.test', async () => {
            const terminal = antigravity.window.createTerminal('Cm Test');
            terminal.show();
            terminal.sendText('cm test');
        })
    );

    // genコマンド
    context.subscriptions.push(
        antigravity.commands.registerCommand('cm.genAdd', async () => {
            const packageName = await antigravity.window.showInputBox({
                prompt: 'Enter package name (e.g., user/repo or user/repo@v1.0.0)',
                placeHolder: 'user/repo'
            });

            if (packageName) {
                const terminal = antigravity.window.createTerminal('gen');
                terminal.show();
                terminal.sendText(`gen add ${packageName}`);
            }
        })
    );

    context.subscriptions.push(
        antigravity.commands.registerCommand('cm.genUpdate', () => {
            const terminal = antigravity.window.createTerminal('gen');
            terminal.show();
            terminal.sendText('gen update');
        })
    );

    context.subscriptions.push(
        antigravity.commands.registerCommand('cm.genInit', async () => {
            const projectType = await antigravity.window.showQuickPick(
                ['Binary', 'Library'],
                { placeHolder: 'Select project type' }
            );

            if (projectType) {
                const terminal = antigravity.window.createTerminal('gen');
                terminal.show();
                terminal.sendText(`gen init ${projectType === 'Library' ? '--lib' : '--bin'}`);
            }
        })
    );

    // 高度な機能
    context.subscriptions.push(
        antigravity.commands.registerCommand('cm.showAST', async () => {
            const editor = antigravity.window.activeTextEditor;
            if (!editor) return;

            const ast = await client.getAST(editor.document.uri);
            if (ast) {
                await showOutputPanel('Cm AST', ast, 'json');
            }
        })
    );

    context.subscriptions.push(
        antigravity.commands.registerCommand('cm.showMIR', async () => {
            const editor = antigravity.window.activeTextEditor;
            if (!editor) return;

            const mir = await client.getMIR(editor.document.uri);
            if (mir) {
                await showOutputPanel('Cm MIR', mir, 'cm-mir');
            }
        })
    );

    context.subscriptions.push(
        antigravity.commands.registerCommand('cm.showLLVM', async () => {
            const editor = antigravity.window.activeTextEditor;
            if (!editor) return;

            const llvm = await client.getLLVM(editor.document.uri);
            if (llvm) {
                await showOutputPanel('Cm LLVM IR', llvm, 'llvm');
            }
        })
    );
}

// イベントリスナー登録
function registerEventListeners(context) {
    // ドキュメント変更時の自動フォーマット
    context.subscriptions.push(
        antigravity.workspace.onWillSaveTextDocument(async (e) => {
            if (e.document.languageId === 'cm') {
                const config = new ConfigManager();
                if (config.get('format.onSave')) {
                    const formatted = await client.formatDocument(e.document.uri);
                    if (formatted) {
                        e.waitUntil(Promise.resolve([{
                            range: new antigravity.Range(
                                e.document.positionAt(0),
                                e.document.positionAt(e.document.getText().length)
                            ),
                            newText: formatted
                        }]));
                    }
                }
            }
        })
    );

    // ホバー情報表示
    context.subscriptions.push(
        antigravity.languages.registerHoverProvider('cm', {
            provideHover: async (document, position) => {
                const hover = await client.hover(document.uri, position);
                if (hover) {
                    return new antigravity.Hover(hover.contents, hover.range);
                }
                return null;
            }
        })
    );

    // コード補完
    context.subscriptions.push(
        antigravity.languages.registerCompletionItemProvider('cm', {
            provideCompletionItems: async (document, position, token, context) => {
                const completions = await client.completion(
                    document.uri,
                    position,
                    context
                );
                return completions;
            },
            resolveCompletionItem: async (item) => {
                return await client.completionResolve(item);
            }
        }, '.', ':', '<', '(')
    );

    // シグネチャヘルプ
    context.subscriptions.push(
        antigravity.languages.registerSignatureHelpProvider('cm', {
            provideSignatureHelp: async (document, position, token, context) => {
                return await client.signatureHelp(document.uri, position);
            }
        }, '(', ',')
    );

    // コードアクション
    context.subscriptions.push(
        antigravity.languages.registerCodeActionProvider('cm', {
            provideCodeActions: async (document, range, context) => {
                return await client.codeActions(document.uri, range, context);
            }
        })
    );

    // コードレンズ
    context.subscriptions.push(
        antigravity.languages.registerCodeLensProvider('cm', {
            provideCodeLenses: async (document) => {
                return await client.codeLenses(document.uri);
            },
            resolveCodeLens: async (codeLens) => {
                return await client.codeLensResolve(codeLens);
            }
        })
    );

    // ドキュメントシンボル
    context.subscriptions.push(
        antigravity.languages.registerDocumentSymbolProvider('cm', {
            provideDocumentSymbols: async (document) => {
                return await client.documentSymbols(document.uri);
            }
        })
    );

    // ワークスペースシンボル
    context.subscriptions.push(
        antigravity.languages.registerWorkspaceSymbolProvider({
            provideWorkspaceSymbols: async (query) => {
                return await client.workspaceSymbols(query);
            }
        })
    );
}

// ユーティリティ関数
async function navigateToLocation(location) {
    const uri = antigravity.Uri.parse(location.uri);
    const document = await antigravity.workspace.openTextDocument(uri);
    const editor = await antigravity.window.showTextDocument(document);

    const position = new antigravity.Position(
        location.range.start.line,
        location.range.start.character
    );

    editor.selection = new antigravity.Selection(position, position);
    editor.revealRange(new antigravity.Range(position, position));
}

async function showReferencesPanel(references) {
    const items = references.map(ref => ({
        label: `${ref.uri.split('/').pop()}:${ref.range.start.line + 1}`,
        description: ref.preview,
        location: ref
    }));

    const selected = await antigravity.window.showQuickPick(items, {
        placeHolder: 'Select a reference to navigate'
    });

    if (selected) {
        await navigateToLocation(selected.location);
    }
}

async function showOutputPanel(title, content, language) {
    const panel = antigravity.window.createWebviewPanel(
        'cmOutput',
        title,
        antigravity.ViewColumn.Beside,
        {
            enableScripts: true,
            retainContextWhenHidden: true
        }
    );

    panel.webview.html = `
        <!DOCTYPE html>
        <html>
        <head>
            <style>
                body {
                    font-family: monospace;
                    padding: 10px;
                    background-color: var(--background);
                    color: var(--foreground);
                }
                pre {
                    white-space: pre-wrap;
                    word-wrap: break-word;
                }
                .highlight {
                    background-color: var(--highlight);
                }
            </style>
        </head>
        <body>
            <pre><code class="${language}">${escapeHtml(content)}</code></pre>
            <script>
                // シンタックスハイライト用のスクリプト
                if (window.hljs) {
                    document.querySelectorAll('pre code').forEach((block) => {
                        hljs.highlightBlock(block);
                    });
                }
            </script>
        </body>
        </html>
    `;
}

function escapeHtml(text) {
    const map = {
        '&': '&amp;',
        '<': '&lt;',
        '>': '&gt;',
        '"': '&quot;',
        "'": '&#039;'
    };
    return text.replace(/[&<>"']/g, m => map[m]);
}

function handleConfigurationChange(e) {
    const logger = new Logger('Cm Extension');
    logger.info('Configuration changed, reloading...');

    // LSPクライアント再起動が必要な設定変更
    if (e.affectsConfiguration('cm.lsp')) {
        if (client) {
            client.restart();
        }
    }

    // UIコンポーネントの更新
    if (statusBar) {
        statusBar.update();
    }
}

// 拡張機能のディアクティベート
async function deactivate() {
    if (client) {
        await client.stop();
    }
    if (debugAdapter) {
        debugAdapter.dispose();
    }
}

module.exports = {
    activate,
    deactivate
};
```

## Antigravity文法定義

```yaml
# syntaxes/cm.ag-syntax
name: Cm
scopeName: source.cm
fileTypes: [cm]
patterns:
  - include: '#comments'
  - include: '#strings'
  - include: '#keywords'
  - include: '#types'
  - include: '#functions'
  - include: '#numbers'
  - include: '#operators'

repository:
  comments:
    patterns:
      - name: comment.line.double-slash.cm
        match: '//.*$'
      - name: comment.block.cm
        begin: '/\*'
        end: '\*/'

  strings:
    patterns:
      - name: string.quoted.double.cm
        begin: '"'
        end: '"'
        patterns:
          - name: constant.character.escape.cm
            match: '\\[\\\"nrt]'
          - name: string.interpolation.cm
            begin: '\{'
            end: '\}'

  keywords:
    patterns:
      - name: keyword.control.cm
        match: '\b(if|else|while|for|return|break|continue|match|case)\b'
      - name: keyword.declaration.cm
        match: '\b(struct|enum|union|typedef|impl|trait|with|overload)\b'
      - name: storage.modifier.cm
        match: '\b(const|static|extern|inline|volatile|pub|mut)\b'
      - name: keyword.operator.cm
        match: '\b(move|as|sizeof|typeof|alignof)\b'
      - name: constant.language.cm
        match: '\b(true|false|null|nullptr)\b'

  types:
    patterns:
      - name: storage.type.primitive.cm
        match: '\b(void|bool|char|int|float|double|[iu](8|16|32|64|128)|[f](32|64)|size_t|usize|isize)\b'
      - name: entity.name.type.cm
        match: '\b[A-Z][A-Za-z0-9_]*\b'

  functions:
    patterns:
      - name: entity.name.function.cm
        match: '\b[a-z_][a-zA-Z0-9_]*\s*(?=\()'

  numbers:
    patterns:
      - name: constant.numeric.hex.cm
        match: '0x[0-9a-fA-F_]+'
      - name: constant.numeric.binary.cm
        match: '0b[01_]+'
      - name: constant.numeric.octal.cm
        match: '0o[0-7_]+'
      - name: constant.numeric.float.cm
        match: '\b\d+\.\d+([eE][+-]?\d+)?(f32|f64)?\b'
      - name: constant.numeric.integer.cm
        match: '\b\d+([iu](8|16|32|64|size))?\b'

  operators:
    patterns:
      - name: keyword.operator.arithmetic.cm
        match: '[+\-*/%]'
      - name: keyword.operator.comparison.cm
        match: '(==|!=|<=?|>=?)'
      - name: keyword.operator.logical.cm
        match: '(&&|\|\||!)'
      - name: keyword.operator.bitwise.cm
        match: '[&|^~]|<<|>>'
      - name: keyword.operator.assignment.cm
        match: '(=|\+=|-=|\*=|/=|%=|&=|\|=|\^=|<<=|>>=)'
```

## スニペット定義

```json
// snippets/cm.ag-snippets
{
  "Function": {
    "prefix": "fn",
    "body": [
      "${1:return_type} ${2:function_name}(${3:params}) {",
      "\t$0",
      "}"
    ],
    "description": "Function definition"
  },
  "Struct": {
    "prefix": "struct",
    "body": [
      "struct ${1:Name} {",
      "\t${2:type} ${3:field};$0",
      "}"
    ],
    "description": "Struct definition"
  },
  "Enum": {
    "prefix": "enum",
    "body": [
      "enum ${1:Name} {",
      "\t${2:Variant1},",
      "\t${3:Variant2},$0",
      "}"
    ],
    "description": "Enum definition"
  },
  "For Loop": {
    "prefix": "for",
    "body": [
      "for (${1:int i = 0}; ${2:i < n}; ${3:i++}) {",
      "\t$0",
      "}"
    ],
    "description": "For loop"
  },
  "If Statement": {
    "prefix": "if",
    "body": [
      "if (${1:condition}) {",
      "\t$0",
      "}"
    ],
    "description": "If statement"
  },
  "Main Function": {
    "prefix": "main",
    "body": [
      "int main(int argc, char** argv) {",
      "\t$0",
      "\treturn 0;",
      "}"
    ],
    "description": "Main function"
  },
  "Generic Function": {
    "prefix": "gfn",
    "body": [
      "<${1:T}: ${2:Trait}>",
      "${3:T} ${4:function_name}(${5:T param}) {",
      "\t$0",
      "}"
    ],
    "description": "Generic function"
  },
  "Test": {
    "prefix": "test",
    "body": [
      "#[test]",
      "void test_${1:name}() {",
      "\t$0",
      "}"
    ],
    "description": "Test function"
  },
  "Import": {
    "prefix": "import",
    "body": "import ${1:module};$0",
    "description": "Import statement"
  },
  "Match": {
    "prefix": "match",
    "body": [
      "match ${1:expression} {",
      "\t${2:pattern} => ${3:result},",
      "\t_ => ${0:default},",
      "}"
    ],
    "description": "Match expression"
  }
}
```

これで、Antigravity IDE向けのCm言語拡張機能の完全な設計が完成しました。LSP統合、デバッグ機能、gen パッケージマネージャーとの統合など、包括的な開発環境を提供します。