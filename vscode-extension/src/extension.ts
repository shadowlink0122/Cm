// ============================================================
// Cm言語拡張クライアント - LSPサーバー起動・非アクティブコードのトーンダウン表示
// ============================================================
// ホバー・定義ジャンプ・シンボル一覧はLSPサーバー（server/main.ts）が提供する。本ファイルはサーバーの起動・停止と、LSPに無いエディタ装飾（非アクティブコードの減光）のみを担う。

import * as vscode from 'vscode';
import {
  LanguageClient,
  LanguageClientOptions,
  ServerOptions,
  TransportKind,
} from 'vscode-languageclient/node';
import { findInactiveRanges } from './inactiveCode';
import { SERVER_MODULE_PATH } from './paths';

export { findInactiveRanges };

let client: LanguageClient | undefined;

// LSPサーバーを起動する（.cmを開いたときに有効化され、ファイルウォッチの通知も中継する）
function startLanguageClient(context: vscode.ExtensionContext): void {
  const serverModule = context.asAbsolutePath(SERVER_MODULE_PATH);
  const serverOptions: ServerOptions = {
    run: { module: serverModule, transport: TransportKind.ipc },
    debug: {
      module: serverModule,
      transport: TransportKind.ipc,
      options: { execArgv: ['--nolazy', '--inspect=6009'] },
    },
  };
  const clientOptions: LanguageClientOptions = {
    documentSelector: [{ language: 'cm' }],
    synchronize: {
      fileEvents: vscode.workspace.createFileSystemWatcher('**/*.cm'),
    },
  };
  client = new LanguageClient(
    'cmLanguageServer',
    'Cm Language Server',
    serverOptions,
    clientOptions,
  );
  void client.start();
}

// 明らかに無効なスコープ（未定義シンボルの#ifdef等）の減光はLSPに対応概念が無いためクライアント側で行う
function registerInactiveCodeDimming(context: vscode.ExtensionContext): void {
  const decoration = vscode.window.createTextEditorDecorationType({
    opacity: '0.45',
  });

  let timer: ReturnType<typeof setTimeout> | undefined;

  const update = (editor: vscode.TextEditor | undefined) => {
    if (!editor || editor.document.languageId !== 'cm') {
      return;
    }
    const config = vscode.workspace.getConfiguration('cm');
    if (!config.get<boolean>('dimInactiveCode', true)) {
      editor.setDecorations(decoration, []);
      return;
    }
    const activeDefines = config.get<string[]>('activeDefines', []);
    const ranges = findInactiveRanges(editor.document.getText(), activeDefines).map(
      (r) => new vscode.Range(r.startLine, r.startChar, r.endLine, r.endChar),
    );
    editor.setDecorations(decoration, ranges);
  };

  const scheduleUpdate = (editor: vscode.TextEditor | undefined) => {
    if (timer) {
      clearTimeout(timer);
    }
    timer = setTimeout(() => update(editor), 200);
  };

  context.subscriptions.push(
    decoration,
    vscode.window.onDidChangeActiveTextEditor((e: vscode.TextEditor | undefined) => update(e)),
    vscode.workspace.onDidChangeTextDocument((e: vscode.TextDocumentChangeEvent) => {
      const editor = vscode.window.activeTextEditor;
      if (editor && e.document === editor.document) {
        scheduleUpdate(editor);
      }
    }),
    vscode.workspace.onDidChangeConfiguration((e: vscode.ConfigurationChangeEvent) => {
      if (e.affectsConfiguration('cm')) {
        update(vscode.window.activeTextEditor);
      }
    }),
  );

  update(vscode.window.activeTextEditor);
}

export function activate(context: vscode.ExtensionContext): void {
  startLanguageClient(context);
  registerInactiveCodeDimming(context);
}

export function deactivate(): Thenable<void> | undefined {
  const stopping = client?.stop();
  client = undefined;
  return stopping;
}
