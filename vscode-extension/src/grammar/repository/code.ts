// ============================================================
// コード系: キーワード・型・関数・変数・演算子・区切り記号
// ============================================================

import { TmRepositoryEntry } from '../tmTypes';
import {
  BUILTIN_FUNCTIONS,
  CONTROL_CONDITIONAL,
  CONTROL_EXCEPTION,
  CONTROL_EXPORT,
  CONTROL_FLOW,
  CONTROL_REPEAT,
  CONTROL_RETURN,
  DECLARATION_KEYWORDS,
  FUNCTION_PROTOTYPE_MATCH,
  IDENT,
  IDENT_ALT,
  INTRINSIC_KEYWORDS,
  MODIFIER_KEYWORDS,
  PRIMITIVE_TYPES,
  SIZEOF_KEYWORDS,
  SVA_FUNCTIONS,
  SV_CONTROL_KEYWORDS,
  SV_MODIFIER_KEYWORDS,
  TYPE_DECLARATION_KEYWORDS,
  words,
} from '../terms';

export const keywords: TmRepositoryEntry = {
  patterns: [
    {
      name: 'keyword.control.conditional.cm',
      match: words(CONTROL_CONDITIONAL),
    },
    {
      name: 'keyword.control.repeat.cm',
      match: words(CONTROL_REPEAT),
    },
    {
      name: 'keyword.control.return.cm',
      match: words(CONTROL_RETURN),
    },
    {
      name: 'keyword.control.flow.cm',
      match: words(CONTROL_FLOW),
    },
    {
      name: 'keyword.control.export.cm',
      match: words(CONTROL_EXPORT),
    },
    {
      name: 'keyword.control.exception.cm',
      match: words(CONTROL_EXCEPTION),
    },
    {
      name: 'keyword.other.cm',
      match: words(DECLARATION_KEYWORDS),
    },
    {
      name: 'storage.type.primitive.cm',
      match: words(MODIFIER_KEYWORDS),
    },
    {
      name: 'keyword.operator.new.cm',
      match: words(SIZEOF_KEYWORDS),
    },
    {
      name: 'keyword.other.asm.cm',
      match: words(INTRINSIC_KEYWORDS),
    },
    {
      name: 'keyword.control.sv.cm',
      match: words(SV_CONTROL_KEYWORDS),
    },
    {
      name: 'storage.modifier.sv.cm',
      match: words(SV_MODIFIER_KEYWORDS),
    },
  ],
};

export const typeDeclaration: TmRepositoryEntry = {
  comment:
    '型宣言（struct/enum/interface/impl/union/typedef の直後の識別子）を型名として着色する。プリミティブ以外の任意の型はimportのモジュール名と同じ entity.name.type 系スコープになり、変数（variable.other）とは別色になる',
  patterns: [
    {
      match: `\\b(${TYPE_DECLARATION_KEYWORDS.join('|')})\\s+(${IDENT_ALT})`,
      captures: {
        '1': { name: 'keyword.other.cm' },
        '2': { name: 'entity.name.type.cm' },
      },
    },
  ],
};

export const types: TmRepositoryEntry = {
  patterns: [
    {
      name: 'storage.type.primitive.cm',
      match: words(PRIMITIVE_TYPES),
    },
    {
      comment:
        '型として使用された非プリミティブ識別子（変数宣言の型位置・ポインタ/参照・ジェネリック引数・キャスト先など）。世界標準（C/C++/Rust）に従い型名はPascalCaseのため先頭大文字で判定する',
      name: 'entity.name.type.cm',
      match: '\\b([A-Z][a-zA-Z0-9_]*)\\b',
    },
  ],
};

export const builtinFunctions: TmRepositoryEntry = {
  patterns: [
    {
      name: 'support.function.builtin.cm',
      match: words(BUILTIN_FUNCTIONS),
    },
    {
      comment:
        'SVA組み込み（sv_assert_property・時相演算子）。after/past等は変数名にも使える一般語のため、呼び出し位置のみ組み込み色にする',
      name: 'support.function.builtin.cm',
      match: `\\b(${SVA_FUNCTIONS.join('|')})\\b(?=\\s*\\()`,
    },
  ],
};

export const functionDeclaration: TmRepositoryEntry = {
  patterns: [
    {
      match: FUNCTION_PROTOTYPE_MATCH,
      captures: {
        '1': { name: 'entity.name.function.cm' },
      },
    },
  ],
};

export const functionCall: TmRepositoryEntry = {
  patterns: [
    {
      name: 'entity.name.function.member.cm',
      match: `(?<=\\.)(${IDENT})\\s*(?=\\()`,
    },
    {
      name: 'entity.name.function.cm',
      match: `\\b(${IDENT})\\s*(?=\\()`,
    },
  ],
};

export const variables: TmRepositoryEntry = {
  patterns: [
    {
      name: 'variable.other.member.cm',
      match: `(?<=\\.)(${IDENT})\\b(?!\\s*\\()`,
    },
    {
      name: 'variable.other.cm',
      match: '\\b([a-z_][a-zA-Z0-9_]*|_[a-zA-Z0-9_]+)\\b',
    },
  ],
};

export const operators: TmRepositoryEntry = {
  patterns: [
    {
      name: 'keyword.operator.bitslice.cm',
      match: '\\+:|-:',
    },
    {
      name: 'keyword.operator.logical.cm',
      match: '(&&|\\|\\||!(?!=))',
    },
    {
      name: 'keyword.operator.comparison.cm',
      match: '(==|!=|<=|>=)',
    },
    {
      name: 'punctuation.definition.typeparameters.cm',
      match: '(<(?!<)|>(?!>))',
    },
    {
      name: 'keyword.operator.arithmetic.cm',
      match: '(\\+(?!\\+)|\\-(?!\\-)|\\*(?!=)|/(?!=)|%(?!=))',
    },
    {
      name: 'keyword.operator.bitwise.cm',
      match: '(&(?!&|=)|\\|(?!\\||=)|\\^(?!=)|~|<<|>>)',
    },
    {
      name: 'keyword.operator.assignment.cm',
      match: '(=|\\+=|\\-=|\\*=|/=|%=|&=|\\|=|\\^=|<<=|>>=)',
    },
    {
      name: 'keyword.operator.increment-decrement.cm',
      match: '(\\+\\+|\\-\\-)',
    },
    {
      name: 'keyword.operator.pointer.cm',
      match: '(\\*(?!\\*)|&(?!&)|->)',
    },
    {
      name: 'keyword.operator.fat-arrow.cm',
      match: '=>',
    },
    {
      name: 'keyword.operator.ternary.cm',
      match: '\\?|:',
    },
  ],
};

export const punctuation: TmRepositoryEntry = {
  patterns: [
    {
      name: 'punctuation.terminator.statement.cm',
      match: ';',
    },
    {
      name: 'punctuation.separator.comma.cm',
      match: ',',
    },
    {
      name: 'punctuation.accessor.cm',
      match: '\\.|::',
    },
    {
      name: 'punctuation.section.block.begin.cm',
      match: '\\{',
    },
    {
      name: 'punctuation.section.block.end.cm',
      match: '\\}',
    },
    {
      name: 'punctuation.section.brackets.begin.cm',
      match: '\\[',
    },
    {
      name: 'punctuation.section.brackets.end.cm',
      match: '\\]',
    },
    {
      name: 'punctuation.section.parens.begin.cm',
      match: '\\(',
    },
    {
      name: 'punctuation.section.parens.end.cm',
      match: '\\)',
    },
  ],
};
