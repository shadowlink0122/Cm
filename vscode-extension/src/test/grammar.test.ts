// ============================================================
// 文法の構造テスト
// ============================================================
// includeの参照先が存在すること・全repositoryキーが到達可能なこと・正規表現がコンパイル可能なことを検証する。

import * as assert from 'node:assert/strict';
import { test } from 'node:test';
import { buildGrammar } from '../grammar/grammar';

// 文法オブジェクトを再帰的に歩き、includeの参照名と正規表現(match/begin/end)を収集する
function walk(
  node: unknown,
  includes: Set<string>,
  regexes: { where: string; source: string }[],
  where: string,
): void {
  if (Array.isArray(node)) {
    node.forEach((child, i) => walk(child, includes, regexes, `${where}[${i}]`));
    return;
  }
  if (typeof node !== 'object' || node === null) {
    return;
  }
  const record = node as Record<string, unknown>;
  for (const [key, value] of Object.entries(record)) {
    if (key === 'include' && typeof value === 'string' && value.startsWith('#')) {
      includes.add(value.slice(1));
    } else if ((key === 'match' || key === 'begin' || key === 'end') && typeof value === 'string') {
      regexes.push({ where: `${where}.${key}`, source: value });
    } else {
      walk(value, includes, regexes, `${where}.${key}`);
    }
  }
}

test('includeの参照先repositoryキーが全て存在する', () => {
  const grammar = buildGrammar();
  const includes = new Set<string>();
  walk(grammar, includes, [], 'grammar');
  const defined = new Set(Object.keys(grammar.repository));
  for (const name of includes) {
    assert.ok(defined.has(name), `include参照 #${name} に対応するrepositoryキーがない`);
  }
});

test('全repositoryキーがどこかから参照されている（孤児がない）', () => {
  const grammar = buildGrammar();
  const includes = new Set<string>();
  walk(grammar, includes, [], 'grammar');
  for (const key of Object.keys(grammar.repository)) {
    assert.ok(includes.has(key), `repositoryキー ${key} がどこからも参照されていない`);
  }
});

test('全正規表現がコンパイル可能', () => {
  const grammar = buildGrammar();
  const regexes: { where: string; source: string }[] = [];
  walk(grammar, new Set(), regexes, 'grammar');
  assert.ok(regexes.length > 50, '正規表現の収集数が異常に少ない（walkの回帰）');
  for (const { where, source } of regexes) {
    // TextMate(Oniguruma)構文の大半はJS RegExpと互換。非互換構文を導入した場合はこのテストを更新する
    assert.doesNotThrow(() => new RegExp(source), `${where} の正規表現が不正: ${source}`);
  }
});

test('captures内のpatternsからの参照（文字列補間）も解決できる', () => {
  const grammar = buildGrammar();
  assert.ok(grammar.repository['interpolation-expression'], 'interpolation-expressionが存在しない');
  assert.ok(
    grammar.repository['string-interpolation-punctuation'],
    'string-interpolation-punctuationが存在しない',
  );
});
