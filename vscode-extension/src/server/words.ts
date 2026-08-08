// ============================================================
// カーソル位置の識別子抽出（VSCode API非依存の純ロジック）
// ============================================================
// LSPサーバーがホバー・定義ジャンプの対象語を切り出すために使う。キーワード・プリミティブ型はナビゲーション対象外とする。

import {
  BOOLEAN_CONSTANTS,
  CONTROL_CONDITIONAL,
  CONTROL_EXCEPTION,
  CONTROL_EXPORT,
  CONTROL_FLOW,
  CONTROL_REPEAT,
  CONTROL_RETURN,
  DECLARATION_KEYWORDS,
  MODIFIER_KEYWORDS,
  NULL_CONSTANTS,
  PRIMITIVE_TYPES,
  SIZEOF_KEYWORDS,
} from '../grammar/terms';

// キーワード・プリミティブ型はホバー対象外（言語仕様の説明までは提供しない）
const SKIP_WORDS = new Set<string>([
  ...PRIMITIVE_TYPES,
  ...BOOLEAN_CONSTANTS,
  ...NULL_CONSTANTS,
  ...CONTROL_CONDITIONAL,
  ...CONTROL_REPEAT,
  ...CONTROL_RETURN,
  ...CONTROL_FLOW,
  ...CONTROL_EXPORT,
  ...CONTROL_EXCEPTION,
  ...DECLARATION_KEYWORDS,
  ...MODIFIER_KEYWORDS,
  ...SIZEOF_KEYWORDS,
  'self',
]);

export interface WordAtOffset {
  word: string;
  start: number;
  end: number;
}

function isWordChar(ch: string): boolean {
  return /[A-Za-z0-9_]/.test(ch);
}

// オフセット位置を含む識別子を切り出す（識別子上に無い・数値始まりの場合はundefined）
export function wordAtOffset(text: string, offset: number): WordAtOffset | undefined {
  let start = offset;
  while (start > 0 && isWordChar(text[start - 1])) {
    start--;
  }
  let end = offset;
  while (end < text.length && isWordChar(text[end])) {
    end++;
  }
  if (start === end) {
    return undefined;
  }
  const word = text.slice(start, end);
  if (!/^[A-Za-z_]/.test(word)) {
    return undefined;
  }
  return { word, start, end };
}

// ナビゲーション対象外の語（キーワード等）か判定する
export function isSkippedWord(word: string): boolean {
  return SKIP_WORDS.has(word);
}

// 識別子が直前の `.` を伴うメソッドアクセスか判定する（`obj.method` の method 側）
export function isMethodAccess(text: string, wordStart: number): boolean {
  return wordStart > 0 && text[wordStart - 1] === '.';
}
