// ============================================================
// import/export文のキーワード着色（TextMateトークン化）テスト
// ============================================================
// 実エンジンでトークン化し、as/fromがモジュール名（緑=entity.name.type.module）ではなく
// C++/Rustの修飾子と同じスコープ（storage.modifier）で着色されることを検証する。

import { strict as assert } from 'node:assert';
import { test } from 'node:test';
import { loadGrammar, tokensWithScope } from './tokenize';

const PRIMITIVE = 'storage.modifier.cm';
const MODULE = 'entity.name.type.module.cm';

test('import * as M: as は修飾子スコープ(storage.modifier)、モジュール名の緑に拾われない', async () => {
  const grammar = await loadGrammar();
  const line = 'import * as M;';
  assert.deepEqual(tokensWithScope(grammar, line, PRIMITIVE), ['as']);
  assert.ok(!tokensWithScope(grammar, line, MODULE).includes('as'), 'as が緑になってはいけない');
  assert.ok(tokensWithScope(grammar, line, MODULE).includes('M'), 'M はモジュール名(緑)のまま');
});

test('import ./math as M: エイリアスの as も青', async () => {
  const grammar = await loadGrammar();
  assert.deepEqual(tokensWithScope(grammar, 'import ./math as M;', PRIMITIVE), ['as']);
});

test('選択import {compute as compute_b} 内の as も青', async () => {
  const grammar = await loadGrammar();
  const line = 'import ./m::{compute as compute_b};';
  assert.deepEqual(tokensWithScope(grammar, line, PRIMITIVE), ['as']);
});

test('export * from mod: from は修飾子スコープ(storage.modifier)', async () => {
  const grammar = await loadGrammar();
  assert.deepEqual(tokensWithScope(grammar, 'export * from mod;', PRIMITIVE), ['export', 'from']);
});
