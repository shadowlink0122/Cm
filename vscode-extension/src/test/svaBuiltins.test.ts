// ============================================================
// SVA組み込み関数ハイライトのトークナイズテスト
// ============================================================
// SV-N7（並行アサーション）のsv_assert_property・時相演算子が呼び出し位置で組み込み色になり、
// after/past等の一般語が変数として使われた場合は組み込み色にならないことを検証する。

import * as assert from 'node:assert/strict';
import { test } from 'node:test';
import { loadGrammar, tokensWithScope } from './tokenize';

const BUILTIN_SCOPE = 'support.function.builtin.cm';

test('sv_assert_propertyと時相演算子は呼び出し位置で組み込み色になる', async () => {
  const grammar = await loadGrammar();
  const line = 'sv_assert_property(clk, implies(rose(req), after(ack, 2)));';
  const builtins = tokensWithScope(grammar, line, BUILTIN_SCOPE);
  assert.deepEqual(
    builtins,
    ['sv_assert_property', 'implies', 'rose', 'after'],
    `SVA組み込み: ${JSON.stringify(builtins)}`,
  );
});

test('past/fell/stableも呼び出し位置で組み込み色になる', async () => {
  const grammar = await loadGrammar();
  const line = 'sv_assert_property(clk, implies(past(req, 2), fell(x) || stable(y)));';
  const builtins = tokensWithScope(grammar, line, BUILTIN_SCOPE);
  assert.ok(builtins.includes('past'), `past: ${JSON.stringify(builtins)}`);
  assert.ok(builtins.includes('fell'), `fell: ${JSON.stringify(builtins)}`);
  assert.ok(builtins.includes('stable'), `stable: ${JSON.stringify(builtins)}`);
});

test('afterやpast等の一般語は変数位置では組み込み色にならない', async () => {
  const grammar = await loadGrammar();
  const line = 'int after = past + 1;';
  const builtins = tokensWithScope(grammar, line, BUILTIN_SCOPE);
  assert.deepEqual(
    builtins,
    [],
    `変数のafter/pastは組み込み色にしない: ${JSON.stringify(builtins)}`,
  );
});
