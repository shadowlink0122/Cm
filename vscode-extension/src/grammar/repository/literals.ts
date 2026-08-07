// ============================================================
// リテラル系: コメント・文字列・数値・定数
// ============================================================

import { TmRepositoryEntry } from '../tmTypes';
import {
  BOOLEAN_CONSTANTS,
  ESCAPE_RULE,
  NULL_CONSTANTS,
  PRELUDE_VALUE_VARIANTS,
  words,
} from '../terms';

export const comments: TmRepositoryEntry = {
  patterns: [
    {
      name: 'comment.block.cm',
      begin: '/\\*',
      end: '\\*/',
      captures: {
        '0': { name: 'punctuation.definition.comment.cm' },
      },
    },
    {
      name: 'comment.line.double-slash.cm',
      begin: '//',
      end: '$',
      captures: {
        '0': { name: 'punctuation.definition.comment.cm' },
      },
    },
  ],
};

export const strings: TmRepositoryEntry = {
  patterns: [
    {
      name: 'string.quoted.double.cm',
      begin: '"',
      end: '"',
      patterns: [
        ESCAPE_RULE,
        {
          name: 'constant.character.escape.cm',
          match: '\\{\\{|\\}\\}',
        },
        {
          comment:
            '補間プレースホルダ: 同一行で } まで閉じる {式} / {式:書式} のみ着色する（閉じない単独の { はbegin/endを使わないため文字列色のまま）。中括弧は青（template-expressionスコープ）、内側の式は通常の構文ハイライト。式内のエスケープ文字（\\"等）と構造体リテラルの波括弧2段までを許容する（{x.f(\\"s\\")}や{Point{x: 1}}が対象。文字列リテラル内の裸の"は文字列終端なので式に現れない）',
          match:
            '(\\{)((?:[^{}"\\\\]|\\\\.|\\{(?:[^{}"\\\\]|\\\\.|\\{(?:[^{}"\\\\]|\\\\.)*\\})*\\})*)(\\})',
          captures: {
            '1': { name: 'punctuation.definition.template-expression.begin.cm' },
            '2': { patterns: [{ include: '#interpolation-expression' }] },
            '3': { name: 'punctuation.definition.template-expression.end.cm' },
          },
        },
      ],
    },
    {
      // charリテラル: \xHH等の複数文字エスケープにも対応（R5でcharと文字列のエスケープ表を共用化）
      name: 'string.quoted.single.cm',
      match: "'(\\\\(x[0-9a-fA-F]{2}|u[0-9a-fA-F]{4}|U[0-9a-fA-F]{8}|.)|[^'\\\\])'",
    },
  ],
};

export const numbers: TmRepositoryEntry = {
  patterns: [
    {
      comment: "SV幅付きリテラル: N'[dbhDBH]VALUE",
      name: 'constant.numeric.sv-literal.cm',
      match: "\\b[0-9]+'[dDbBhH][0-9a-fA-F]+\\b",
    },
    {
      name: 'constant.numeric.hex.cm',
      match: '\\b0[xX][0-9a-fA-F]+\\b',
    },
    {
      name: 'constant.numeric.binary.cm',
      match: '\\b0[bB][01?]+\\b',
    },
    {
      name: 'constant.numeric.octal.cm',
      match: '\\b0[oO][0-7]+\\b',
    },
    {
      name: 'constant.numeric.float.cm',
      match: '\\b[0-9]+\\.[0-9]+([eE][+-]?[0-9]+)?\\b',
    },
    {
      name: 'constant.numeric.integer.cm',
      match: '\\b[0-9]+\\b',
    },
  ],
};

export const constants: TmRepositoryEntry = {
  patterns: [
    {
      name: 'constant.language.boolean.cm',
      match: words(BOOLEAN_CONSTANTS),
    },
    {
      name: 'constant.language.null.cm',
      match: words(NULL_CONSTANTS),
    },
    {
      comment: 'OptionのNoneバリアント: プリミティブ型と同一スコープにして全テーマで同色にする',
      name: 'storage.type.primitive.cm',
      match: words(PRELUDE_VALUE_VARIANTS),
    },
    {
      name: 'constant.numeric.cm',
      match: '\\b[A-Z][A-Z0-9_]*\\b',
    },
    {
      name: 'variable.language.self.cm',
      match: '\\b(self)\\b',
    },
  ],
};
