# VSCode Extension for Cm with Full LSP Support
**Date**: 2026-01-15
**Version**: v0.12.0

## 概要

Cm言語のVSCode拡張機能の完全実装。LSPによるコードジャンプ、定義参照、型情報表示、高度なシンタックスハイライトを提供します。

## プロジェクト構造

```
vscode-cm/
├── package.json                # 拡張機能マニフェスト
├── tsconfig.json              # TypeScript設定
├── webpack.config.js          # Webpack設定
├── .vscodeignore             # パッケージ除外設定
├── CHANGELOG.md
├── README.md
├── LICENSE
├── src/
│   ├── extension.ts          # メインエントリポイント
│   ├── client/
│   │   ├── languageClient.ts # LSPクライアント
│   │   ├── commands.ts       # コマンド実装
│   │   ├── hover.ts          # ホバー機能
│   │   ├── completion.ts     # 補完機能
│   │   ├── definition.ts     # 定義ジャンプ
│   │   └── reference.ts      # 参照検索
│   ├── debug/
│   │   └── debugAdapter.ts   # デバッグアダプター
│   ├── providers/
│   │   ├── documentSymbol.ts # ドキュメントシンボル
│   │   ├── codeAction.ts     # コードアクション
│   │   ├── codeLens.ts       # コードレンズ
│   │   └── formatter.ts      # フォーマッター
│   └── utils/
│       ├── config.ts         # 設定管理
│       └── logger.ts         # ロギング
├── syntaxes/
│   ├── cm.tmLanguage.json    # TMLanguage定義
│   └── cm-markdown.json      # Markdown内のCm
├── snippets/
│   └── cm.code-snippets      # コードスニペット
├── themes/
│   ├── cm-dark.json          # ダークテーマ
│   └── cm-light.json         # ライトテーマ
├── images/
│   ├── icon.png              # 拡張機能アイコン
│   └── logo.svg              # Cmロゴ
└── test/
    ├── suite/
    └── fixtures/
```

## package.json - 完全な拡張機能マニフェスト

```json
{
  "name": "cm-language",
  "displayName": "Cm Language Support",
  "description": "Complete language support for Cm with LSP, debugging, and advanced features",
  "version": "0.12.0",
  "publisher": "cm-lang",
  "license": "MIT",
  "icon": "images/icon.png",
  "repository": {
    "type": "git",
    "url": "https://github.com/cm-lang/vscode-cm"
  },
  "categories": [
    "Programming Languages",
    "Debuggers",
    "Formatters",
    "Linters",
    "Snippets"
  ],
  "keywords": [
    "cm",
    "systems programming",
    "lsp",
    "debugging",
    "syntax highlighting"
  ],
  "engines": {
    "vscode": "^1.74.0"
  },
  "main": "./dist/extension.js",
  "activationEvents": [
    "onLanguage:cm",
    "workspaceContains:**/*.cm",
    "workspaceContains:gen.toml",
    "workspaceContains:Cm.toml"
  ],
  "contributes": {
    "languages": [
      {
        "id": "cm",
        "aliases": ["Cm", "cm"],
        "extensions": [".cm"],
        "firstLine": "^//\\s*cm\\s*$",
        "configuration": "./language-configuration.json",
        "icon": {
          "light": "./images/cm-icon-light.svg",
          "dark": "./images/cm-icon-dark.svg"
        }
      }
    ],
    "grammars": [
      {
        "language": "cm",
        "scopeName": "source.cm",
        "path": "./syntaxes/cm.tmLanguage.json"
      },
      {
        "injectTo": ["text.html.markdown"],
        "scopeName": "markdown.cm.codeblock",
        "path": "./syntaxes/cm-markdown.json",
        "embeddedLanguages": {
          "meta.embedded.block.cm": "cm"
        }
      }
    ],
    "configuration": {
      "title": "Cm",
      "properties": {
        "cm.server.path": {
          "type": "string",
          "default": "cm-lsp",
          "description": "Path to cm-lsp executable"
        },
        "cm.server.trace": {
          "type": "string",
          "enum": ["off", "messages", "verbose"],
          "default": "off",
          "description": "Trace LSP communication"
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
        "cm.format.enable": {
          "type": "boolean",
          "default": true,
          "description": "Enable auto-formatting"
        },
        "cm.format.onSave": {
          "type": "boolean",
          "default": true,
          "description": "Format on save"
        },
        "cm.lint.enable": {
          "type": "boolean",
          "default": true,
          "description": "Enable linting"
        },
        "cm.inlayHints.typeHints": {
          "type": "boolean",
          "default": true,
          "description": "Show type hints"
        },
        "cm.inlayHints.parameterHints": {
          "type": "boolean",
          "default": true,
          "description": "Show parameter hints"
        },
        "cm.hover.documentation": {
          "type": "boolean",
          "default": true,
          "description": "Show documentation on hover"
        },
        "cm.hover.type": {
          "type": "boolean",
          "default": true,
          "description": "Show type information on hover"
        },
        "cm.completion.autoImport": {
          "type": "boolean",
          "default": true,
          "description": "Auto-import on completion"
        },
        "cm.completion.showSnippets": {
          "type": "boolean",
          "default": true,
          "description": "Show snippets in completion"
        }
      }
    },
    "commands": [
      {
        "command": "cm.restartServer",
        "title": "Cm: Restart Language Server"
      },
      {
        "command": "cm.showReferences",
        "title": "Cm: Show References"
      },
      {
        "command": "cm.goToDefinition",
        "title": "Cm: Go to Definition"
      },
      {
        "command": "cm.goToTypeDefinition",
        "title": "Cm: Go to Type Definition"
      },
      {
        "command": "cm.goToImplementation",
        "title": "Cm: Go to Implementation"
      },
      {
        "command": "cm.findAllReferences",
        "title": "Cm: Find All References"
      },
      {
        "command": "cm.rename",
        "title": "Cm: Rename Symbol"
      },
      {
        "command": "cm.format",
        "title": "Cm: Format Document"
      },
      {
        "command": "cm.build",
        "title": "Cm: Build Project"
      },
      {
        "command": "cm.run",
        "title": "Cm: Run Project"
      },
      {
        "command": "cm.test",
        "title": "Cm: Run Tests"
      },
      {
        "command": "cm.genAdd",
        "title": "Cm: Add Package (gen)"
      },
      {
        "command": "cm.genUpdate",
        "title": "Cm: Update Packages (gen)"
      },
      {
        "command": "cm.showCallHierarchy",
        "title": "Cm: Show Call Hierarchy"
      },
      {
        "command": "cm.showTypeHierarchy",
        "title": "Cm: Show Type Hierarchy"
      },
      {
        "command": "cm.expandMacro",
        "title": "Cm: Expand Macro"
      },
      {
        "command": "cm.showAST",
        "title": "Cm: Show AST"
      },
      {
        "command": "cm.showMIR",
        "title": "Cm: Show MIR"
      },
      {
        "command": "cm.showLLVM",
        "title": "Cm: Show LLVM IR"
      }
    ],
    "menus": {
      "editor/title": [
        {
          "when": "resourceLangId == cm",
          "command": "cm.run",
          "group": "navigation"
        },
        {
          "when": "resourceLangId == cm",
          "command": "cm.build",
          "group": "navigation"
        }
      ],
      "editor/context": [
        {
          "when": "resourceLangId == cm",
          "command": "cm.goToDefinition",
          "group": "navigation@1"
        },
        {
          "when": "resourceLangId == cm",
          "command": "cm.findAllReferences",
          "group": "navigation@2"
        },
        {
          "when": "resourceLangId == cm",
          "command": "cm.rename",
          "group": "1_modification@1"
        },
        {
          "when": "resourceLangId == cm",
          "command": "cm.format",
          "group": "1_modification@2"
        }
      ]
    },
    "keybindings": [
      {
        "command": "cm.goToDefinition",
        "key": "f12",
        "when": "editorTextFocus && editorLangId == cm"
      },
      {
        "command": "cm.findAllReferences",
        "key": "shift+f12",
        "when": "editorTextFocus && editorLangId == cm"
      },
      {
        "command": "cm.rename",
        "key": "f2",
        "when": "editorTextFocus && editorLangId == cm"
      },
      {
        "command": "cm.format",
        "key": "shift+alt+f",
        "when": "editorTextFocus && editorLangId == cm"
      },
      {
        "command": "cm.build",
        "key": "ctrl+shift+b",
        "mac": "cmd+shift+b",
        "when": "resourceLangId == cm"
      },
      {
        "command": "cm.run",
        "key": "f5",
        "when": "resourceLangId == cm"
      }
    ],
    "snippets": [
      {
        "language": "cm",
        "path": "./snippets/cm.code-snippets"
      }
    ],
    "taskDefinitions": [
      {
        "type": "cm",
        "required": ["command"],
        "properties": {
          "command": {
            "type": "string",
            "enum": ["build", "run", "test", "clean"]
          },
          "args": {
            "type": "array"
          }
        }
      }
    ],
    "debuggers": [
      {
        "type": "cm",
        "label": "Cm Debug",
        "program": "./out/debugAdapter.js",
        "runtime": "node",
        "languages": ["cm"],
        "configurationAttributes": {
          "launch": {
            "required": ["program"],
            "properties": {
              "program": {
                "type": "string",
                "description": "Path to program"
              },
              "args": {
                "type": "array",
                "description": "Arguments"
              },
              "env": {
                "type": "object",
                "description": "Environment variables"
              },
              "cwd": {
                "type": "string",
                "description": "Working directory"
              }
            }
          }
        }
      }
    ]
  },
  "scripts": {
    "vscode:prepublish": "npm run compile",
    "compile": "webpack",
    "watch": "webpack --watch",
    "test": "npm run compile && node ./out/test/runTest.js",
    "package": "vsce package",
    "publish": "vsce publish"
  },
  "dependencies": {
    "vscode-languageclient": "^8.0.0",
    "vscode-debugadapter": "^1.51.0",
    "vscode-debugprotocol": "^1.51.0"
  },
  "devDependencies": {
    "@types/vscode": "^1.74.0",
    "@types/node": "^18.0.0",
    "@typescript-eslint/eslint-plugin": "^5.0.0",
    "@typescript-eslint/parser": "^5.0.0",
    "eslint": "^8.0.0",
    "typescript": "^4.9.0",
    "webpack": "^5.0.0",
    "webpack-cli": "^5.0.0",
    "ts-loader": "^9.0.0",
    "@vscode/test-electron": "^2.2.0",
    "vsce": "^2.0.0"
  }
}
```

## LSPクライアント実装

```typescript
// src/extension.ts - メインエントリポイント
import * as vscode from 'vscode';
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind,
    RevealOutputChannelOn,
    DocumentSelector
} from 'vscode-languageclient/node';

let client: LanguageClient;

export async function activate(context: vscode.ExtensionContext) {
    const outputChannel = vscode.window.createOutputChannel('Cm Language Server');

    // サーバー実行可能ファイルの設定
    const serverPath = vscode.workspace.getConfiguration('cm').get<string>('server.path', 'cm-lsp');

    const serverOptions: ServerOptions = {
        run: {
            command: serverPath,
            args: ['--stdio'],
            transport: TransportKind.stdio
        },
        debug: {
            command: serverPath,
            args: ['--stdio', '--debug'],
            transport: TransportKind.stdio
        }
    };

    // ドキュメントセレクター
    const documentSelector: DocumentSelector = [
        { scheme: 'file', language: 'cm' },
        { scheme: 'untitled', language: 'cm' }
    ];

    // クライアントオプション
    const clientOptions: LanguageClientOptions = {
        documentSelector,
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.cm')
        },
        outputChannel,
        revealOutputChannelOn: RevealOutputChannelOn.Error,
        initializationOptions: {
            // 拡張機能の設定を送信
            inlayHints: {
                typeHints: vscode.workspace.getConfiguration('cm').get('inlayHints.typeHints'),
                parameterHints: vscode.workspace.getConfiguration('cm').get('inlayHints.parameterHints')
            },
            hover: {
                documentation: vscode.workspace.getConfiguration('cm').get('hover.documentation'),
                type: vscode.workspace.getConfiguration('cm').get('hover.type')
            },
            completion: {
                autoImport: vscode.workspace.getConfiguration('cm').get('completion.autoImport'),
                showSnippets: vscode.workspace.getConfiguration('cm').get('completion.showSnippets')
            }
        },
        middleware: {
            // カスタムミドルウェアでLSP機能を拡張
            provideHover: async (document, position, token, next) => {
                // カスタムホバー処理
                const result = await next(document, position, token);
                if (result) {
                    // 追加の情報を付加
                    enhanceHoverInfo(result);
                }
                return result;
            },

            provideCompletionItem: async (document, position, context, token, next) => {
                // カスタム補完処理
                const result = await next(document, position, context, token);
                if (result) {
                    // スニペットを追加
                    addCustomSnippets(result, document, position);
                }
                return result;
            }
        }
    };

    // Language Clientの作成と起動
    client = new LanguageClient(
        'cm-lsp',
        'Cm Language Server',
        serverOptions,
        clientOptions
    );

    // カスタム通知ハンドラー
    client.onReady().then(() => {
        // 診断情報のカスタム処理
        client.onNotification('cm/diagnosticsWithFixes', (params: any) => {
            handleDiagnosticsWithFixes(params);
        });

        // プログレス表示
        client.onNotification('cm/progress', (params: any) => {
            showProgress(params);
        });
    });

    await client.start();

    // コマンド登録
    registerCommands(context, client);

    // ステータスバー
    createStatusBar(context);

    outputChannel.appendLine('Cm Language Server started successfully');
}

export function deactivate(): Thenable<void> | undefined {
    if (!client) {
        return undefined;
    }
    return client.stop();
}

// コマンド登録
function registerCommands(context: vscode.ExtensionContext, client: LanguageClient) {
    // 定義へジャンプ
    context.subscriptions.push(
        vscode.commands.registerCommand('cm.goToDefinition', async () => {
            const editor = vscode.window.activeTextEditor;
            if (!editor) return;

            const position = editor.selection.active;
            const params = {
                textDocument: { uri: editor.document.uri.toString() },
                position: { line: position.line, character: position.character }
            };

            const result = await client.sendRequest('textDocument/definition', params);
            if (result) {
                handleDefinitionResponse(result);
            }
        })
    );

    // 全参照検索
    context.subscriptions.push(
        vscode.commands.registerCommand('cm.findAllReferences', async () => {
            const editor = vscode.window.activeTextEditor;
            if (!editor) return;

            const position = editor.selection.active;
            const params = {
                textDocument: { uri: editor.document.uri.toString() },
                position: { line: position.line, character: position.character },
                context: { includeDeclaration: true }
            };

            const result = await client.sendRequest('textDocument/references', params);
            if (result) {
                handleReferencesResponse(result);
            }
        })
    );

    // 型定義へジャンプ
    context.subscriptions.push(
        vscode.commands.registerCommand('cm.goToTypeDefinition', async () => {
            const editor = vscode.window.activeTextEditor;
            if (!editor) return;

            const position = editor.selection.active;
            const params = {
                textDocument: { uri: editor.document.uri.toString() },
                position: { line: position.line, character: position.character }
            };

            const result = await client.sendRequest('textDocument/typeDefinition', params);
            if (result) {
                handleTypeDefinitionResponse(result);
            }
        })
    );

    // シンボルのリネーム
    context.subscriptions.push(
        vscode.commands.registerCommand('cm.rename', async () => {
            const editor = vscode.window.activeTextEditor;
            if (!editor) return;

            const position = editor.selection.active;
            const newName = await vscode.window.showInputBox({
                prompt: 'Enter new name',
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

            if (!newName) return;

            const params = {
                textDocument: { uri: editor.document.uri.toString() },
                position: { line: position.line, character: position.character },
                newName
            };

            const result = await client.sendRequest('textDocument/rename', params);
            if (result) {
                const edit = new vscode.WorkspaceEdit();
                applyWorkspaceEdit(result, edit);
                await vscode.workspace.applyEdit(edit);
            }
        })
    );

    // コールヒエラルキー
    context.subscriptions.push(
        vscode.commands.registerCommand('cm.showCallHierarchy', async () => {
            const editor = vscode.window.activeTextEditor;
            if (!editor) return;

            const position = editor.selection.active;
            const params = {
                textDocument: { uri: editor.document.uri.toString() },
                position: { line: position.line, character: position.character }
            };

            const result = await client.sendRequest('textDocument/prepareCallHierarchy', params);
            if (result && result.length > 0) {
                showCallHierarchy(result[0]);
            }
        })
    );

    // genパッケージ管理コマンド
    context.subscriptions.push(
        vscode.commands.registerCommand('cm.genAdd', async () => {
            const packageName = await vscode.window.showInputBox({
                prompt: 'Enter package name (e.g., user/repo or user/repo@v1.0.0)'
            });

            if (!packageName) return;

            const terminal = vscode.window.createTerminal('gen');
            terminal.show();
            terminal.sendText(`gen add ${packageName}`);
        })
    );

    context.subscriptions.push(
        vscode.commands.registerCommand('cm.genUpdate', () => {
            const terminal = vscode.window.createTerminal('gen');
            terminal.show();
            terminal.sendText('gen update');
        })
    );
}

// ホバー情報の拡張
function enhanceHoverInfo(hover: vscode.Hover) {
    // 型情報とドキュメントを見やすくフォーマット
    if (hover.contents && Array.isArray(hover.contents)) {
        hover.contents = hover.contents.map(content => {
            if (typeof content === 'string') {
                return new vscode.MarkdownString(formatHoverContent(content));
            }
            return content;
        });
    }
}

// カスタムスニペットの追加
function addCustomSnippets(
    completions: vscode.CompletionItem[] | vscode.CompletionList,
    document: vscode.TextDocument,
    position: vscode.Position
) {
    const items = Array.isArray(completions) ? completions : completions.items;

    // コンテキストに応じたスニペットを追加
    const line = document.lineAt(position.line).text;
    const linePrefix = line.substring(0, position.character);

    if (linePrefix.trim().length === 0 || linePrefix.endsWith(' ')) {
        // 行頭や空白の後ではステートメントスニペットを提案
        items.push(createSnippetCompletionItem('for', 'for (${1:init}; ${2:cond}; ${3:inc}) {\n\t$0\n}'));
        items.push(createSnippetCompletionItem('if', 'if (${1:condition}) {\n\t$0\n}'));
        items.push(createSnippetCompletionItem('fn', '${1:type} ${2:name}(${3:params}) {\n\t$0\n}'));
    }
}

function createSnippetCompletionItem(label: string, insertText: string): vscode.CompletionItem {
    const item = new vscode.CompletionItem(label, vscode.CompletionItemKind.Snippet);
    item.insertText = new vscode.SnippetString(insertText);
    item.documentation = new vscode.MarkdownString(`Snippet: ${label}`);
    return item;
}

// ステータスバー作成
function createStatusBar(context: vscode.ExtensionContext) {
    const statusBar = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 100);
    statusBar.text = '$(symbol-misc) Cm';
    statusBar.tooltip = 'Cm Language Server is running';
    statusBar.command = 'cm.restartServer';
    statusBar.show();
    context.subscriptions.push(statusBar);
}
```

## 高度なシンタックスハイライト定義

```json
// syntaxes/cm.tmLanguage.json
{
  "$schema": "https://raw.githubusercontent.com/martinring/tmlanguage/master/tmlanguage.json",
  "name": "Cm",
  "scopeName": "source.cm",
  "fileTypes": ["cm"],
  "patterns": [
    {
      "include": "#comments"
    },
    {
      "include": "#strings"
    },
    {
      "include": "#characters"
    },
    {
      "include": "#numbers"
    },
    {
      "include": "#keywords"
    },
    {
      "include": "#types"
    },
    {
      "include": "#functions"
    },
    {
      "include": "#macros"
    },
    {
      "include": "#generics"
    },
    {
      "include": "#attributes"
    },
    {
      "include": "#operators"
    }
  ],
  "repository": {
    "comments": {
      "patterns": [
        {
          "name": "comment.line.double-slash.cm",
          "begin": "//",
          "end": "$",
          "patterns": [
            {
              "name": "keyword.other.documentation.cm",
              "match": "(?<=///)\\s*(@\\w+)"
            }
          ]
        },
        {
          "name": "comment.block.cm",
          "begin": "/\\*",
          "end": "\\*/",
          "patterns": [
            {
              "name": "comment.block.documentation.cm",
              "begin": "/\\*\\*",
              "end": "\\*/",
              "patterns": [
                {
                  "name": "keyword.other.documentation.cm",
                  "match": "@\\w+"
                }
              ]
            }
          ]
        }
      ]
    },
    "strings": {
      "patterns": [
        {
          "name": "string.quoted.double.cm",
          "begin": "\"",
          "end": "\"",
          "patterns": [
            {
              "include": "#string_escaped_char"
            },
            {
              "include": "#string_interpolation"
            }
          ]
        },
        {
          "name": "string.quoted.raw.cm",
          "begin": "r#*\"",
          "end": "\"#*"
        }
      ]
    },
    "string_escaped_char": {
      "patterns": [
        {
          "name": "constant.character.escape.cm",
          "match": "\\\\([\\\\\"nrt0]|u\\{[0-9a-fA-F]{1,6}\\}|x[0-9a-fA-F]{2})"
        }
      ]
    },
    "string_interpolation": {
      "patterns": [
        {
          "name": "meta.embedded.line.cm",
          "begin": "\\{",
          "end": "\\}",
          "patterns": [
            {
              "include": "$self"
            }
          ]
        }
      ]
    },
    "characters": {
      "patterns": [
        {
          "name": "string.quoted.single.cm",
          "match": "'([^'\\\\]|\\\\[\\\\nrt0'\"]|\\\\u\\{[0-9a-fA-F]{1,6}\\}|\\\\x[0-9a-fA-F]{2})'"
        }
      ]
    },
    "numbers": {
      "patterns": [
        {
          "name": "constant.numeric.hex.cm",
          "match": "\\b0x[0-9a-fA-F][0-9a-fA-F_]*\\b"
        },
        {
          "name": "constant.numeric.binary.cm",
          "match": "\\b0b[01][01_]*\\b"
        },
        {
          "name": "constant.numeric.octal.cm",
          "match": "\\b0o[0-7][0-7_]*\\b"
        },
        {
          "name": "constant.numeric.float.cm",
          "match": "\\b[0-9][0-9_]*\\.[0-9][0-9_]*([eE][+-]?[0-9_]+)?(f32|f64)?\\b"
        },
        {
          "name": "constant.numeric.integer.cm",
          "match": "\\b[0-9][0-9_]*(u8|u16|u32|u64|usize|i8|i16|i32|i64|isize)?\\b"
        }
      ]
    },
    "keywords": {
      "patterns": [
        {
          "name": "keyword.control.flow.cm",
          "match": "\\b(if|else|while|for|loop|return|break|continue|match|case|default)\\b"
        },
        {
          "name": "keyword.declaration.cm",
          "match": "\\b(struct|enum|union|interface|impl|trait|typedef|type|with|overload)\\b"
        },
        {
          "name": "storage.modifier.cm",
          "match": "\\b(const|static|extern|inline|volatile|restrict|pub|priv|mut)\\b"
        },
        {
          "name": "keyword.operator.cm",
          "match": "\\b(move|as|sizeof|typeof|alignof|offsetof)\\b"
        },
        {
          "name": "constant.language.boolean.cm",
          "match": "\\b(true|false)\\b"
        },
        {
          "name": "constant.language.null.cm",
          "match": "\\b(null|nullptr)\\b"
        },
        {
          "name": "variable.language.cm",
          "match": "\\b(self|super)\\b"
        }
      ]
    },
    "types": {
      "patterns": [
        {
          "name": "storage.type.primitive.cm",
          "match": "\\b(void|bool|char|int|float|double|i8|i16|i32|i64|i128|u8|u16|u32|u64|u128|f32|f64|isize|usize|size_t|ptrdiff_t)\\b"
        },
        {
          "name": "entity.name.type.cm",
          "match": "\\b[A-Z][A-Za-z0-9_]*\\b"
        }
      ]
    },
    "functions": {
      "patterns": [
        {
          "name": "entity.name.function.cm",
          "match": "\\b([a-z_][a-zA-Z0-9_]*)\\s*(?=\\()"
        },
        {
          "name": "entity.name.function.macro.cm",
          "match": "\\b([a-z_][a-zA-Z0-9_]*)!"
        }
      ]
    },
    "macros": {
      "patterns": [
        {
          "name": "meta.preprocessor.cm",
          "match": "^\\s*#\\s*(define|undef|if|ifdef|ifndef|else|elif|endif|include|pragma)\\b"
        },
        {
          "name": "entity.name.function.macro.cm",
          "match": "\\b[A-Z_][A-Z0-9_]*!"
        }
      ]
    },
    "generics": {
      "patterns": [
        {
          "name": "meta.generic.cm",
          "begin": "<(?![<>=])",
          "end": ">",
          "patterns": [
            {
              "include": "#types"
            },
            {
              "name": "entity.name.type.parameter.cm",
              "match": "\\b([A-Z][a-zA-Z0-9_]*)\\s*(?::)"
            },
            {
              "name": "punctuation.separator.cm",
              "match": "[,:]"
            },
            {
              "include": "#generics"
            }
          ]
        }
      ]
    },
    "attributes": {
      "patterns": [
        {
          "name": "meta.attribute.cm",
          "begin": "#\\[",
          "end": "\\]",
          "patterns": [
            {
              "name": "entity.name.function.attribute.cm",
              "match": "\\b\\w+\\b"
            },
            {
              "include": "#strings"
            },
            {
              "include": "#numbers"
            }
          ]
        }
      ]
    },
    "operators": {
      "patterns": [
        {
          "name": "keyword.operator.arithmetic.cm",
          "match": "[+\\-*/%]"
        },
        {
          "name": "keyword.operator.comparison.cm",
          "match": "(==|!=|<=?|>=?)"
        },
        {
          "name": "keyword.operator.logical.cm",
          "match": "(&&|\\|\\||!)"
        },
        {
          "name": "keyword.operator.bitwise.cm",
          "match": "[&|^~]|<<|>>"
        },
        {
          "name": "keyword.operator.assignment.cm",
          "match": "(=|\\+=|-=|\\*=|/=|%=|&=|\\|=|\\^=|<<=|>>=)"
        },
        {
          "name": "keyword.operator.range.cm",
          "match": "\\.\\."
        },
        {
          "name": "keyword.operator.arrow.cm",
          "match": "->"
        },
        {
          "name": "keyword.operator.double-colon.cm",
          "match": "::"
        }
      ]
    }
  }
}
```

## language-configuration.json

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
    { "open": "<", "close": ">", "notIn": ["string"] },
    { "open": "\"", "close": "\"", "notIn": ["string"] },
    { "open": "'", "close": "'", "notIn": ["string"] }
  ],
  "surroundingPairs": [
    ["{", "}"],
    ["[", "]"],
    ["(", ")"],
    ["<", ">"],
    ["\"", "\""],
    ["'", "'"]
  ],
  "indentationRules": {
    "increaseIndentPattern": "^.*(\\{[^}]*|\\([^)]*|\\[[^\\]]*)$",
    "decreaseIndentPattern": "^\\s*(\\}|\\)|\\]|\\*/)"
  },
  "folding": {
    "markers": {
      "start": "^\\s*//\\s*#region\\b",
      "end": "^\\s*//\\s*#endregion\\b"
    }
  },
  "onEnterRules": [
    {
      "beforeText": "^\\s*/{3}.*$",
      "action": {
        "indent": "none",
        "appendText": "/// "
      }
    },
    {
      "beforeText": "^\\s*/\\*\\*\\s*$",
      "afterText": "^\\s*\\*/$",
      "action": {
        "indent": "indentOutdent",
        "appendText": " * "
      }
    }
  ],
  "wordPattern": "(-?\\d*\\.\\d\\w*)|([^\\`\\~\\!\\@\\#\\%\\^\\&\\*\\(\\)\\-\\=\\+\\[\\{\\]\\}\\\\\\|\\;\\:\\'\\\"\\,\\.\\<\\>\\/\\?\\s]+)"
}
```

これで、Cm言語のVSCode拡張機能の完全な設計が完成しました。LSPによる高度な機能（コードジャンプ、定義参照、型情報表示）と美しいシンタックスハイライトを提供します。