// ============================================================
// 候補絞り込み（server/scope.ts）のユニットテスト
// ============================================================
// 呼び出し文脈ゲートとファイル到達性による絞り込みをVSCode API非依存で検証する。実行: npm test

import { strict as assert } from 'node:assert';
import * as path from 'node:path';
import { test } from 'node:test';
import { pathToFileURL } from 'node:url';
import { CmSymbolKind } from '../navigation/symbols';
import { importedUris, scopeByAccess, scopeByReachability } from '../server/scope';

interface Entry {
  uri: string;
  symbol: { kind: CmSymbolKind };
}

function entry(uri: string, kind: CmSymbolKind): Entry {
  return { uri, symbol: { kind } };
}

const uriOf = (rel: string): string => pathToFileURL(path.resolve('/ws', rel)).toString();

test('scopeByAccess: 単独呼び出しはトップレベル宣言のみ、メソッドを除外する', () => {
  const entries = [
    entry(uriOf('a.cm'), 'function'),
    entry(uriOf('b.cm'), 'method'),
    entry(uriOf('c.cm'), 'method'),
  ];
  const scoped = scopeByAccess(entries, 'bare');
  assert.equal(scoped.length, 1);
  assert.equal(scoped[0].symbol.kind, 'function');
});

test('scopeByAccess: メンバアクセスはメソッド・フィールド・列挙子のみ', () => {
  const entries = [
    entry(uriOf('a.cm'), 'function'),
    entry(uriOf('b.cm'), 'method'),
    entry(uriOf('c.cm'), 'field'),
  ];
  const scoped = scopeByAccess(entries, 'member');
  assert.deepEqual(
    scoped.map((e) => e.symbol.kind),
    ['method', 'field'],
  );
});

test('scopeByAccess: 単独呼び出しでトップレベルが無ければ全件フォールバック', () => {
  const entries = [entry(uriOf('a.cm'), 'method'), entry(uriOf('b.cm'), 'variant')];
  const scoped = scopeByAccess(entries, 'bare');
  assert.equal(scoped.length, 2);
});

test('scopeByAccess: 修飾アクセスは種別で絞らない', () => {
  const entries = [entry(uriOf('a.cm'), 'function'), entry(uriOf('b.cm'), 'method')];
  assert.equal(scopeByAccess(entries, 'qualified').length, 2);
});

test('importedUris: 相対パスimportを.cmファイルURIへ解決し、名前空間importは無視する', () => {
  const docUri = uriOf('pkg/main.cm');
  const text = [
    'import ./util;',
    'import ./sub/helper::{a, b};',
    'import ../shared/const::*;',
    'import ./math as M;',
    'import std::io::println;', // 名前空間import → 対象外
  ].join('\n');
  const got = importedUris(text, docUri);
  assert.ok(got.has(uriOf('pkg/util.cm')), 'util.cm');
  assert.ok(got.has(uriOf('pkg/sub/helper.cm')), 'sub/helper.cm');
  assert.ok(got.has(uriOf('shared/const.cm')), '../shared/const.cm');
  assert.ok(got.has(uriOf('pkg/math.cm')), 'math.cm (as別名)');
  assert.equal(got.size, 4, '名前空間importは含めない');
});

test('scopeByReachability: 同一ファイル定義がワークスペース全体を遮蔽する', () => {
  const docUri = uriOf('pkg/main.cm');
  const entries = [
    entry(docUri, 'function'), // 同一ファイル
    entry(uriOf('other/a.cm'), 'function'),
    entry(uriOf('other/b.cm'), 'function'),
  ];
  const scoped = scopeByReachability(entries, docUri, '');
  assert.equal(scoped.length, 1);
  assert.equal(scoped[0].uri, docUri);
});

test('scopeByReachability: 同一ファイルに無ければimport先を優先する', () => {
  const docUri = uriOf('pkg/main.cm');
  const text = 'import ./util;';
  const entries = [
    entry(uriOf('pkg/util.cm'), 'function'), // import先
    entry(uriOf('unrelated/x.cm'), 'function'),
    entry(uriOf('unrelated/y.cm'), 'function'),
  ];
  const scoped = scopeByReachability(entries, docUri, text);
  assert.equal(scoped.length, 1);
  assert.ok(scoped[0].uri.endsWith('pkg/util.cm'));
});

test('scopeByReachability: 同一ファイルにもimport先にも無ければ全件フォールバック', () => {
  const docUri = uriOf('pkg/main.cm');
  const entries = [entry(uriOf('a/x.cm'), 'function'), entry(uriOf('b/y.cm'), 'function')];
  const scoped = scopeByReachability(entries, docUri, 'import ./nowhere;');
  assert.equal(scoped.length, 2);
});
