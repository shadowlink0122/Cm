// ============================================================
// 識別子抽出（server/words.ts）のユニットテスト
// ============================================================
// VSCode API非依存のロジックをnode:testで検証する。実行: npm test

import { strict as assert } from 'node:assert';
import { test } from 'node:test';
import { classifyAccess, isMethodAccess, isSkippedWord, wordAtOffset } from '../server/words';

test('オフセット位置の識別子を語頭・語中・語尾いずれでも切り出す', () => {
  const text = 'Point p = make_point(1, 2);';
  const head = text.indexOf('make_point');
  for (const offset of [head, head + 4, head + 'make_point'.length]) {
    const found = wordAtOffset(text, offset);
    assert.equal(found?.word, 'make_point', `offset=${offset}`);
    assert.equal(found?.start, head);
  }
});

test('識別子外・数値リテラル上ではundefinedを返す', () => {
  const text = 'int x = 123;';
  assert.equal(wordAtOffset(text, text.indexOf('=')), undefined);
  assert.equal(wordAtOffset(text, text.indexOf('123') + 1), undefined);
});

test('キーワード・プリミティブ型はナビゲーション対象外', () => {
  assert.ok(isSkippedWord('int'));
  assert.ok(isSkippedWord('struct'));
  assert.ok(isSkippedWord('return'));
  assert.ok(isSkippedWord('self'));
  assert.ok(!isSkippedWord('Point'));
  assert.ok(!isSkippedWord('println'));
});

test('直前にドットがある識別子をメソッドアクセスと判定する', () => {
  const text = 'items.push(1);';
  const found = wordAtOffset(text, text.indexOf('push'));
  assert.ok(found);
  assert.ok(isMethodAccess(text, found.start));
  const bare = wordAtOffset(text, 0);
  assert.ok(bare);
  assert.ok(!isMethodAccess(text, bare.start));
});

test('呼び出し文脈をメンバ・修飾・単独で判定する', () => {
  // メンバアクセス `w.write()`
  const member = 'w.write();';
  assert.equal(classifyAccess(member, member.indexOf('write')), 'member');
  // 修飾アクセス `File::write()`
  const qualified = 'File::write();';
  assert.equal(classifyAccess(qualified, qualified.indexOf('write')), 'qualified');
  // 単独呼び出し `write()`
  const bare = 'write(buf);';
  assert.equal(classifyAccess(bare, bare.indexOf('write')), 'bare');
  // 行頭の識別子も単独扱い
  assert.equal(classifyAccess('write();', 0), 'bare');
});
