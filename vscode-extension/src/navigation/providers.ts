// ============================================================
// ホバー・定義ジャンプ・アウトラインのプロバイダ登録
// ============================================================
// シンボル抽出はsymbols.ts（VSCode API非依存）が担い、本ファイルはワークスペースインデックスの維持とVSCode APIへの橋渡しのみを行う。

import * as vscode from 'vscode';
import {
  BOOLEAN_CONSTANTS,
  CONTROL_CONDITIONAL,
  CONTROL_EXCEPTION,
  CONTROL_EXPORT,
  CONTROL_FLOW,
  CONTROL_REPEAT,
  CONTROL_RETURN,
  DECLARATION_KEYWORDS,
  MODIFIER_KEYWORDS,
  NULL_CONSTANTS,
  PRIMITIVE_TYPES,
  SIZEOF_KEYWORDS,
} from '../grammar/terms';
import { BuiltinEntry, lookupBuiltinFunction, lookupBuiltinMethod } from './builtins';
import { CmSymbol, extractSymbols, rankMatches } from './symbols';

const WORD_PATTERN = /[A-Za-z_][A-Za-z0-9_]*/;
const INDEX_EXCLUDE = '{**/node_modules/**,**/.tmp/**,**/.git/**,**/build/**,**/out/**}';
const MAX_INDEX_FILES = 20000;
const HOVER_MAX_ENTRIES = 3;
const DEFINITION_MAX_ENTRIES = 20;

// キーワード・プリミティブ型はホバー対象外（言語仕様の説明までは提供しない）
const SKIP_WORDS = new Set<string>([
  ...PRIMITIVE_TYPES,
  ...BOOLEAN_CONSTANTS,
  ...NULL_CONSTANTS,
  ...CONTROL_CONDITIONAL,
  ...CONTROL_REPEAT,
  ...CONTROL_RETURN,
  ...CONTROL_FLOW,
  ...CONTROL_EXPORT,
  ...CONTROL_EXCEPTION,
  ...DECLARATION_KEYWORDS,
  ...MODIFIER_KEYWORDS,
  ...SIZEOF_KEYWORDS,
  'self',
]);

interface IndexedSymbol {
  uri: vscode.Uri;
  symbol: CmSymbol;
}

// ワークスペース全.cmファイルのシンボルインデックス（ファイル単位で差し替え可能）
class CmSymbolIndex {
  private readonly files = new Map<string, { uri: vscode.Uri; symbols: CmSymbol[] }>();
  private buildPromise: Promise<void> | undefined;

  // 初回参照時に一度だけワークスペース全体を走査する（以後はイベント駆動の差分更新）
  ensureBuilt(): Promise<void> {
    if (!this.buildPromise) {
      this.buildPromise = this.buildAll();
    }
    return this.buildPromise;
  }

  private async buildAll(): Promise<void> {
    const uris = await vscode.workspace.findFiles('**/*.cm', INDEX_EXCLUDE, MAX_INDEX_FILES);
    for (const uri of uris) {
      try {
        const bytes = await vscode.workspace.fs.readFile(uri);
        this.setFile(uri, Buffer.from(bytes).toString('utf8'));
      } catch {
        // 読み取り不能なファイルはインデックス対象外とする
      }
    }
  }

  setFile(uri: vscode.Uri, text: string): void {
    this.files.set(uri.toString(), { uri, symbols: extractSymbols(text) });
  }

  removeFile(uri: vscode.Uri): void {
    this.files.delete(uri.toString());
  }

  lookup(name: string): IndexedSymbol[] {
    const result: IndexedSymbol[] = [];
    for (const entry of this.files.values()) {
      for (const symbol of entry.symbols) {
        if (symbol.name === name) {
          result.push({ uri: entry.uri, symbol });
        }
      }
    }
    return result;
  }

  allSymbols(): IndexedSymbol[] {
    const result: IndexedSymbol[] = [];
    for (const entry of this.files.values()) {
      for (const symbol of entry.symbols) {
        result.push({ uri: entry.uri, symbol });
      }
    }
    return result;
  }
}

// カーソル位置の識別子を取り出す（キーワード・プリミティブ型は対象外）
function wordAtPosition(
  document: vscode.TextDocument,
  position: vscode.Position,
): string | undefined {
  const range = document.getWordRangeAtPosition(position, WORD_PATTERN);
  if (!range) {
    return undefined;
  }
  const word = document.getText(range);
  return SKIP_WORDS.has(word) ? undefined : word;
}

// カーソル位置の識別子が直前の `.` を伴うメソッドアクセスか判定する（`obj.method` の method 側）
function isMethodAccess(document: vscode.TextDocument, position: vscode.Position): boolean {
  const range = document.getWordRangeAtPosition(position, WORD_PATTERN);
  if (!range || range.start.character === 0) {
    return false;
  }
  const before = document.getText(new vscode.Range(range.start.translate(0, -1), range.start));
  return before === '.';
}

// コンパイラ組み込みメソッド・関数のホバー内容を組み立てる（ソース定義を持たないためコードジャンプは提供しない）
function buildBuiltinHover(entries: BuiltinEntry[]): vscode.MarkdownString {
  const md = new vscode.MarkdownString();
  for (const entry of entries) {
    md.appendCodeblock(entry.signature, 'cm');
    md.appendMarkdown(`${entry.doc}\n\n`);
    md.appendMarkdown(`*コンパイラ組み込み（${entry.receiver}）*\n\n`);
  }
  return md;
}

// 現在のドキュメント内容でインデックスを更新した上で識別子の定義を検索する
async function findSymbols(
  index: CmSymbolIndex,
  document: vscode.TextDocument,
  position: vscode.Position,
): Promise<IndexedSymbol[]> {
  const word = wordAtPosition(document, position);
  if (!word) {
    return [];
  }
  await index.ensureBuilt();
  index.setFile(document.uri, document.getText());
  const entries = index.lookup(word);
  const ranked = new Set(rankMatches(entries.map((m) => m.symbol)));
  return entries.filter((m) => ranked.has(m.symbol));
}

function symbolLabel(sym: CmSymbol): string {
  return sym.container ? `${sym.container}::${sym.name}` : sym.name;
}

const SYMBOL_KIND_MAP: Record<CmSymbol['kind'], vscode.SymbolKind> = {
  struct: vscode.SymbolKind.Struct,
  enum: vscode.SymbolKind.Enum,
  interface: vscode.SymbolKind.Interface,
  union: vscode.SymbolKind.Struct,
  typedef: vscode.SymbolKind.TypeParameter,
  function: vscode.SymbolKind.Function,
  method: vscode.SymbolKind.Method,
  macro: vscode.SymbolKind.Constant,
  constant: vscode.SymbolKind.Constant,
  field: vscode.SymbolKind.Field,
  variant: vscode.SymbolKind.EnumMember,
  module: vscode.SymbolKind.Module,
};

function toLocation(entry: IndexedSymbol): vscode.Location {
  const { symbol } = entry;
  const start = new vscode.Position(symbol.line, symbol.char);
  return new vscode.Location(
    entry.uri,
    new vscode.Range(start, start.translate(0, symbol.name.length)),
  );
}

export function registerNavigation(context: vscode.ExtensionContext): void {
  const index = new CmSymbolIndex();
  const selector: vscode.DocumentSelector = { language: 'cm' };

  // 開いているドキュメントの編集を反映する（保存不要・500msデバウンス）
  const pendingTimers = new Map<string, ReturnType<typeof setTimeout>>();
  const scheduleReindex = (document: vscode.TextDocument) => {
    if (document.languageId !== 'cm') {
      return;
    }
    const key = document.uri.toString();
    const timer = pendingTimers.get(key);
    if (timer) {
      clearTimeout(timer);
    }
    pendingTimers.set(
      key,
      setTimeout(() => {
        pendingTimers.delete(key);
        index.setFile(document.uri, document.getText());
      }, 500),
    );
  };

  // ワークスペース上のファイル追加・変更・削除を追従する
  const watcher = vscode.workspace.createFileSystemWatcher('**/*.cm');
  const reindexFromDisk = async (uri: vscode.Uri) => {
    try {
      const bytes = await vscode.workspace.fs.readFile(uri);
      index.setFile(uri, Buffer.from(bytes).toString('utf8'));
    } catch {
      index.removeFile(uri);
    }
  };

  context.subscriptions.push(
    watcher,
    watcher.onDidCreate(reindexFromDisk),
    watcher.onDidChange(reindexFromDisk),
    watcher.onDidDelete((uri) => index.removeFile(uri)),
    vscode.workspace.onDidChangeTextDocument((e) => scheduleReindex(e.document)),

    vscode.languages.registerHoverProvider(selector, {
      async provideHover(document, position) {
        const entries = await findSymbols(index, document, position);
        if (entries.length === 0) {
          // ユーザー定義シンボルが無ければコンパイラ組み込みを探す（あらかじめ共有された定義として表示）
          const word = wordAtPosition(document, position);
          if (word) {
            if (isMethodAccess(document, position)) {
              const methods = lookupBuiltinMethod(word);
              if (methods.length > 0) {
                return new vscode.Hover(buildBuiltinHover(methods));
              }
            } else {
              const fn = lookupBuiltinFunction(word);
              if (fn) {
                return new vscode.Hover(buildBuiltinHover([fn]));
              }
            }
          }
          return undefined;
        }
        const md = new vscode.MarkdownString();
        for (const entry of entries.slice(0, HOVER_MAX_ENTRIES)) {
          md.appendCodeblock(entry.symbol.signature, 'cm');
          if (entry.symbol.doc) {
            md.appendMarkdown(`${entry.symbol.doc.replace(/\n/g, '\n\n')}\n\n`);
          }
          md.appendMarkdown(
            `*${vscode.workspace.asRelativePath(entry.uri)}:${entry.symbol.line + 1}*\n\n`,
          );
        }
        if (entries.length > HOVER_MAX_ENTRIES) {
          md.appendMarkdown(`*…他 ${entries.length - HOVER_MAX_ENTRIES} 件の定義*`);
        }
        return new vscode.Hover(md);
      },
    }),

    vscode.languages.registerDefinitionProvider(selector, {
      async provideDefinition(document, position) {
        const entries = await findSymbols(index, document, position);
        return entries.slice(0, DEFINITION_MAX_ENTRIES).map(toLocation);
      },
    }),

    vscode.languages.registerDocumentSymbolProvider(selector, {
      provideDocumentSymbols(document) {
        const symbols = extractSymbols(document.getText());
        index.setFile(document.uri, document.getText());
        return symbols.map((sym) => {
          const fullRange = new vscode.Range(
            sym.line,
            0,
            sym.endLine,
            document.lineAt(sym.endLine).text.length,
          );
          const selection = new vscode.Range(
            sym.line,
            sym.char,
            sym.line,
            sym.char + sym.name.length,
          );
          return new vscode.DocumentSymbol(
            symbolLabel(sym),
            sym.signature === sym.name ? '' : sym.signature,
            SYMBOL_KIND_MAP[sym.kind],
            fullRange,
            selection,
          );
        });
      },
    }),

    vscode.languages.registerWorkspaceSymbolProvider({
      async provideWorkspaceSymbols(query) {
        await index.ensureBuilt();
        const lower = query.toLowerCase();
        return index
          .allSymbols()
          .filter((entry) => entry.symbol.name.toLowerCase().includes(lower))
          .slice(0, 500)
          .map(
            (entry) =>
              new vscode.SymbolInformation(
                symbolLabel(entry.symbol),
                SYMBOL_KIND_MAP[entry.symbol.kind],
                entry.symbol.container ?? '',
                toLocation(entry),
              ),
          );
      },
    }),
  );
}
