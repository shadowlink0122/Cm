// ============================================================
// 非アクティブコード判定のユニットテスト
// ============================================================

import * as assert from 'node:assert/strict';
import { test } from 'node:test';
import { findInactiveRanges } from '../inactiveCode';

test('未定義シンボルの#ifdef本体を減光する', () => {
  const text = ['#ifdef DEBUG', 'int x = 1;', '#end'].join('\n');
  const ranges = findInactiveRanges(text, []);
  assert.equal(ranges.length, 1);
  assert.equal(ranges[0].startLine, 1);
  assert.equal(ranges[0].endLine, 1);
});

test('#defineで定義済みのシンボルは減光しない', () => {
  const text = ['#define DEBUG', '#ifdef DEBUG', 'int x = 1;', '#end'].join('\n');
  assert.equal(findInactiveRanges(text, []).length, 0);
});

test('activeDefinesで指定したシンボルは定義済み扱いになる', () => {
  const text = ['#ifdef TEST', 'int x = 1;', '#end'].join('\n');
  assert.equal(findInactiveRanges(text, ['TEST']).length, 0);
});

test('定義済みシンボルの#ifndef本体を減光する', () => {
  const text = ['#define FEATURE', '#ifndef FEATURE', 'int x = 1;', '#end'].join('\n');
  const ranges = findInactiveRanges(text, []);
  assert.equal(ranges.length, 1);
  assert.equal(ranges[0].startLine, 2);
});

test('条件が真と確定した場合は#else側を減光する', () => {
  const text = ['#define ON', '#ifdef ON', 'int a;', '#else', 'int b;', '#end'].join('\n');
  const ranges = findInactiveRanges(text, []);
  assert.equal(ranges.length, 1);
  assert.equal(ranges[0].startLine, 4);
  assert.equal(ranges[0].endLine, 4);
});

test('環境依存の__NAME__形式は判定不能として減光しない', () => {
  const text = ['#ifdef __x86_64__', 'int x = 1;', '#end'].join('\n');
  assert.equal(findInactiveRanges(text, []).length, 0);
});

test('__CM__は常に定義済み扱い', () => {
  const text = ['#ifndef __CM__', 'int x = 1;', '#end'].join('\n');
  assert.equal(findInactiveRanges(text, []).length, 1);
});

test('リテラルfalseのifブロックを減光する', () => {
  const text = ['if (false) {', '  int x = 1;', '}'].join('\n');
  const ranges = findInactiveRanges(text, []);
  assert.equal(ranges.length, 1);
  assert.equal(ranges[0].startLine, 0);
  assert.equal(ranges[0].endLine, 2);
});

test('while (0) ブロックも減光する', () => {
  const text = ['while (0) {', '  step();', '}'].join('\n');
  assert.equal(findInactiveRanges(text, []).length, 1);
});

test('減光中の親スコープ内のネストは重複して報告しない', () => {
  const text = ['#ifdef UNDEFINED', '#ifdef ALSO_UNDEFINED', 'int x;', '#end', '#end'].join('\n');
  const ranges = findInactiveRanges(text, []);
  assert.equal(ranges.length, 1);
  assert.equal(ranges[0].startLine, 1);
  assert.equal(ranges[0].endLine, 3);
});

test('#undefで定義を打ち消すと未定義扱いになる', () => {
  const text = ['#define X', '#undef X', '#ifdef X', 'int x;', '#end'].join('\n');
  assert.equal(findInactiveRanges(text, []).length, 1);
});
