// ============================================================
// Cm文法の組み立て
// ============================================================
// repositoryのキー順・トップレベルpatternsの並びはマッチ優先順位を意味するため、変更時は既存順を崩さないこと。

import { TmGrammar } from './tmTypes';
import { attributes, platformDirective, preprocessor } from './repository/directives';
import { importStatement, moduleStatement, useStatement } from './repository/modules';
import { comments, constants, numbers, strings } from './repository/literals';
import {
  builtinFunctions,
  functionCall,
  functionDeclaration,
  keywords,
  operators,
  punctuation,
  typeDeclaration,
  types,
  variables,
} from './repository/code';
import {
  asmBlock,
  backtickBlock,
  interpolationExpression,
  stringInterpolationPunctuation,
} from './repository/embedded';

export function buildGrammar(): TmGrammar {
  return {
    $schema: 'https://raw.githubusercontent.com/martinring/tmlanguage/master/tmlanguage.json',
    name: 'Cm',
    scopeName: 'source.cm',
    fileTypes: ['cm'],
    patterns: [
      { include: '#platform-directive' },
      { include: '#preprocessor' },
      { include: '#comments' },
      { include: '#backtick-block' },
      { include: '#strings' },
      { include: '#attributes' },
      { include: '#import-statement' },
      { include: '#use-statement' },
      { include: '#module-statement' },
      { include: '#asm-block' },
      { include: '#type-declaration' },
      { include: '#keywords' },
      { include: '#constants' },
      { include: '#builtin-functions' },
      { include: '#function-declaration' },
      { include: '#function-call' },
      { include: '#types' },
      { include: '#variables' },
      { include: '#operators' },
      { include: '#numbers' },
      { include: '#punctuation' },
    ],
    repository: {
      'platform-directive': platformDirective,
      preprocessor: preprocessor,
      'module-statement': moduleStatement,
      'import-statement': importStatement,
      'use-statement': useStatement,
      'asm-block': asmBlock,
      comments: comments,
      'backtick-block': backtickBlock,
      strings: strings,
      keywords: keywords,
      'type-declaration': typeDeclaration,
      types: types,
      constants: constants,
      'builtin-functions': builtinFunctions,
      'function-declaration': functionDeclaration,
      'function-call': functionCall,
      variables: variables,
      operators: operators,
      numbers: numbers,
      punctuation: punctuation,
      'interpolation-expression': interpolationExpression,
      'string-interpolation-punctuation': stringInterpolationPunctuation,
      attributes: attributes,
    },
  };
}

// 生成JSONのシリアライズ形式（2スペースインデント + 末尾改行）
export function renderGrammar(): string {
  return JSON.stringify(buildGrammar(), null, 2) + '\n';
}
