// ============================================================
// 組み込み定数ハイライトのトークナイズテスト
// ============================================================
// bareのNoneがPascalCaseの型名推定（entity.name.type）に食われず、プリミティブ型と同一スコープで着色されることを検証する。

import * as assert from 'node:assert/strict';
import { test } from 'node:test';
import { loadGrammar, tokensWithScope } from './tokenize';

const PRIMITIVE_SCOPE = 'storage.type.primitive.cm';
const TYPE_SCOPE = 'entity.name.type.cm';

test('bareのNoneはプリミティブ型と同一スコープで着色される', async () => {
  const grammar = await loadGrammar();
  const line = 'Option<int> o = None;';
  const primitives = tokensWithScope(grammar, line, PRIMITIVE_SCOPE);
  assert.ok(
    primitives.includes('None'),
    `Noneがプリミティブ色になるべき: ${JSON.stringify(primitives)}`,
  );
  assert.ok(
    primitives.includes('int'),
    `intは従来通りプリミティブ色: ${JSON.stringify(primitives)}`,
  );
  const types = tokensWithScope(grammar, line, TYPE_SCOPE);
  assert.deepEqual(
    types,
    ['Option'],
    `Optionは型色のまま・Noneは型色にしない: ${JSON.stringify(types)}`,
  );
});

test('文字列リテラル内のNoneは文字列のままプリミティブ色にならない', async () => {
  const grammar = await loadGrammar();
  const plain = 'string s = "None";';
  assert.ok(!tokensWithScope(grammar, plain, PRIMITIVE_SCOPE).includes('None'));
  const interp = 'println("{o.unwrap_or(\\"None\\")}");';
  assert.ok(!tokensWithScope(grammar, interp, PRIMITIVE_SCOPE).includes('None'));
});
