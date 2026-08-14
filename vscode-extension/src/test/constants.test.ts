// ============================================================
// 組み込み定数ハイライトのトークナイズテスト
// ============================================================
// bareのNoneがPascalCaseの型名推定（entity.name.type）に食われず、C++/Rustのenumメンバと同じスコープで着色されることを検証する。

import * as assert from 'node:assert/strict';
import { test } from 'node:test';
import { loadGrammar, tokensWithScope } from './tokenize';

const PRIMITIVE_SCOPE = 'storage.type.primitive.cm';
const ENUM_MEMBER_SCOPE = 'variable.other.enummember.cm';
const TYPE_SCOPE = 'entity.name.type.cm';

test('bareのNoneはenumメンバスコープで着色される', async () => {
  const grammar = await loadGrammar();
  const line = 'Option<int> o = None;';
  const members = tokensWithScope(grammar, line, ENUM_MEMBER_SCOPE);
  assert.ok(
    members.includes('None'),
    `Noneがenumメンバ色になるべき: ${JSON.stringify(members)}`,
  );
  const primitives = tokensWithScope(grammar, line, PRIMITIVE_SCOPE);
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

test('文字列リテラル内のNoneは文字列のままenumメンバ色にならない', async () => {
  const grammar = await loadGrammar();
  const plain = 'string s = "None";';
  assert.ok(!tokensWithScope(grammar, plain, ENUM_MEMBER_SCOPE).includes('None'));
  const interp = 'println("{o.unwrap_or(\\"None\\")}");';
  assert.ok(!tokensWithScope(grammar, interp, ENUM_MEMBER_SCOPE).includes('None'));
});
