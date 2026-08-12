// ============================================================
// 文字列エスケープハイライトのトークナイズテスト
// ============================================================
// R5（文字列エスケープの実装と診断）とハイライトの整合を検証する:
// 対応エスケープ（\a含む）はエスケープ色、未知のエスケープは不正色になる。

import * as assert from 'node:assert/strict';
import { test } from 'node:test';
import { loadGrammar, tokensWithScope } from './tokenize';

const ESCAPE_SCOPE = 'constant.character.escape.cm';
const INVALID_SCOPE = 'invalid.illegal.escape.cm';

test('対応エスケープ（\\a含むC標準系・\\xHH・\\uHHHH）はエスケープ色になる', async () => {
  const grammar = await loadGrammar();
  const line = 'string s = "\\a\\b\\f\\v\\n\\t\\x41\\u3042";';
  const escapes = tokensWithScope(grammar, line, ESCAPE_SCOPE);
  assert.deepEqual(
    escapes,
    ['\\a', '\\b', '\\f', '\\v', '\\n', '\\t', '\\x41', '\\u3042'],
    `エスケープトークン: ${JSON.stringify(escapes)}`,
  );
  assert.equal(tokensWithScope(grammar, line, INVALID_SCOPE).length, 0);
});

test('未知のエスケープはコンパイラ診断と整合する不正色になる', async () => {
  const grammar = await loadGrammar();
  const line = 'string s = "\\q\\z";';
  const invalid = tokensWithScope(grammar, line, INVALID_SCOPE);
  assert.deepEqual(invalid, ['\\q', '\\z'], `不正エスケープ: ${JSON.stringify(invalid)}`);
});

test('raw文字列（バッククォート）はデリミタのエスケープ \\` のみエスケープ色', async () => {
  const grammar = await loadGrammar();
  const line = 'string s = `a\\`b`;';
  const escapes = tokensWithScope(grammar, line, ESCAPE_SCOPE);
  assert.deepEqual(escapes, ['\\`'], `raw文字列のエスケープ: ${JSON.stringify(escapes)}`);
});
