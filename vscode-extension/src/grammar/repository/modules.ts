// ============================================================
// モジュール系: module / import / use 文
// ============================================================

import { TmRepositoryEntry } from '../tmTypes';
import { ESCAPE_RULE, FUNCTION_PROTOTYPE_MATCH, IDENT, MODULE_NAME_RULE } from '../terms';

export const moduleStatement: TmRepositoryEntry = {
  patterns: [
    {
      begin: '\\b(module)\\s+',
      end: ';',
      beginCaptures: {
        '1': { name: 'keyword.control.module.cm' },
      },
      endCaptures: {
        '0': { name: 'punctuation.terminator.statement.cm' },
      },
      patterns: [
        MODULE_NAME_RULE,
        {
          name: 'punctuation.accessor.cm',
          match: '\\.',
        },
      ],
    },
  ],
};

export const importStatement: TmRepositoryEntry = {
  patterns: [
    {
      begin: '\\b(import)\\s+',
      end: ';',
      beginCaptures: {
        '1': { name: 'keyword.control.import.cm' },
      },
      endCaptures: {
        '0': { name: 'punctuation.terminator.statement.cm' },
      },
      patterns: [
        {
          begin: '\\{',
          end: '\\}',
          beginCaptures: {
            '0': { name: 'punctuation.definition.imports.begin.cm' },
          },
          endCaptures: {
            '0': { name: 'punctuation.definition.imports.end.cm' },
          },
          patterns: [
            {
              name: 'variable.other.readwrite.cm',
              match: IDENT,
            },
            {
              name: 'punctuation.separator.comma.cm',
              match: ',',
            },
          ],
        },
        MODULE_NAME_RULE,
        {
          name: 'punctuation.accessor.cm',
          match: '\\.|::',
        },
      ],
    },
  ],
};

export const useStatement: TmRepositoryEntry = {
  patterns: [
    {
      begin: '\\b(use)\\s+',
      end: '\\}|;',
      beginCaptures: {
        '1': { name: 'keyword.control.directive.cm' },
      },
      endCaptures: {
        '0': { name: 'punctuation.section.block.end.cm' },
      },
      patterns: [
        {
          name: 'string.quoted.double.cm',
          begin: '"',
          end: '"',
          patterns: [ESCAPE_RULE],
        },
        {
          name: 'entity.name.type.module.cm',
          match: `${IDENT}(?=\\s*\\{)`,
        },
        {
          name: 'entity.name.type.module.cm',
          match: '\\b(js|libc)\\b',
        },
        {
          name: 'punctuation.section.block.begin.cm',
          match: '\\{',
        },
        { include: '#comments' },
        {
          match: FUNCTION_PROTOTYPE_MATCH,
          captures: {
            '1': { name: 'entity.name.function.cm' },
          },
        },
        { include: '#types' },
        MODULE_NAME_RULE,
        {
          name: 'punctuation.accessor.cm',
          match: '\\.',
        },
      ],
    },
  ],
};
