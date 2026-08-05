// ============================================================
// Cm言語拡張ランタイム - 非アクティブコードのトーンダウン表示・コードナビゲーション
// ============================================================
// 判定ロジックはinactiveCode.ts / navigation/symbols.ts（いずれもVSCode API非依存）にあり、本ファイルはエディタ連携（デコレーション適用・イベント購読・プロバイダ登録）のみを担う。

import * as vscode from 'vscode';
import { findInactiveRanges } from './inactiveCode';
import { registerNavigation } from './navigation/providers';

export { findInactiveRanges };

export function activate(context: vscode.ExtensionContext): void {
  registerNavigation(context);
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

export function deactivate(): void {
  // 破棄処理は subscriptions に委譲
}
