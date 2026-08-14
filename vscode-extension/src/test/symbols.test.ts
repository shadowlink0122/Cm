// ============================================================
// シンボル抽出（navigation/symbols.ts）のユニットテスト
// ============================================================
// VSCode API非依存のロジックをnode:testで検証する。実行: npm test

import { strict as assert } from 'node:assert';
import { test } from 'node:test';
import { CmSymbol, extractSymbols, rankMatches, sanitizeSource } from '../navigation/symbols';

function byName(symbols: CmSymbol[], name: string): CmSymbol | undefined {
  return symbols.find((s) => s.name === name);
}

test('struct・enum・interface・typedefのトップレベル宣言を抽出する', () => {
  const src = [
    '/// 2次元の点',
    'export struct Point {',
    '    int x;',
    '    int y;',
    '}',
    '',
    'enum Color {',
    '    Red,',
    '    Green,',
    '    Custom(int),',
    '}',
    '',
    'export interface Printable {',
    '    void print_it();',
    '}',
    '',
    'export typedef Number = int | double;',
  ].join('\n');
  const symbols = extractSymbols(src);

  const point = byName(symbols, 'Point');
  assert.equal(point?.kind, 'struct');
  assert.equal(point?.doc, '2次元の点');
  assert.equal(point?.signature, 'export struct Point');
  assert.equal(point?.line, 1);
  assert.equal(point?.endLine, 4);

  assert.equal(byName(symbols, 'Color')?.kind, 'enum');
  assert.equal(byName(symbols, 'Printable')?.kind, 'interface');
  assert.equal(byName(symbols, 'Number')?.kind, 'typedef');

  // フィールド・バリアント・インターフェースメソッドも所属付きで拾う
  assert.equal(byName(symbols, 'x')?.kind, 'field');
  assert.equal(byName(symbols, 'x')?.container, 'Point');
  assert.equal(byName(symbols, 'Custom')?.kind, 'variant');
  assert.equal(byName(symbols, 'Custom')?.container, 'Color');
  assert.equal(byName(symbols, 'print_it')?.kind, 'method');
  assert.equal(byName(symbols, 'print_it')?.container, 'Printable');
});

test('トップレベル関数とimplメソッド・コンストラクタを抽出する', () => {
  const src = [
    '// 加算する',
    'int add(int a, int b) {',
    '    return a + b;',
    '}',
    '',
    'export <T: Eq> void assert_eq(T left, T right) {',
    '    exit(1);',
    '}',
    '',
    'export impl<K, V> HashMap<K, V> {',
    '    self() {',
    '        self.size = 0;',
    '    }',
    '    ~self() {',
    '    }',
    '    void insert(K key, V value) {',
    '        int idx = 0;',
    '    }',
    '    Option<V> get(K key) {',
    '        return None;',
    '    }',
    '}',
  ].join('\n');
  const symbols = extractSymbols(src);

  const add = byName(symbols, 'add');
  assert.equal(add?.kind, 'function');
  assert.equal(add?.doc, '加算する');
  assert.equal(add?.signature, 'int add(int a, int b)');

  assert.equal(byName(symbols, 'assert_eq')?.kind, 'function');
  assert.equal(
    byName(symbols, 'assert_eq')?.signature,
    'export <T: Eq> void assert_eq(T left, T right)',
  );

  for (const name of ['self', '~self', 'insert', 'get']) {
    const sym = byName(symbols, name);
    assert.equal(sym?.kind, 'method', `${name} はメソッドとして抽出される`);
    assert.equal(sym?.container, 'HashMap', `${name} の所属はHashMap`);
  }
  // メソッド本体のローカル宣言は拾わない
  assert.equal(byName(symbols, 'idx'), undefined);
});

test('impl 型 for インターフェース は対象型（forの前）をコンテナにする', () => {
  const src = ['impl Point for Printable {', '    void print_it() {', '    }', '}'].join('\n');
  const symbols = extractSymbols(src);
  assert.equal(byName(symbols, 'print_it')?.container, 'Point');
});

test('演算子impl（impl 型 for Add等）とinterfaceの演算子宣言を抽出する', () => {
  const src = [
    'interface Comparable {',
    '    operator bool ==(Comparable other);',
    '}',
    '',
    'impl Vec2 for Add {',
    '    operator Vec2 +(Vec2 other) {',
    '        return Vec2{x: 1, y: 2};',
    '    }',
    '}',
  ].join('\n');
  const symbols = extractSymbols(src);
  const plus = byName(symbols, 'operator+');
  assert.equal(plus?.kind, 'method');
  assert.equal(plus?.container, 'Vec2', '演算子インターフェースimplの所属は対象型');
  const eq = byName(symbols, 'operator==');
  assert.equal(eq?.kind, 'method');
  assert.equal(eq?.container, 'Comparable');
});

test('複数行シグネチャを1行へ連結する', () => {
  const src = ['export void configure(', '    int width,', '    int height', ') {', '}'].join('\n');
  const symbols = extractSymbols(src);
  assert.equal(
    byName(symbols, 'configure')?.signature,
    'export void configure( int width, int height )',
  );
});

test('module・macro・トップレベルconst・useブロックのFFI宣言を抽出する', () => {
  const src = [
    'module std.collections.hashmap;',
    '',
    'macro int MAX_SIZE = 1024;',
    'export const string VERSION = "1.0";',
    '',
    'use libc {',
    '    void* malloc(int size);',
    '    void free(void* ptr);',
    '}',
  ].join('\n');
  const symbols = extractSymbols(src);
  assert.equal(byName(symbols, 'std.collections.hashmap')?.kind, 'module');
  assert.equal(byName(symbols, 'MAX_SIZE')?.kind, 'macro');
  assert.equal(byName(symbols, 'VERSION')?.kind, 'constant');
  assert.equal(byName(symbols, 'malloc')?.kind, 'function');
  assert.equal(byName(symbols, 'malloc')?.container, 'libc');
  assert.equal(byName(symbols, 'free')?.container, 'libc');
});

test('制御構文・関数呼び出し・文字列内の波括弧を宣言と誤認しない', () => {
  const src = [
    'int main() {',
    '    if (true) {',
    '        println("brace { in string");',
    '    }',
    '    for (int i = 0; i < 3; i++) {',
    '        step();',
    '    }',
    '    return 0;',
    '}',
    '',
    'struct After {',
    '    int value;',
    '}',
  ].join('\n');
  const symbols = extractSymbols(src);
  const names = symbols.map((s) => s.name);
  assert.deepEqual(names, ['main', 'After', 'value']);
  // 文字列内の `{` で深さが狂わずAfterがトップレベル扱いになる
  assert.equal(byName(symbols, 'After')?.kind, 'struct');
});

test('ブロックコメント内の宣言風テキストを無視する', () => {
  const src = [
    '/*',
    'struct Ghost {',
    '}',
    '*/',
    '// export interface AlsoGhost {',
    'struct Real {',
    '}',
  ].join('\n');
  const symbols = extractSymbols(src);
  assert.equal(byName(symbols, 'Ghost'), undefined);
  assert.equal(byName(symbols, 'AlsoGhost'), undefined);
  assert.equal(byName(symbols, 'Real')?.kind, 'struct');
});

test('///ドキュメントは複数行を保持し、区切り線コメントは除外する', () => {
  const src = [
    '// ============================================================',
    '/// アサーション: 条件が偽なら異常終了する。',
    '///',
    '/// SVバックエンドでは即時アサーションに変換される。',
    'export void assert(bool cond, string msg) {',
    '}',
  ].join('\n');
  const symbols = extractSymbols(src);
  const doc = byName(symbols, 'assert')?.doc ?? '';
  assert.ok(doc.includes('条件が偽なら異常終了'));
  assert.ok(doc.includes('SVバックエンド'));
  assert.ok(!doc.includes('======'));
});

test('rankMatchesは型・関数をフィールド・バリアントより優先する', () => {
  const src = [
    'struct Value {',
    '    int size;',
    '}',
    '',
    'int size() {',
    '    return 0;',
    '}',
  ].join('\n');
  const symbols = extractSymbols(src).filter((s) => s.name === 'size');
  assert.equal(symbols.length, 2);
  const ranked = rankMatches(symbols);
  assert.equal(ranked.length, 1);
  assert.equal(ranked[0].kind, 'function');
});

test('sanitizeSourceは文字列・コメントの中身だけを空白化し長さを保つ', () => {
  const lines = sanitizeSource('int x = 1; // comment {\nstring s = "a{b}c";');
  assert.equal(lines[0].length, 'int x = 1; // comment {'.length);
  assert.ok(!lines[0].includes('comment'));
  assert.ok(!lines[1].includes('a{b}c'));
  assert.ok(lines[1].startsWith('string s = "'));
});

test('ネスト型宣言（struct内struct・enum内enum）をコンテナ名付きで抽出する', () => {
  const src = [
    'struct Outer {',
    '    struct Inner {',
    '        int mem;',
    '    };',
    '    enum Mode {',
    '        FAST,',
    '        SLOW,',
    '    }',
    '    Inner inner;',
    '}',
    '',
    'enum Category {',
    '    enum Sub {',
    '        MEM,',
    '    },',
    '    A,',
    '}',
  ].join('\n');
  const symbols = extractSymbols(src);

  const inner = byName(symbols, 'Inner');
  assert.equal(inner?.kind, 'struct');
  assert.equal(inner?.container, 'Outer');
  assert.equal(byName(symbols, 'mem')?.container, 'Inner');

  const mode = byName(symbols, 'Mode');
  assert.equal(mode?.kind, 'enum');
  assert.equal(mode?.container, 'Outer');
  assert.equal(byName(symbols, 'FAST')?.container, 'Mode');

  const sub = byName(symbols, 'Sub');
  assert.equal(sub?.kind, 'enum');
  assert.equal(sub?.container, 'Category');
  assert.equal(byName(symbols, 'MEM')?.container, 'Sub');
  assert.equal(byName(symbols, 'A')?.container, 'Category');
});
