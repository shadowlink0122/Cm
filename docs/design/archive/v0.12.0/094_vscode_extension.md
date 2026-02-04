# VSCode Extension for Cm Language
**Date**: 2026-01-14
**Version**: v0.12.0

## 概要

`vscode-cm`はCm言語の公式Visual Studio Code拡張機能です。シンタックスハイライト、インテリセンス、デバッグ、リファクタリングなど、包括的な開発体験を提供します。

## 設計目標

1. **完全なIDE体験**: コード編集からデバッグまで統合環境を提供
2. **高速な応答性**: LSPによる非同期処理で軽快な動作
3. **豊富なカスタマイズ**: テーマ、設定、スニペットの充実
4. **デバッグ統合**: ブレークポイント、ステップ実行をサポート
5. **生産性ツール**: タスクランナー、テストランナーの統合

## 拡張機能アーキテクチャ

```
┌──────────────────────────────────────────────┐
│              VSCode Extension                 │
├──────────────┬──────────┬──────────┬─────────┤
│  Language    │  Debug   │  Task    │ Webview │
│  Client      │  Adapter │  Provider│  Provider│
├──────────────┴──────────┴──────────┴─────────┤
│           Extension Activation                │
├───────────────────────────────────────────────┤
│          Configuration Manager                │
└───────────────────────────────────────────────┘
                      │
        ┌─────────────┼─────────────┐
        │             │             │
    cm-lsp        cm-debug      cm compiler
```

## package.json

```json
{
  "name": "vscode-cm",
  "displayName": "Cm Language",
  "description": "Official Cm language support for Visual Studio Code",
  "version": "0.12.0",
  "publisher": "cm-lang",
  "icon": "images/cm-logo.png",
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
    "ownership",
    "memory safe"
  ],
  "engines": {
    "vscode": "^1.74.0"
  },
  "activationEvents": [
    "onLanguage:cm",
    "workspaceContains:**/*.cm",
    "workspaceContains:Cm.toml"
  ],
  "main": "./dist/extension.js",
  "contributes": {
    "languages": [
      {
        "id": "cm",
        "aliases": ["Cm", "cm"],
        "extensions": [".cm"],
        "firstLine": "^#!/.*\\bcm\\b",
        "configuration": "./language-configuration.json"
      },
      {
        "id": "cm-toml",
        "aliases": ["Cm.toml"],
        "filenames": ["Cm.toml", "Cm.lock"],
        "configuration": "./toml-configuration.json"
      }
    ],
    "grammars": [
      {
        "language": "cm",
        "scopeName": "source.cm",
        "path": "./syntaxes/cm.tmLanguage.json"
      },
      {
        "language": "cm-toml",
        "scopeName": "source.toml.cm",
        "path": "./syntaxes/cm-toml.tmLanguage.json"
      }
    ],
    "configuration": {
      "title": "Cm",
      "properties": {
        "cm.path": {
          "type": "string",
          "default": "cm",
          "description": "Path to the Cm compiler executable"
        },
        "cm.lsp.enable": {
          "type": "boolean",
          "default": true,
          "description": "Enable Cm Language Server"
        },
        "cm.lsp.trace.server": {
          "type": "string",
          "enum": ["off", "messages", "verbose"],
          "default": "off",
          "description": "Traces the communication between VS Code and the language server"
        },
        "cm.format.enable": {
          "type": "boolean",
          "default": true,
          "description": "Enable formatting via cmfmt"
        },
        "cm.format.indentSize": {
          "type": "number",
          "default": 4,
          "description": "Number of spaces for indentation"
        },
        "cm.lint.enable": {
          "type": "boolean",
          "default": true,
          "description": "Enable linting via cmlint"
        },
        "cm.lint.onSave": {
          "type": "boolean",
          "default": true,
          "description": "Run linter on save"
        },
        "cm.inlayHints.enable": {
          "type": "boolean",
          "default": true,
          "description": "Enable inlay hints"
        },
        "cm.inlayHints.typeHints": {
          "type": "boolean",
          "default": true,
          "description": "Show type hints for inferred types"
        },
        "cm.inlayHints.parameterHints": {
          "type": "boolean",
          "default": true,
          "description": "Show parameter name hints in function calls"
        },
        "cm.debug.enable": {
          "type": "boolean",
          "default": true,
          "description": "Enable debugging support"
        },
        "cm.test.enable": {
          "type": "boolean",
          "default": true,
          "description": "Enable test runner integration"
        }
      }
    },
    "commands": [
      {
        "command": "cm.build",
        "title": "Cm: Build Project",
        "icon": "$(tools)"
      },
      {
        "command": "cm.run",
        "title": "Cm: Run Project",
        "icon": "$(play)"
      },
      {
        "command": "cm.test",
        "title": "Cm: Run Tests",
        "icon": "$(beaker)"
      },
      {
        "command": "cm.format",
        "title": "Cm: Format Document"
      },
      {
        "command": "cm.addDependency",
        "title": "Cm: Add Dependency"
      },
      {
        "command": "cm.updateDependencies",
        "title": "Cm: Update Dependencies"
      },
      {
        "command": "cm.showDocumentation",
        "title": "Cm: Show Documentation"
      },
      {
        "command": "cm.expandMacro",
        "title": "Cm: Expand Macro"
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
          "command": "cm.format",
          "group": "1_modification"
        },
        {
          "when": "resourceLangId == cm",
          "command": "cm.expandMacro",
          "group": "2_cm"
        }
      ]
    },
    "keybindings": [
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
      },
      {
        "command": "cm.test",
        "key": "ctrl+shift+t",
        "mac": "cmd+shift+t",
        "when": "resourceLangId == cm"
      }
    ],
    "snippets": [
      {
        "language": "cm",
        "path": "./snippets/cm.json"
      }
    ],
    "problemMatchers": [
      {
        "name": "cm",
        "owner": "cm",
        "fileLocation": ["relative", "${workspaceFolder}"],
        "pattern": {
          "regexp": "^(.*):(\\d+):(\\d+):\\s+(error|warning|info)\\[([A-Z]\\d{3})\\]:\\s+(.*)$",
          "file": 1,
          "line": 2,
          "column": 3,
          "severity": 4,
          "code": 5,
          "message": 6
        }
      }
    ],
    "taskDefinitions": [
      {
        "type": "cm",
        "required": ["command"],
        "properties": {
          "command": {
            "type": "string",
            "description": "The cm command to run"
          },
          "args": {
            "type": "array",
            "description": "Additional arguments"
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
                "description": "Path to the program to debug"
              },
              "args": {
                "type": "array",
                "description": "Command line arguments"
              },
              "cwd": {
                "type": "string",
                "description": "Working directory"
              },
              "env": {
                "type": "object",
                "description": "Environment variables"
              }
            }
          }
        }
      }
    ]
  }
}
```

## 主要機能実装

### 1. 拡張機能のアクティベーション

```typescript
// src/extension.ts
import * as vscode from 'vscode';
import { LanguageClient, LanguageClientOptions, ServerOptions } from 'vscode-languageclient/node';
import { CmDebugAdapterFactory } from './debug/debugAdapter';
import { CmTaskProvider } from './tasks/taskProvider';
import { CmTestProvider } from './test/testProvider';
import { CmDocumentationProvider } from './docs/documentationProvider';

let client: LanguageClient;

export async function activate(context: vscode.ExtensionContext) {
    console.log('Cm extension is now active!');

    // 1. Language Server起動
    await startLanguageServer(context);

    // 2. コマンド登録
    registerCommands(context);

    // 3. デバッガー登録
    registerDebugger(context);

    // 4. タスクプロバイダー登録
    registerTaskProvider(context);

    // 5. テストプロバイダー登録
    registerTestProvider(context);

    // 6. ドキュメントプロバイダー登録
    registerDocumentationProvider(context);

    // 7. ステータスバー表示
    createStatusBarItems(context);
}

async function startLanguageServer(context: vscode.ExtensionContext) {
    const serverExecutable = {
        command: 'cm-lsp',
        args: ['--stdio'],
        options: {
            env: {
                ...process.env,
                RUST_LOG: 'info',
            },
        },
    };

    const serverOptions: ServerOptions = {
        run: serverExecutable,
        debug: {
            ...serverExecutable,
            args: ['--stdio', '--debug'],
        },
    };

    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: 'file', language: 'cm' }],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.cm'),
        },
        initializationOptions: {
            // カスタム設定
            inlayHints: {
                enable: vscode.workspace.getConfiguration('cm.inlayHints').get('enable'),
                typeHints: vscode.workspace.getConfiguration('cm.inlayHints').get('typeHints'),
                parameterHints: vscode.workspace.getConfiguration('cm.inlayHints').get('parameterHints'),
            },
        },
    };

    client = new LanguageClient('cm-lsp', 'Cm Language Server', serverOptions, clientOptions);

    await client.start();
}
```

### 2. シンタックスハイライト

```json
// syntaxes/cm.tmLanguage.json
{
  "name": "Cm",
  "scopeName": "source.cm",
  "patterns": [
    {
      "include": "#comments"
    },
    {
      "include": "#strings"
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
      "include": "#constants"
    },
    {
      "include": "#macros"
    },
    {
      "include": "#generics"
    }
  ],
  "repository": {
    "comments": {
      "patterns": [
        {
          "name": "comment.line.double-slash.cm",
          "match": "//.*$"
        },
        {
          "name": "comment.block.cm",
          "begin": "/\\*",
          "end": "\\*/"
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
              "name": "constant.character.escape.cm",
              "match": "\\\\[\\\\\"nrt]"
            },
            {
              "name": "constant.character.escape.unicode.cm",
              "match": "\\\\u\\{[0-9a-fA-F]+\\}"
            }
          ]
        }
      ]
    },
    "keywords": {
      "patterns": [
        {
          "name": "keyword.control.cm",
          "match": "\\b(if|else|while|for|return|break|continue|match|case|default)\\b"
        },
        {
          "name": "keyword.other.cm",
          "match": "\\b(struct|enum|union|typedef|interface|impl|trait|with|overload)\\b"
        },
        {
          "name": "storage.modifier.cm",
          "match": "\\b(const|static|extern|inline|volatile|restrict)\\b"
        },
        {
          "name": "keyword.operator.cm",
          "match": "\\b(move|sizeof|typeof|alignof)\\b"
        }
      ]
    },
    "types": {
      "patterns": [
        {
          "name": "storage.type.primitive.cm",
          "match": "\\b(void|bool|char|int|float|double|size_t|ptrdiff_t|int8|int16|int32|int64|uint8|uint16|uint32|uint64|f32|f64)\\b"
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
          "match": "\\b([a-z_][a-zA-Z0-9_]*)\\s*\\(",
          "captures": {
            "1": {
              "name": "entity.name.function.cm"
            }
          }
        }
      ]
    },
    "constants": {
      "patterns": [
        {
          "name": "constant.language.cm",
          "match": "\\b(true|false|null|nullptr)\\b"
        },
        {
          "name": "constant.numeric.cm",
          "match": "\\b(0x[0-9a-fA-F]+|0b[01]+|0o[0-7]+|\\d+\\.?\\d*([eE][+-]?\\d+)?)\\b"
        },
        {
          "name": "variable.other.constant.cm",
          "match": "\\b[A-Z_][A-Z0-9_]*\\b"
        }
      ]
    },
    "macros": {
      "patterns": [
        {
          "name": "meta.preprocessor.cm",
          "match": "^\\s*#\\s*(define|undef|if|ifdef|ifndef|else|elif|endif|include|pragma)\\b.*$"
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
          "begin": "<",
          "end": ">",
          "patterns": [
            {
              "include": "#types"
            },
            {
              "match": "\\b([A-Z][a-zA-Z0-9_]*)\\s*(:)\\s*([A-Za-z_][A-Za-z0-9_]*)",
              "captures": {
                "1": {
                  "name": "entity.name.type.parameter.cm"
                },
                "2": {
                  "name": "punctuation.separator.cm"
                },
                "3": {
                  "name": "entity.name.type.constraint.cm"
                }
              }
            }
          ]
        }
      ]
    }
  }
}
```

### 3. コード補完とスニペット

```json
// snippets/cm.json
{
  "Function": {
    "prefix": "fn",
    "body": [
      "${1:return_type} ${2:function_name}(${3:params}) {",
      "    $0",
      "}"
    ],
    "description": "Function definition"
  },
  "Struct": {
    "prefix": "struct",
    "body": [
      "struct ${1:Name} {",
      "    ${2:field_type} ${3:field_name};$0",
      "}"
    ],
    "description": "Struct definition"
  },
  "Enum": {
    "prefix": "enum",
    "body": [
      "enum ${1:Name} {",
      "    ${2:Variant1},",
      "    ${3:Variant2},$0",
      "}"
    ],
    "description": "Enum definition"
  },
  "Impl": {
    "prefix": "impl",
    "body": [
      "impl ${1:Type} {",
      "    ${2:return_type} ${3:method_name}(${4:&self}) {",
      "        $0",
      "    }",
      "}"
    ],
    "description": "Implementation block"
  },
  "Generic Function": {
    "prefix": "gfn",
    "body": [
      "<${1:T}: ${2:Trait}>",
      "${3:return_type} ${4:function_name}(${5:params}) {",
      "    $0",
      "}"
    ],
    "description": "Generic function"
  },
  "Test": {
    "prefix": "test",
    "body": [
      "#[test]",
      "void test_${1:name}() {",
      "    $0",
      "}"
    ],
    "description": "Test function"
  },
  "Main": {
    "prefix": "main",
    "body": [
      "int main(int argc, char** argv) {",
      "    $0",
      "    return 0;",
      "}"
    ],
    "description": "Main function"
  },
  "For Loop": {
    "prefix": "for",
    "body": [
      "for (int ${1:i} = 0; ${1:i} < ${2:n}; ${1:i}++) {",
      "    $0",
      "}"
    ],
    "description": "For loop"
  },
  "While Loop": {
    "prefix": "while",
    "body": [
      "while (${1:condition}) {",
      "    $0",
      "}"
    ],
    "description": "While loop"
  },
  "If Statement": {
    "prefix": "if",
    "body": [
      "if (${1:condition}) {",
      "    $0",
      "}"
    ],
    "description": "If statement"
  },
  "If-Else": {
    "prefix": "ife",
    "body": [
      "if (${1:condition}) {",
      "    ${2:// then}",
      "} else {",
      "    ${0:// else}",
      "}"
    ],
    "description": "If-else statement"
  },
  "Match": {
    "prefix": "match",
    "body": [
      "match ${1:expression} {",
      "    ${2:Pattern1} => ${3:result1},",
      "    ${4:Pattern2} => ${5:result2},",
      "    _ => ${0:default},",
      "}"
    ],
    "description": "Match expression"
  }
}
```

### 4. デバッグアダプター

```typescript
// src/debug/debugAdapter.ts
import * as vscode from 'vscode';
import { DebugProtocol } from '@vscode/debugprotocol';

export class CmDebugAdapter implements vscode.DebugAdapter {
    private sendMessage: (message: DebugProtocol.ProtocolMessage) => void;

    constructor() {
        // Initialize debug adapter
    }

    onDidSendMessage(handler: (message: DebugProtocol.ProtocolMessage) => void): void {
        this.sendMessage = handler;
    }

    handleMessage(message: DebugProtocol.ProtocolMessage): void {
        if (message.type === 'request') {
            const request = message as DebugProtocol.Request;
            switch (request.command) {
                case 'initialize':
                    this.handleInitialize(request);
                    break;
                case 'launch':
                    this.handleLaunch(request);
                    break;
                case 'setBreakpoints':
                    this.handleSetBreakpoints(request);
                    break;
                case 'continue':
                    this.handleContinue(request);
                    break;
                case 'next':
                    this.handleNext(request);
                    break;
                case 'stepIn':
                    this.handleStepIn(request);
                    break;
                case 'stepOut':
                    this.handleStepOut(request);
                    break;
                case 'stackTrace':
                    this.handleStackTrace(request);
                    break;
                case 'scopes':
                    this.handleScopes(request);
                    break;
                case 'variables':
                    this.handleVariables(request);
                    break;
                case 'evaluate':
                    this.handleEvaluate(request);
                    break;
                default:
                    this.sendErrorResponse(request, `Unknown command: ${request.command}`);
            }
        }
    }

    private handleInitialize(request: DebugProtocol.InitializeRequest): void {
        const response: DebugProtocol.InitializeResponse = {
            seq: 0,
            type: 'response',
            request_seq: request.seq,
            command: request.command,
            success: true,
            body: {
                supportsConfigurationDoneRequest: true,
                supportsFunctionBreakpoints: true,
                supportsConditionalBreakpoints: true,
                supportsEvaluateForHovers: true,
                supportsStepBack: false,
                supportsSetVariable: true,
                supportsRestartFrame: false,
                supportsGotoTargetsRequest: false,
                supportsStepInTargetsRequest: false,
                supportsCompletionsRequest: true,
                supportsModulesRequest: false,
                supportsExceptionOptions: true,
                supportsValueFormattingOptions: true,
                supportsExceptionInfoRequest: true,
                supportTerminateDebuggee: true,
                supportSuspendDebuggee: true,
            },
        };
        this.sendMessage(response);
    }

    dispose(): void {
        // Cleanup resources
    }
}
```

### 5. Webview機能

```typescript
// src/webview/documentationView.ts
export class DocumentationWebview {
    private panel: vscode.WebviewPanel | undefined;

    public show(context: vscode.ExtensionContext, symbol: string) {
        if (this.panel) {
            this.panel.reveal();
        } else {
            this.panel = vscode.window.createWebviewPanel(
                'cmDocumentation',
                'Cm Documentation',
                vscode.ViewColumn.Two,
                {
                    enableScripts: true,
                    retainContextWhenHidden: true,
                }
            );

            this.panel.onDidDispose(() => {
                this.panel = undefined;
            });
        }

        this.panel.webview.html = this.getHtmlForWebview(symbol);
    }

    private getHtmlForWebview(symbol: string): string {
        return `<!DOCTYPE html>
        <html>
        <head>
            <meta charset="UTF-8">
            <style>
                body {
                    font-family: var(--vscode-font-family);
                    color: var(--vscode-foreground);
                    background-color: var(--vscode-editor-background);
                    padding: 20px;
                }
                h1 { color: var(--vscode-textLink-foreground); }
                code {
                    background-color: var(--vscode-textCodeBlock-background);
                    padding: 2px 4px;
                    border-radius: 3px;
                }
                pre {
                    background-color: var(--vscode-textCodeBlock-background);
                    padding: 10px;
                    border-radius: 5px;
                    overflow-x: auto;
                }
            </style>
        </head>
        <body>
            <h1>${symbol}</h1>
            <div id="content">
                <!-- Documentation content will be loaded here -->
            </div>
            <script>
                // Fetch and display documentation
                fetch('/api/docs/${symbol}')
                    .then(response => response.text())
                    .then(html => {
                        document.getElementById('content').innerHTML = html;
                    });
            </script>
        </body>
        </html>`;
    }
}
```

### 6. テストランナー統合

```typescript
// src/test/testProvider.ts
export class CmTestProvider {
    private testController: vscode.TestController;

    constructor(context: vscode.ExtensionContext) {
        this.testController = vscode.tests.createTestController('cmTests', 'Cm Tests');
        context.subscriptions.push(this.testController);

        this.testController.createRunProfile(
            'Run',
            vscode.TestRunProfileKind.Run,
            (request, token) => this.runTests(request, token)
        );

        this.testController.createRunProfile(
            'Debug',
            vscode.TestRunProfileKind.Debug,
            (request, token) => this.debugTests(request, token)
        );

        // Watch for test file changes
        const watcher = vscode.workspace.createFileSystemWatcher('**/*_test.cm');
        watcher.onDidCreate(uri => this.parseTestFile(uri));
        watcher.onDidChange(uri => this.parseTestFile(uri));
        watcher.onDidDelete(uri => this.deleteTests(uri));

        // Initial discovery
        this.discoverTests();
    }

    private async discoverTests() {
        const testFiles = await vscode.workspace.findFiles('**/*_test.cm');
        for (const file of testFiles) {
            await this.parseTestFile(file);
        }
    }

    private async parseTestFile(uri: vscode.Uri) {
        const content = await vscode.workspace.fs.readFile(uri);
        const text = new TextDecoder().decode(content);

        // Parse test functions
        const testRegex = /#\[test\]\s+void\s+(\w+)\s*\(\)/g;
        let match;

        const file = this.testController.createTestItem(uri.toString(), uri.fsPath, uri);

        while ((match = testRegex.exec(text)) !== null) {
            const testName = match[1];
            const line = text.substring(0, match.index).split('\n').length - 1;

            const test = this.testController.createTestItem(
                `${uri.toString()}::${testName}`,
                testName,
                uri
            );
            test.range = new vscode.Range(line, 0, line, match[0].length);

            file.children.add(test);
        }

        this.testController.items.add(file);
    }

    private async runTests(
        request: vscode.TestRunRequest,
        token: vscode.CancellationToken
    ) {
        const run = this.testController.createTestRun(request);

        for (const test of request.include ?? []) {
            if (token.isCancellationRequested) {
                break;
            }

            run.started(test);

            try {
                // Run the test
                const result = await this.executeTest(test);

                if (result.success) {
                    run.passed(test, result.duration);
                } else {
                    run.failed(test, new vscode.TestMessage(result.message), result.duration);
                }
            } catch (error) {
                run.errored(test, new vscode.TestMessage(error.toString()));
            }
        }

        run.end();
    }

    private async executeTest(test: vscode.TestItem): Promise<TestResult> {
        // Execute cm test command
        const terminal = vscode.window.createTerminal({
            name: `Test: ${test.label}`,
            hideFromUser: true,
        });

        terminal.sendText(`cm test ${test.id}`);

        // Parse test output
        // ...

        return { success: true, duration: 100 };
    }
}
```

## 拡張機能設定

### settings.json サポート

```json
// .vscode/settings.json
{
  "cm.path": "/usr/local/bin/cm",
  "cm.lsp.enable": true,
  "cm.format.indentSize": 4,
  "cm.lint.enable": true,
  "cm.inlayHints.typeHints": true,
  "cm.inlayHints.parameterHints": true,
  "[cm]": {
    "editor.defaultFormatter": "cm-lang.vscode-cm",
    "editor.formatOnSave": true,
    "editor.semanticHighlighting.enabled": true,
    "editor.inlayHints.enabled": "on"
  }
}
```

## テーマサポート

```json
// themes/cm-dark.json
{
  "name": "Cm Dark",
  "type": "dark",
  "colors": {
    "editor.background": "#1e1e1e",
    "editor.foreground": "#d4d4d4"
  },
  "tokenColors": [
    {
      "scope": ["keyword.control.cm", "keyword.other.cm"],
      "settings": {
        "foreground": "#C586C0"
      }
    },
    {
      "scope": "entity.name.function.cm",
      "settings": {
        "foreground": "#DCDCAA"
      }
    },
    {
      "scope": "entity.name.type.cm",
      "settings": {
        "foreground": "#4EC9B0"
      }
    },
    {
      "scope": "string.quoted.double.cm",
      "settings": {
        "foreground": "#CE9178"
      }
    },
    {
      "scope": "comment",
      "settings": {
        "foreground": "#6A9955"
      }
    },
    {
      "scope": "constant.numeric.cm",
      "settings": {
        "foreground": "#B5CEA8"
      }
    }
  ]
}
```

## マーケットプレイス公開

### 公開設定

```yaml
# .github/workflows/publish.yml
name: Publish Extension

on:
  push:
    tags:
      - 'v*'

jobs:
  publish:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3

      - name: Setup Node.js
        uses: actions/setup-node@v3
        with:
          node-version: '18'

      - name: Install dependencies
        run: npm ci

      - name: Package extension
        run: npm run package

      - name: Publish to VSCode Marketplace
        run: npx vsce publish -p ${{ secrets.VSCE_TOKEN }}

      - name: Publish to Open VSX
        run: npx ovsx publish -p ${{ secrets.OVSX_TOKEN }}
```

## パフォーマンス最適化

```typescript
// Lazy loading and caching
class LazyLanguageClient {
    private client: LanguageClient | undefined;
    private startPromise: Promise<void> | undefined;

    async getClient(): Promise<LanguageClient> {
        if (!this.client) {
            if (!this.startPromise) {
                this.startPromise = this.start();
            }
            await this.startPromise;
        }
        return this.client!;
    }

    private async start(): Promise<void> {
        // Start language server only when needed
        this.client = new LanguageClient(/* ... */);
        await this.client.start();
    }
}
```

## 実装優先順位

1. **Phase 1** (v0.12.0): 基本機能（シンタックス、LSP統合、基本デバッグ）
2. **Phase 2** (v0.13.0): 高度な機能（テストランナー、リファクタリング）
3. **Phase 3** (v0.14.0): プロファイリング、ビジュアライゼーション