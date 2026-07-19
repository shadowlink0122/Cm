// ============================================================
// 埋め込みコード系: asmブロック・バッククォートブロック・文字列補間
// ============================================================

import { TmRepositoryEntry } from '../tmTypes';
import { ASM_CONSTRAINT_KEYWORDS, ESCAPE_RULE, codeIncludes, words } from '../terms';

export const asmBlock: TmRepositoryEntry = {
  patterns: [
    {
      name: 'meta.embedded.assembly.cm',
      begin: '\\b(__asm__)\\s*\\{',
      end: '\\}',
      beginCaptures: {
        '1': { name: 'keyword.other.asm.cm' },
      },
      patterns: [
        {
          name: 'string.quoted.double.assembly.cm',
          begin: '"',
          end: '"',
          patterns: [ESCAPE_RULE],
        },
        { include: '#comments' },
        {
          name: 'keyword.other.asm.constraint.cm',
          match: words(ASM_CONSTRAINT_KEYWORDS),
        },
        { include: '#variables' },
      ],
    },
  ],
};

export const backtickBlock: TmRepositoryEntry = {
  comment: 'バッククォート文字列: デリミターは文字列色、中身は通常構文ハイライト',
  begin: '`',
  end: '`',
  beginCaptures: {
    '0': { name: 'string.quoted.backtick.cm' },
  },
  endCaptures: {
    '0': { name: 'string.quoted.backtick.cm' },
  },
  patterns: codeIncludes({ comments: true, punctuation: '#punctuation' }),
};

export const interpolationExpression: TmRepositoryEntry = {
  comment:
    'プレースホルダ内の式: トップレベルと同じinclude構成で任意の式（キャスト・namespace参照・ネストした関数呼び出し・演算子等）を通常コードと同様にハイライトする',
  patterns: codeIncludes({ punctuation: '#string-interpolation-punctuation' }),
};

export const stringInterpolationPunctuation: TmRepositoryEntry = {
  comment:
    'プレースホルダ内の区切り記号: 文字列外のpunctuationとスコープを分け、テーマの既定色（式と同系色）で表示する',
  patterns: [
    {
      name: 'meta.embedded.punctuation.accessor.cm',
      match: '\\.|::',
    },
    {
      name: 'meta.embedded.punctuation.parens.cm',
      match: '[()]',
    },
    {
      name: 'meta.embedded.punctuation.brackets.cm',
      match: '[\\[\\]]',
    },
    {
      name: 'meta.embedded.punctuation.comma.cm',
      match: ',',
    },
  ],
};
