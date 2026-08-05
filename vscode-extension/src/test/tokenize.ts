// ============================================================
// トークナイズテスト共通ヘルパー
// ============================================================
// 生成済み文法(syntaxes/cm.tmLanguage.json)を実エンジン(vscode-textmate + oniguruma)で走らせるための共有ローダー。

import * as assert from 'node:assert/strict';
import * as fs from 'node:fs';
import * as path from 'node:path';
import * as oniguruma from 'vscode-oniguruma';
import * as vsctm from 'vscode-textmate';

// 文法ファイルとonig.wasmを読み込み、source.cm文法を1度だけ初期化する
export async function loadGrammar(): Promise<vsctm.IGrammar> {
  const wasmPath = path.join(__dirname, '../../../node_modules/vscode-oniguruma/release/onig.wasm');
  const wasmBin = fs.readFileSync(wasmPath).buffer;
  const onigLib = oniguruma.loadWASM(wasmBin).then(() => ({
    createOnigScanner: (sources: string[]) => new oniguruma.OnigScanner(sources),
    createOnigString: (s: string) => new oniguruma.OnigString(s),
  }));
  const registry = new vsctm.Registry({
    onigLib,
    loadGrammar: async () => {
      const grammarPath = path.join(__dirname, '../../../syntaxes/cm.tmLanguage.json');
      return vsctm.parseRawGrammar(fs.readFileSync(grammarPath).toString(), grammarPath);
    },
  });
  const grammar = await registry.loadGrammar('source.cm');
  assert.ok(grammar, 'source.cm文法をロードできない');
  return grammar;
}

// 1行をトークナイズし、指定スコープを含むトークンのテキスト一覧を返す
export function tokensWithScope(grammar: vsctm.IGrammar, line: string, scope: string): string[] {
  const result = grammar.tokenizeLine(line, vsctm.INITIAL);
  return result.tokens
    .filter((t) => t.scopes.includes(scope))
    .map((t) => line.slice(t.startIndex, t.endIndex));
}
