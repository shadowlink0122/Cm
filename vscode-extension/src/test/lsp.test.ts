// ============================================================
// LSPサーバーのE2Eテスト（実バンドルをstdioで起動して検証）
// ============================================================
// vsixに同梱されるdist/server/main.jsそのものを子プロセスとして起動し、initialize→didOpen→hover/definition/シンボル一覧の応答をLSPプロトコル越しに検証する。実行: npm test（build:bundle後に走る）

import { strict as assert } from 'node:assert';
import { ChildProcess, spawn } from 'node:child_process';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import * as os from 'node:os';
import * as path from 'node:path';
import { after, before, test } from 'node:test';
import { pathToFileURL } from 'node:url';
import {
  createMessageConnection,
  MessageConnection,
  StreamMessageReader,
  StreamMessageWriter,
} from 'vscode-jsonrpc/node';
import { SERVER_MODULE_PATH } from '../paths';

const EXT_ROOT = path.resolve(__dirname, '..', '..', '..');

// ワークスペースフィクスチャ: 定義側（defs.cm）と参照側（main.cm）を分け、クロスファイルのインデックスを検証する
const DEFS_CM = [
  '/// 2次元の点',
  'struct Point {',
  '    int x;',
  '    int y;',
  '}',
  '',
  '/// 点を生成する',
  'Point make_point(int x, int y) {',
  '    Point p;',
  '    return p;',
  '}',
].join('\n');

const MAIN_CM = [
  'int main() {',
  '    Point p = make_point(1, 2);',
  '    println(p.x);',
  '    return 0;',
  '}',
].join('\n');

let workspaceDir: string;
let server: ChildProcess;
let connection: MessageConnection;
let mainUri: string;

// main.cm内の識別子出現位置を{line, character}で求める（テスト側の位置ハードコードを避ける)
function positionOf(word: string): { line: number; character: number } {
  const lines = MAIN_CM.split('\n');
  for (let line = 0; line < lines.length; line++) {
    const character = lines[line].indexOf(word);
    if (character >= 0) {
      return { line, character: character + 1 };
    }
  }
  throw new Error(`fixture内に ${word} が見つからない`);
}

interface HoverResult {
  contents: { kind: string; value: string };
}

interface LocationResult {
  uri: string;
  range: { start: { line: number; character: number } };
}

before(async () => {
  workspaceDir = await mkdtemp(path.join(os.tmpdir(), 'cm-lsp-test-'));
  await writeFile(path.join(workspaceDir, 'defs.cm'), DEFS_CM);
  await writeFile(path.join(workspaceDir, 'main.cm'), MAIN_CM);

  // vsix同梱物そのもの（バンドル）を起動する
  server = spawn(process.execPath, [path.join(EXT_ROOT, SERVER_MODULE_PATH), '--stdio'], {
    cwd: EXT_ROOT,
    stdio: ['pipe', 'pipe', 'inherit'],
  });
  connection = createMessageConnection(
    new StreamMessageReader(server.stdout!),
    new StreamMessageWriter(server.stdin!),
  );
  connection.listen();

  const workspaceUri = pathToFileURL(workspaceDir).toString();
  await connection.sendRequest('initialize', {
    processId: process.pid,
    rootUri: workspaceUri,
    workspaceFolders: [{ uri: workspaceUri, name: 'fixture' }],
    capabilities: {},
  });
  await connection.sendNotification('initialized', {});

  mainUri = pathToFileURL(path.join(workspaceDir, 'main.cm')).toString();
  await connection.sendNotification('textDocument/didOpen', {
    textDocument: { uri: mainUri, languageId: 'cm', version: 1, text: MAIN_CM },
  });
});

after(async () => {
  try {
    await connection.sendRequest('shutdown');
    await connection.sendNotification('exit');
  } catch {
    // サーバーが既に落ちていても後始末は続行する
  }
  connection.dispose();
  server.kill();
  await rm(workspaceDir, { recursive: true, force: true });
});

test('ホバー: 未オープンの別ファイルで定義されたstructのシグネチャとdocを表示する', async () => {
  const hover = (await connection.sendRequest('textDocument/hover', {
    textDocument: { uri: mainUri },
    position: positionOf('Point'),
  })) as HoverResult;
  assert.ok(hover, 'hoverがnullでない');
  assert.equal(hover.contents.kind, 'markdown');
  assert.ok(hover.contents.value.includes('struct Point'), 'シグネチャを含む');
  assert.ok(hover.contents.value.includes('2次元の点'), 'docコメントを含む');
  assert.ok(hover.contents.value.includes('defs.cm:2'), '定義位置を含む');
});

test('定義ジャンプ: クロスファイルの関数定義位置を返す', async () => {
  const locations = (await connection.sendRequest('textDocument/definition', {
    textDocument: { uri: mainUri },
    position: positionOf('make_point'),
  })) as LocationResult[];
  assert.equal(locations.length, 1);
  assert.ok(locations[0].uri.endsWith('defs.cm'));
  // defs.cm内で `Point make_point(...)` は0-basedで7行目
  assert.equal(locations[0].range.start.line, 7);
  assert.equal(locations[0].range.start.character, 'Point '.length);
});

test('ホバー: コンパイラ組み込み関数はビルトイン情報を表示する', async () => {
  const hover = (await connection.sendRequest('textDocument/hover', {
    textDocument: { uri: mainUri },
    position: positionOf('println'),
  })) as HoverResult;
  assert.ok(hover, 'hoverがnullでない');
  assert.ok(hover.contents.value.includes('println'), 'シグネチャを含む');
  assert.ok(hover.contents.value.includes('コンパイラ組み込み'), 'ビルトイン表記を含む');
});

test('ホバー: キーワード上では応答しない', async () => {
  const hover = await connection.sendRequest('textDocument/hover', {
    textDocument: { uri: mainUri },
    position: positionOf('return'),
  });
  assert.equal(hover, null);
});

test('編集の即時反映: didChange直後の新シンボルへジャンプできる', async () => {
  const edited = `${MAIN_CM}\n\nstruct Fresh {\n    int v;\n}\nFresh f;\n`;
  await connection.sendNotification('textDocument/didChange', {
    textDocument: { uri: mainUri, version: 2 },
    contentChanges: [{ text: edited }],
  });
  const lines = edited.split('\n');
  const useLine = lines.findIndex((l) => l.startsWith('Fresh f;'));
  const locations = (await connection.sendRequest('textDocument/definition', {
    textDocument: { uri: mainUri },
    position: { line: useLine, character: 1 },
  })) as LocationResult[];
  assert.equal(locations.length, 1);
  assert.equal(
    locations[0].range.start.line,
    lines.findIndex((l) => l.startsWith('struct Fresh')),
  );
});

test('定義ジャンプ: 単独呼び出し write() は同名メソッドを除外しトップレベル関数のみ返す', async () => {
  const lines = [
    'struct Writer {',
    '}',
    'impl Writer {',
    '    void write(int b) {',
    '    }',
    '}',
    'void write(int b) {',
    '}',
    'int main() {',
    '    Writer w;',
    '    w.write(1);',
    '    write(2);',
    '    return 0;',
    '}',
  ];
  const edited = lines.join('\n');
  await connection.sendNotification('textDocument/didChange', {
    textDocument: { uri: mainUri, version: 3 },
    contentChanges: [{ text: edited }],
  });
  const methodLine = lines.findIndex((l) => l.includes('void write') && l.startsWith('    '));
  const funcLine = lines.findIndex((l) => l.startsWith('void write'));

  // 単独呼び出し `write(2)` はトップレベル関数のみ
  const bareCallLine = lines.findIndex((l) => l.includes('write(2)'));
  const bare = (await connection.sendRequest('textDocument/definition', {
    textDocument: { uri: mainUri },
    position: { line: bareCallLine, character: lines[bareCallLine].indexOf('write') + 1 },
  })) as LocationResult[];
  assert.equal(bare.length, 1, 'トップレベル関数1件のみ');
  assert.equal(bare[0].range.start.line, funcLine);

  // メンバアクセス `w.write(1)` はメソッドのみ
  const memberCallLine = lines.findIndex((l) => l.includes('w.write(1)'));
  const member = (await connection.sendRequest('textDocument/definition', {
    textDocument: { uri: mainUri },
    position: { line: memberCallLine, character: lines[memberCallLine].indexOf('write') + 1 },
  })) as LocationResult[];
  assert.equal(member.length, 1, 'メソッド1件のみ');
  assert.equal(member[0].range.start.line, methodLine);
});

test('定義ジャンプ: 同名関数がある場合は同一ファイルの定義を優先し一意化する', async () => {
  // defs.cm にも make_point があるが、ローカル定義を持つファイルではローカルを優先する
  const lines = [
    'Point make_point(int x, int y) {',
    '    Point p;',
    '    return p;',
    '}',
    'int main() {',
    '    Point q = make_point(3, 4);',
    '    return 0;',
    '}',
  ];
  const edited = lines.join('\n');
  await connection.sendNotification('textDocument/didChange', {
    textDocument: { uri: mainUri, version: 4 },
    contentChanges: [{ text: edited }],
  });
  const callLine = lines.findIndex((l) => l.includes('make_point(3, 4)'));
  const localDefLine = lines.findIndex((l) => l.startsWith('Point make_point'));
  const locations = (await connection.sendRequest('textDocument/definition', {
    textDocument: { uri: mainUri },
    position: { line: callLine, character: lines[callLine].indexOf('make_point') + 1 },
  })) as LocationResult[];
  assert.equal(locations.length, 1, '同一ファイルの1件のみ（defs.cmの同名は除外）');
  assert.ok(locations[0].uri.endsWith('main.cm'));
  assert.equal(locations[0].range.start.line, localDefLine);
});

test('ドキュメントシンボル: アウトライン用の一覧を返す', async () => {
  const symbols = (await connection.sendRequest('textDocument/documentSymbol', {
    textDocument: { uri: mainUri },
  })) as { name: string }[];
  const names = symbols.map((s) => s.name);
  assert.ok(names.includes('main'), `mainを含む: ${names.join(', ')}`);
});

test('ワークスペースシンボル: 未オープンファイルのシンボルを検索できる', async () => {
  const symbols = (await connection.sendRequest('workspace/symbol', {
    query: 'point',
  })) as { name: string; location: { uri: string } }[];
  const point = symbols.find((s) => s.name === 'Point');
  assert.ok(point, 'Pointがヒットする');
  assert.ok(point.location.uri.endsWith('defs.cm'));
});
