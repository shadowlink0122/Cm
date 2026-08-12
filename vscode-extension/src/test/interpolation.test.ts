// ============================================================
// 文字列補間ハイライトのトークナイズテスト
// ============================================================
// 生成済み文法(syntaxes/cm.tmLanguage.json)を実エンジン(vscode-textmate + oniguruma)で走らせ、
// 補間プレースホルダの開閉括弧が期待個数ハイライトされることを検証する。
// 背景: v0.17.0でプレースホルダ内の構造体リテラル波括弧・エスケープ引用符が言語仕様として動作するようになったが、
// 旧regexは式内の\"や{}で一致に失敗し、後続プレースホルダのハイライトが全滅していた。

import * as assert from 'node:assert/strict';
import { test } from 'node:test';
import { loadGrammar, tokensWithScope } from './tokenize';

const BEGIN_SCOPE = 'punctuation.definition.template-expression.begin.cm';
const END_SCOPE = 'punctuation.definition.template-expression.end.cm';
const ESCAPE_SCOPE = 'constant.character.escape.cm';
const EMBEDDED_STRING_SCOPE = 'string.quoted.double.interpolation.cm';

test('連続プレースホルダ: エスケープ引用符入りでも全て開閉括弧がハイライトされる', async () => {
  const grammar = await loadGrammar();
  const line = 'println("sv: {sv.len()} {neg.unwrap_or(\\"None\\")} {pos.unwrap_or(\\"None\\")}");';
  const begins = tokensWithScope(grammar, line, BEGIN_SCOPE);
  const ends = tokensWithScope(grammar, line, END_SCOPE);
  assert.equal(begins.length, 3, `開き括弧が3個ハイライトされるべき: ${JSON.stringify(begins)}`);
  assert.equal(ends.length, 3, `閉じ括弧が3個ハイライトされるべき: ${JSON.stringify(ends)}`);
});

test('プレースホルダ内のエスケープ文字列リテラルは区切りの\\"ごと文字列として着色される', async () => {
  const grammar = await loadGrammar();
  const line = 'println("sv: {neg.unwrap_or(\\"None\\")} {pos.unwrap_or(\\"None\\")}");';
  const strings = tokensWithScope(grammar, line, EMBEDDED_STRING_SCOPE);
  assert.deepEqual(
    strings.join(''),
    '\\"None\\"\\"None\\"',
    `文字列トークン: ${JSON.stringify(strings)}`,
  );
});

test('1プレースホルダ内の複数リテラルは別々の文字列として着色され間の式は文字列にならない', async () => {
  const grammar = await loadGrammar();
  const line = 'string s = "{concat(\\"a\\", \\"b\\")}";';
  const strings = tokensWithScope(grammar, line, EMBEDDED_STRING_SCOPE);
  assert.equal(strings.join(''), '\\"a\\"\\"b\\"', `文字列トークン: ${JSON.stringify(strings)}`);
  const commas = tokensWithScope(grammar, line, 'meta.embedded.punctuation.comma.cm');
  assert.deepEqual(commas, [','], `区切りカンマは文字列色にならない: ${JSON.stringify(commas)}`);
});

test('連続プレースホルダ: 区切りなしの隣接{a}{b}が両方ハイライトされる', async () => {
  const grammar = await loadGrammar();
  const line = 'string s = "{a}{b}";';
  assert.equal(tokensWithScope(grammar, line, BEGIN_SCOPE).length, 2);
  assert.equal(tokensWithScope(grammar, line, END_SCOPE).length, 2);
});

test('構造体リテラル入りプレースホルダ: 外側括弧がハイライトされる（2段ネストまで）', async () => {
  const grammar = await loadGrammar();
  const one = 'string s = "{Point{x: 1, y: 2}} end";';
  const beginsOne = tokensWithScope(grammar, one, BEGIN_SCOPE);
  assert.equal(beginsOne.length, 1, `1段ネストの開き括弧: ${JSON.stringify(beginsOne)}`);
  const two = 'string s = "{Pair{first: Box{v: 1}}} end";';
  const beginsTwo = tokensWithScope(grammar, two, BEGIN_SCOPE);
  assert.equal(beginsTwo.length, 1, `2段ネストの開き括弧: ${JSON.stringify(beginsTwo)}`);
});

test('波括弧エスケープ: {{ {v} }} で{{と}}はエスケープ・{v}は補間として着色される', async () => {
  const grammar = await loadGrammar();
  const line = 'string s = "{{ {v} }}";';
  const escapes = tokensWithScope(grammar, line, ESCAPE_SCOPE);
  assert.deepEqual(escapes, ['{{', '}}'], `エスケープ対: ${JSON.stringify(escapes)}`);
  assert.equal(tokensWithScope(grammar, line, BEGIN_SCOPE).length, 1);
  assert.equal(tokensWithScope(grammar, line, END_SCOPE).length, 1);
});

test('閉じない単独の{は補間として着色されず文字列色のまま', async () => {
  const grammar = await loadGrammar();
  const line = 'string s = "abc { def";';
  assert.equal(tokensWithScope(grammar, line, BEGIN_SCOPE).length, 0);
  assert.equal(tokensWithScope(grammar, line, END_SCOPE).length, 0);
});
