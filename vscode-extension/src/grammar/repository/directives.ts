// ============================================================
// ディレクティブ系: プラットフォーム指示・プリプロセッサ・属性
// ============================================================

import { TmRepositoryEntry } from '../tmTypes';
import { IDENT, IDENT_ALT, PREPROCESSOR_BUILTINS } from '../terms';

export const platformDirective: TmRepositoryEntry = {
  patterns: [
    {
      name: 'meta.preprocessor.platform.cm',
      match: '^\\s*(//!)\\s*([\\w-]+)\\s*:\\s*(.+)$',
      captures: {
        '1': { name: 'punctuation.definition.directive.cm' },
        '2': { name: 'keyword.control.directive.cm' },
        '3': { name: 'entity.name.tag.cm' },
      },
    },
  ],
};

export const preprocessor: TmRepositoryEntry = {
  patterns: [
    {
      name: 'meta.preprocessor.macro.cm',
      begin: '^\\s*(#)\\s*(define|undef)\\b',
      end: '$',
      beginCaptures: {
        '1': { name: 'punctuation.definition.directive.cm storage.type.directive.cm' },
        '2': { name: 'keyword.control.directive.cm' },
      },
      patterns: [
        {
          name: 'entity.name.function.preprocessor.cm',
          match: IDENT,
        },
      ],
    },
    {
      name: 'meta.preprocessor.conditional.cm',
      match: `^\\s*(#)\\s*(ifdef|ifndef|elseif|elif|else|endif|end|if)\\b\\s*(${IDENT_ALT})?`,
      captures: {
        '1': { name: 'punctuation.definition.directive.cm storage.type.directive.cm' },
        '2': { name: 'keyword.control.directive.cm' },
        '3': { name: 'storage.type.primitive.cm' },
      },
    },
    {
      name: 'meta.preprocessor.diagnostic.cm',
      begin: '^\\s*(#)\\s*(error|warning)\\b',
      end: '$',
      beginCaptures: {
        '1': { name: 'punctuation.definition.directive.cm storage.type.directive.cm' },
        '2': { name: 'keyword.control.directive.cm' },
      },
      patterns: [
        {
          name: 'string.unquoted.cm',
          match: '.+',
        },
      ],
    },
    {
      name: 'meta.preprocessor.include.cm',
      match: '^\\s*(#)\\s*(include)\\b',
      captures: {
        '1': { name: 'punctuation.definition.directive.cm storage.type.directive.cm' },
        '2': { name: 'keyword.control.directive.cm' },
      },
    },
    {
      name: 'meta.preprocessor.other.cm',
      match: `^\\s*(#)\\s*(${IDENT_ALT})\\b`,
      captures: {
        '1': { name: 'punctuation.definition.directive.cm storage.type.directive.cm' },
        '2': { name: 'keyword.control.directive.cm' },
      },
    },
    {
      name: 'constant.language.preprocessor.cm',
      match: `\\b__(${PREPROCESSOR_BUILTINS.join('|')})__\\b`,
    },
  ],
};

export const attributes: TmRepositoryEntry = {
  patterns: [
    {
      name: 'meta.attribute.derive.cm',
      begin: '(#)(\\[)\\s*(derive)\\s*(\\()',
      end: '(\\))\\s*(\\])|(?=$)',
      beginCaptures: {
        '1': { name: 'punctuation.definition.attribute.cm storage.type.directive.cm' },
        '2': { name: 'entity.name.function.attribute.bracket.cm' },
        '3': { name: 'keyword.control.attribute.cm' },
        '4': { name: 'punctuation.section.arguments.begin.cm' },
      },
      endCaptures: {
        '1': { name: 'punctuation.section.arguments.end.cm' },
        '2': { name: 'entity.name.function.attribute.bracket.cm' },
      },
      patterns: [
        {
          name: 'entity.name.type.interface.cm',
          match: IDENT_ALT,
        },
        {
          name: 'punctuation.separator.comma.cm',
          match: ',',
        },
      ],
    },
    {
      name: 'meta.attribute.cm',
      begin: '(#)(\\[)',
      end: '(\\])|(?=$)',
      beginCaptures: {
        '1': { name: 'punctuation.definition.attribute.cm storage.type.directive.cm' },
        '2': { name: 'entity.name.function.attribute.bracket.cm' },
      },
      endCaptures: {
        '1': { name: 'entity.name.function.attribute.bracket.cm' },
      },
      patterns: [
        {
          name: 'punctuation.separator.namespace.cm',
          match: '::',
        },
        {
          name: 'meta.attribute.arguments.cm',
          begin: '\\(',
          end: '\\)|(?=$)',
          beginCaptures: {
            '0': { name: 'punctuation.section.arguments.begin.cm' },
          },
          endCaptures: {
            '0': { name: 'punctuation.section.arguments.end.cm' },
          },
          patterns: [
            {
              match: `(${IDENT_ALT})\\s*(:)(?!:)`,
              captures: {
                '1': { name: 'variable.other.member.cm' },
                '2': { name: 'punctuation.separator.key-value.cm' },
              },
            },
            { include: '#strings' },
            { include: '#numbers' },
            { include: '#constants' },
            {
              name: 'punctuation.separator.comma.cm',
              match: ',',
            },
          ],
        },
        {
          name: 'keyword.control.attribute.cm',
          match: '[^\\]:(]+',
        },
        {
          name: 'keyword.control.attribute.cm',
          match: ':',
        },
      ],
    },
  ],
};
