// ============================================================
// パッケージング整合性テスト（バンドル成果物と.vscodeignoreの突合）
// ============================================================
// クライアント・LSPサーバーの両バンドルがvsixに同梱され、node_modulesを同梱しない前提でも実行時にrequireが解決することを検証する。
// 過去にnavigationの依存モジュールが.vscodeignoreで除外されてactivate()がMODULE_NOT_FOUNDで停止し、ホバー・定義ジャンプが全滅する不具合があったため、その再発をバンドル層で防ぐ。実行: npm test

import { strict as assert } from 'node:assert';
import { existsSync, readFileSync } from 'node:fs';
import { builtinModules } from 'node:module';
import * as path from 'node:path';
import { test } from 'node:test';
import { listFiles } from '@vscode/vsce';
import { CLIENT_MODULE_PATH, SERVER_MODULE_PATH } from '../paths';

// out/src/test/packaging.test.js から拡張ルートへ遡る
const EXT_ROOT = path.resolve(__dirname, '..', '..', '..');

// バンドル内に残ってよい外部require（クライアントのみvscodeホストAPIを参照する）
const NODE_BUILTINS = new Set(builtinModules);

function externalRequires(bundlePath: string): string[] {
  const source = readFileSync(bundlePath, 'utf8');
  // __require等の別名を除外し、素のrequire("...")呼び出しだけを抽出する
  const pattern = /(?<![A-Za-z0-9_$.])require\(\s*["']([^"']+)["']\s*\)/g;
  const specs = new Set<string>();
  for (const match of source.matchAll(pattern)) {
    specs.add(match[1]);
  }
  return [...specs].sort();
}

// バンドルが自己完結している（node_modules無しで解決できる）ことを検証する
function assertSelfContained(bundleRelPath: string, allowed: Set<string>): void {
  const bundlePath = path.join(EXT_ROOT, bundleRelPath);
  assert.ok(
    existsSync(bundlePath),
    `${bundleRelPath} が未ビルド（npm run build:bundle を実行する）`,
  );
  const unresolved = externalRequires(bundlePath).filter(
    (spec) => !NODE_BUILTINS.has(spec.replace(/^node:/, '')) && !allowed.has(spec),
  );
  assert.deepEqual(
    unresolved,
    [],
    `${bundleRelPath} に同梱されない外部requireが残っている（実行時にMODULE_NOT_FOUNDを招く）: ${unresolved.join(', ')}`,
  );
}

test('クライアント・サーバーバンドルがvsixに同梱される', async () => {
  const packaged = new Set(await listFiles({ cwd: EXT_ROOT }));
  for (const rel of [CLIENT_MODULE_PATH, SERVER_MODULE_PATH]) {
    assert.ok(packaged.has(rel), `${rel} がvsixに同梱されていない（.vscodeignoreを確認する）`);
  }
});

test('package.jsonのmainはクライアントバンドルを指す', () => {
  const pkg = JSON.parse(readFileSync(path.join(EXT_ROOT, 'package.json'), 'utf8'));
  assert.equal(path.normalize(pkg.main), path.normalize(CLIENT_MODULE_PATH));
});

test('クライアントバンドルは vscode 以外の外部requireを持たない', () => {
  assertSelfContained(CLIENT_MODULE_PATH, new Set(['vscode']));
});

test('サーバーバンドルは外部requireを持たない（Node組み込みのみ）', () => {
  assertSelfContained(SERVER_MODULE_PATH, new Set());
});
