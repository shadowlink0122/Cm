// ============================================================
// 文法ビルドCLI
// ============================================================
// 使い方:
//   node out/src/grammar/build.js          … syntaxes/cm.tmLanguage.json を再生成する
//   node out/src/grammar/build.js --check  … 生成結果とコミット済みJSONの一致を検証する（CI用。差分があれば終了コード1）

import * as fs from 'fs';
import * as path from 'path';
import { renderGrammar } from './grammar';

const GRAMMAR_PATH = path.join(__dirname, '..', '..', '..', 'syntaxes', 'cm.tmLanguage.json');

function main(): void {
  const rendered = renderGrammar();
  const checkOnly = process.argv.includes('--check');

  if (checkOnly) {
    const existing = fs.readFileSync(GRAMMAR_PATH, 'utf8');
    if (existing !== rendered) {
      console.error(
        'エラー: syntaxes/cm.tmLanguage.json が文法ソース (src/grammar/) と一致しません。`npm run build:grammar` で再生成してください。',
      );
      process.exit(1);
    }
    console.log('OK: cm.tmLanguage.json は文法ソースと一致しています');
    return;
  }

  fs.writeFileSync(GRAMMAR_PATH, rendered);
  console.log(`生成完了: ${path.relative(process.cwd(), GRAMMAR_PATH)}`);
}

main();
