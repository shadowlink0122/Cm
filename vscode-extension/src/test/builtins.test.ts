// ============================================================
// コンパイラ組み込みメソッド・関数レジストリ（navigation/builtins.ts）のユニットテスト
// ============================================================
// 実行: npm test

import { strict as assert } from 'node:assert';
import { test } from 'node:test';
import {
  BUILTIN_FUNCTIONS,
  BUILTIN_METHODS,
  lookupBuiltinFunction,
  lookupBuiltinMethod,
} from '../navigation/builtins';

test('配列/スライスの高階メソッドが登録されている', () => {
  for (const name of ['map', 'filter', 'reduce', 'forEach', 'find', 'some', 'every', 'sort']) {
    const entries = lookupBuiltinMethod(name);
    assert.ok(entries.length >= 1, `${name} が登録されている`);
    assert.ok(entries[0].signature.includes(name), `${name} のシグネチャに名前が含まれる`);
    assert.ok(entries[0].doc.length > 0, `${name} に説明がある`);
  }
});

test('スライス変更系メソッドが登録されている', () => {
  for (const name of ['push', 'pop', 'remove', 'clear', 'cap']) {
    assert.ok(lookupBuiltinMethod(name).length >= 1, `${name} が登録されている`);
  }
});

test('文字列メソッドが登録されている', () => {
  for (const name of [
    'byte_len',
    'byte_at',
    'chars',
    'codepoint_at',
    'substring',
    'trim',
    'repeat',
    'startsWith',
  ]) {
    assert.ok(lookupBuiltinMethod(name).length >= 1, `${name} が登録されている`);
  }
});

test('SVA（並行アサーション）組み込み関数が登録されている', () => {
  for (const name of [
    'sv_assert_property',
    'implies',
    'implies_next',
    'after',
    'rose',
    'fell',
    'stable',
    'past',
  ]) {
    const fn = lookupBuiltinFunction(name);
    assert.ok(fn, `${name} が登録されている`);
    assert.equal(fn?.receiver, 'なし');
  }
});

test('複数レシーバを持つメソッドは全オーバーロードを返す', () => {
  // len は配列/スライスと文字列の2種
  const len = lookupBuiltinMethod('len');
  assert.equal(len.length, 2);
  const receivers = len.map((e) => e.receiver).sort();
  assert.ok(receivers.some((r) => r.includes('配列')));
  assert.ok(receivers.some((r) => r.includes('文字列')));

  // indexOf は配列/スライスと文字列の2種
  assert.equal(lookupBuiltinMethod('indexOf').length, 2);

  // unwrap は Option と Result の2種
  const unwrap = lookupBuiltinMethod('unwrap');
  assert.equal(unwrap.length, 2);
  assert.ok(unwrap.some((e) => e.receiver.startsWith('Option')));
  assert.ok(unwrap.some((e) => e.receiver.startsWith('Result')));
});

test('Option/Result のメソッドが登録されている', () => {
  for (const name of [
    'is_some',
    'is_none',
    'is_ok',
    'is_err',
    'unwrap',
    'unwrap_or',
    'expect',
    'unwrap_err',
  ]) {
    assert.ok(lookupBuiltinMethod(name).length >= 1, `${name} が登録されている`);
  }
});

test('未登録のメソッド名は空配列を返す', () => {
  assert.deepEqual(lookupBuiltinMethod('no_such_builtin_method'), []);
});

test('組み込み関数（println等）が登録されている', () => {
  for (const name of ['print', 'println', 'panic', 'assert', 'exit']) {
    const fn = lookupBuiltinFunction(name);
    assert.ok(fn, `${name} が登録されている`);
    assert.equal(fn?.receiver, 'なし');
    assert.ok(fn?.signature.includes(name));
  }
  assert.equal(lookupBuiltinFunction('no_such_fn'), undefined);
});

test('全エントリのシグネチャと説明が非空', () => {
  for (const entries of Object.values(BUILTIN_METHODS)) {
    for (const e of entries) {
      assert.ok(e.signature.trim().length > 0);
      assert.ok(e.doc.trim().length > 0);
      assert.ok(e.receiver.trim().length > 0);
    }
  }
  for (const e of Object.values(BUILTIN_FUNCTIONS)) {
    assert.ok(e.signature.trim().length > 0);
    assert.ok(e.doc.trim().length > 0);
  }
});
