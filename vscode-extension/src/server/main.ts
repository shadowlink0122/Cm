// ============================================================
// Cm言語LSPサーバー - ホバー・定義ジャンプ・シンボル一覧
// ============================================================
// シンボル抽出はnavigation/symbols.ts（エディタ非依存の正規表現インデックス）を再利用し、本ファイルはLSPプロトコルへの橋渡し（初期化・ドキュメント同期・各リクエスト応答）を担う。

import * as path from 'node:path';
import { fileURLToPath } from 'node:url';
import {
  createConnection,
  DocumentSymbol,
  FileChangeType,
  Hover,
  InitializeParams,
  InitializeResult,
  Location,
  MarkupKind,
  ProposedFeatures,
  SymbolInformation,
  SymbolKind,
  TextDocuments,
  TextDocumentSyncKind,
} from 'vscode-languageserver/node';
import { TextDocument } from 'vscode-languageserver-textdocument';
import { BuiltinEntry, lookupBuiltinFunction, lookupBuiltinMethod } from '../navigation/builtins';
import { CmSymbol, extractSymbols, rankMatches } from '../navigation/symbols';
import { IndexedSymbol, WorkspaceIndex } from './indexer';
import { scopeByAccess, scopeByReachability } from './scope';
import { classifyAccess, isMethodAccess, isSkippedWord, wordAtOffset } from './words';

const HOVER_MAX_ENTRIES = 3;
const DEFINITION_MAX_ENTRIES = 20;
const WORKSPACE_SYMBOL_MAX_ENTRIES = 500;
const REINDEX_DEBOUNCE_MS = 500;

const connection = createConnection(ProposedFeatures.all);
const documents = new TextDocuments(TextDocument);
const index = new WorkspaceIndex();

connection.onInitialize((params: InitializeParams): InitializeResult => {
  const roots: string[] = [];
  for (const folder of params.workspaceFolders ?? []) {
    roots.push(fileURLToPath(folder.uri));
  }
  if (roots.length === 0 && params.rootUri) {
    roots.push(fileURLToPath(params.rootUri));
  }
  index.setRoots(roots);
  return {
    capabilities: {
      textDocumentSync: TextDocumentSyncKind.Incremental,
      hoverProvider: true,
      definitionProvider: true,
      documentSymbolProvider: true,
      workspaceSymbolProvider: true,
    },
  };
});

connection.onInitialized(() => {
  // 初回走査を先行して開始する（各リクエストはensureBuiltで完了を待つ）
  void index.ensureBuilt();
});

// 開いているドキュメントの編集を反映する（保存不要・500msデバウンス）
const pendingTimers = new Map<string, ReturnType<typeof setTimeout>>();
documents.onDidChangeContent((event) => {
  const key = event.document.uri;
  const timer = pendingTimers.get(key);
  if (timer) {
    clearTimeout(timer);
  }
  pendingTimers.set(
    key,
    setTimeout(() => {
      pendingTimers.delete(key);
      index.setFile(key, event.document.getText());
    }, REINDEX_DEBOUNCE_MS),
  );
});

// 閉じたドキュメントは未保存編集を破棄してディスク内容へ戻す
documents.onDidClose((event) => {
  const timer = pendingTimers.get(event.document.uri);
  if (timer) {
    clearTimeout(timer);
    pendingTimers.delete(event.document.uri);
  }
  void index.loadFromDisk(event.document.uri, fileURLToPath);
});

// ワークスペース上のファイル追加・変更・削除（クライアントのファイルウォッチ）を追従する
connection.onDidChangeWatchedFiles((params) => {
  for (const change of params.changes) {
    if (change.type === FileChangeType.Deleted) {
      index.removeFile(change.uri);
    } else if (!documents.get(change.uri)) {
      // 開いているドキュメントはdidChangeが最新のため、未オープンのものだけディスクから読む
      void index.loadFromDisk(change.uri, fileURLToPath);
    }
  }
});

// 現在のドキュメント内容でインデックスを更新した上で識別子の定義を検索する
interface WordQuery {
  word: string;
  start: number;
  entries: IndexedSymbol[];
}

async function findSymbols(document: TextDocument, offset: number): Promise<WordQuery | undefined> {
  const text = document.getText();
  const found = wordAtOffset(text, offset);
  if (!found || isSkippedWord(found.word)) {
    return undefined;
  }
  await index.ensureBuilt();
  index.setFile(document.uri, text);
  // 候補を「呼び出し文脈の種別」→「ファイル到達性」の順に絞り、同名シンボルの曖昧さを減らす
  const byAccess = scopeByAccess(index.lookup(found.word), classifyAccess(text, found.start));
  const scoped = scopeByReachability(byAccess, document.uri, text);
  const ranked = new Set(rankMatches(scoped.map((m) => m.symbol)));
  return {
    word: found.word,
    start: found.start,
    entries: scoped.filter((m) => ranked.has(m.symbol)),
  };
}

// ホバー表示用にワークスペースルートからの相対パスを組み立てる
function relativeLabel(uri: string): string {
  const filePath = fileURLToPath(uri);
  for (const root of index.getRoots()) {
    const relative = path.relative(root, filePath);
    if (!relative.startsWith('..') && !path.isAbsolute(relative)) {
      return relative;
    }
  }
  return path.basename(filePath);
}

function toLocation(entry: IndexedSymbol): Location {
  const { symbol } = entry;
  return Location.create(entry.uri, {
    start: { line: symbol.line, character: symbol.char },
    end: { line: symbol.line, character: symbol.char + symbol.name.length },
  });
}

function symbolLabel(sym: CmSymbol): string {
  return sym.container ? `${sym.container}::${sym.name}` : sym.name;
}

const SYMBOL_KIND_MAP: Record<CmSymbol['kind'], SymbolKind> = {
  struct: SymbolKind.Struct,
  enum: SymbolKind.Enum,
  interface: SymbolKind.Interface,
  union: SymbolKind.Struct,
  typedef: SymbolKind.TypeParameter,
  function: SymbolKind.Function,
  method: SymbolKind.Method,
  macro: SymbolKind.Constant,
  constant: SymbolKind.Constant,
  field: SymbolKind.Field,
  variant: SymbolKind.EnumMember,
  module: SymbolKind.Module,
};

// ユーザー定義シンボルのホバー内容（シグネチャ・ドキュメントコメント・定義位置）を組み立てる
function buildSymbolHover(entries: IndexedSymbol[]): string {
  const parts: string[] = [];
  for (const entry of entries.slice(0, HOVER_MAX_ENTRIES)) {
    parts.push('```cm\n' + entry.symbol.signature + '\n```');
    if (entry.symbol.doc) {
      parts.push(entry.symbol.doc.replace(/\n/g, '\n\n'));
    }
    parts.push(`*${relativeLabel(entry.uri)}:${entry.symbol.line + 1}*`);
  }
  if (entries.length > HOVER_MAX_ENTRIES) {
    parts.push(`*…他 ${entries.length - HOVER_MAX_ENTRIES} 件の定義*`);
  }
  return parts.join('\n\n');
}

// コンパイラ組み込みメソッド・関数のホバー内容を組み立てる（ソース定義を持たないためコードジャンプは提供しない）
function buildBuiltinHover(entries: BuiltinEntry[]): string {
  const parts: string[] = [];
  for (const entry of entries) {
    parts.push('```cm\n' + entry.signature + '\n```');
    parts.push(entry.doc);
    parts.push(`*コンパイラ組み込み（${entry.receiver}）*`);
  }
  return parts.join('\n\n');
}

function markdownHover(value: string): Hover {
  return { contents: { kind: MarkupKind.Markdown, value } };
}

connection.onHover(async (params): Promise<Hover | undefined> => {
  const document = documents.get(params.textDocument.uri);
  if (!document) {
    return undefined;
  }
  const offset = document.offsetAt(params.position);
  const query = await findSymbols(document, offset);
  if (!query) {
    return undefined;
  }
  if (query.entries.length > 0) {
    return markdownHover(buildSymbolHover(query.entries));
  }
  // ユーザー定義シンボルが無ければコンパイラ組み込みを探す
  if (isMethodAccess(document.getText(), query.start)) {
    const methods = lookupBuiltinMethod(query.word);
    if (methods.length > 0) {
      return markdownHover(buildBuiltinHover(methods));
    }
  } else {
    const fn = lookupBuiltinFunction(query.word);
    if (fn) {
      return markdownHover(buildBuiltinHover([fn]));
    }
  }
  return undefined;
});

connection.onDefinition(async (params): Promise<Location[]> => {
  const document = documents.get(params.textDocument.uri);
  if (!document) {
    return [];
  }
  const offset = document.offsetAt(params.position);
  const query = await findSymbols(document, offset);
  if (!query) {
    return [];
  }
  return query.entries.slice(0, DEFINITION_MAX_ENTRIES).map(toLocation);
});

connection.onDocumentSymbol((params): DocumentSymbol[] => {
  const document = documents.get(params.textDocument.uri);
  if (!document) {
    return [];
  }
  const text = document.getText();
  const lines = text.split('\n');
  const symbols = extractSymbols(text);
  index.setFile(document.uri, text);
  return symbols.map((sym) => {
    const fullRange = {
      start: { line: sym.line, character: 0 },
      end: { line: sym.endLine, character: lines[sym.endLine]?.length ?? 0 },
    };
    const selection = {
      start: { line: sym.line, character: sym.char },
      end: { line: sym.line, character: sym.char + sym.name.length },
    };
    return DocumentSymbol.create(
      symbolLabel(sym),
      sym.signature === sym.name ? undefined : sym.signature,
      SYMBOL_KIND_MAP[sym.kind],
      fullRange,
      selection,
    );
  });
});

connection.onWorkspaceSymbol(async (params): Promise<SymbolInformation[]> => {
  await index.ensureBuilt();
  const lower = params.query.toLowerCase();
  return index
    .allSymbols()
    .filter((entry) => entry.symbol.name.toLowerCase().includes(lower))
    .slice(0, WORKSPACE_SYMBOL_MAX_ENTRIES)
    .map((entry) =>
      SymbolInformation.create(
        symbolLabel(entry.symbol),
        SYMBOL_KIND_MAP[entry.symbol.kind],
        toLocation(entry).range,
        entry.uri,
        entry.symbol.container ?? '',
      ),
    );
});

documents.listen(connection);
connection.listen();
