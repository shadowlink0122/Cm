// ============================================================
// C++/Rust標準スコープへのマッピングのトークナイズテスト
// ============================================================
// enumメンバ・定数・宣言キーワード・修飾子が標準TextMateスコープで着色されることを検証する。

import * as assert from 'node:assert/strict';
import { test } from 'node:test';
import { loadGrammar, tokensWithScope } from './tokenize';

const ENUM_MEMBER = 'variable.other.enummember.cm';
const CONSTANT = 'variable.other.constant.cm';
const STORAGE_TYPE = 'storage.type.cm';
const STORAGE_MODIFIER = 'storage.modifier.cm';
const TYPE = 'entity.name.type.cm';

test('::修飾されたSCREAMING_CASEはenumメンバスコープになる', async () => {
  const grammar = await loadGrammar();
  const line = 'o.mode = Outer::Mode::FAST;';
  assert.deepEqual(tokensWithScope(grammar, line, ENUM_MEMBER), ['FAST']);
  const types = tokensWithScope(grammar, line, TYPE);
  assert.ok(types.includes('Outer') && types.includes('Mode'), '型パス部分は型色のまま');
});

test('裸のSCREAMING_CASEは定数スコープ・単一大文字Tは型スコープになる', async () => {
  const grammar = await loadGrammar();
  const line = 'const int MAX_SIZE = 10; T value = make<T>();';
  assert.deepEqual(tokensWithScope(grammar, line, CONSTANT), ['MAX_SIZE']);
  assert.ok(tokensWithScope(grammar, line, TYPE).includes('T'), 'TはenumメンバでなくPascalCase型色');
});

test('struct/enumキーワードはstorage.type・const/exportはstorage.modifierになる', async () => {
  const grammar = await loadGrammar();
  const line = 'export struct Outer { const int x; }';
  const storage = tokensWithScope(grammar, line, STORAGE_TYPE);
  assert.ok(storage.includes('struct'), 'structはstorage.type');
  const modifiers = tokensWithScope(grammar, line, STORAGE_MODIFIER);
  assert.ok(modifiers.includes('export') && modifiers.includes('const'), 'export/constはstorage.modifier');
});

test('匿名struct（struct { ... } 宣言子;）でもキーワードはstorage.typeで着色される', async () => {
  const grammar = await loadGrammar();
  const line = 'struct { int mem; } STR;';
  assert.ok(tokensWithScope(grammar, line, STORAGE_TYPE).includes('struct'));
  // 宣言子STRはSCREAMING_CASEのため定数スコープ（C/C++の定数変数と同じ見た目）
  assert.ok(tokensWithScope(grammar, line, CONSTANT).includes('STR'));
});
