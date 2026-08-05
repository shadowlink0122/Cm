// ============================================================
// 語彙・正規表現断片の単一ソース
// ============================================================
// キーワード・型・組み込み関数などの語彙一覧はこのファイルだけで管理する。
// 新しい構文を追加するときはここへ語を足し、`npm run build:grammar` でsyntaxes/cm.tmLanguage.jsonを再生成する。

import { TmRule } from './tmTypes';

// 語のリストを \b(a|b|c)\b 形式の単語境界付き選択肢へ変換する
export function words(list: readonly string[]): string {
  return `\\b(${list.join('|')})\\b`;
}

// ---- 識別子 ----
// 歴史的経緯で2表記が混在しているが意味は同一（意図的に統一しない場合は出力互換のため両方を保持する）
export const IDENT = '[a-zA-Z_][a-zA-Z0-9_]*';
export const IDENT_ALT = '[A-Za-z_][A-Za-z0-9_]*';

// ---- 制御キーワード ----
export const CONTROL_CONDITIONAL = ['if', 'else', 'switch', 'case', 'default', 'match'] as const;
export const CONTROL_REPEAT = ['for', 'in', 'while', 'break', 'continue'] as const;
export const CONTROL_RETURN = ['return', 'defer'] as const;
export const CONTROL_FLOW = ['async', 'await'] as const;
export const CONTROL_EXPORT = ['export', 'private'] as const;
export const CONTROL_EXCEPTION = ['try', 'must'] as const;

// ---- 宣言キーワード ----
// DECLARATION_KEYWORDSは文中のキーワード着色用、TYPE_DECLARATION_KEYWORDSは直後の識別子を型名として拾う宣言頭のみ（unionを含む）
export const DECLARATION_KEYWORDS = [
  'typedef',
  'struct',
  'enum',
  'interface',
  'impl',
  'with',
  'module',
  'macro',
  'namespace',
  'overload',
  'operator',
  'template',
] as const;
export const TYPE_DECLARATION_KEYWORDS = [
  'struct',
  'enum',
  'interface',
  'impl',
  'union',
  'typedef',
] as const;

// ---- 修飾子・演算子的キーワード ----
export const MODIFIER_KEYWORDS = [
  'const',
  'constexpr',
  'static',
  'extern',
  'inline',
  'volatile',
  'move',
  'as',
  'is',
  'from',
  'where',
  'auto',
  'var',
] as const;
export const SIZEOF_KEYWORDS = ['sizeof', 'typeof', 'typename'] as const;
export const INTRINSIC_KEYWORDS = [
  '__asm__',
  '__sizeof__',
  '__typeof__',
  '__typename__',
  '__alignof__',
] as const;

// ---- SystemVerilog向けキーワード ----
export const SV_CONTROL_KEYWORDS = ['assign'] as const;
export const SV_MODIFIER_KEYWORDS = [
  'always_comb',
  'always_ff',
  'always_latch',
  'always',
  'initial',
  'posedge',
  'negedge',
] as const;

// ---- 型・定数・組み込み関数 ----
export const PRIMITIVE_TYPES = [
  'tiny',
  'utiny',
  'short',
  'ushort',
  'int',
  'uint',
  'long',
  'ulong',
  'isize',
  'usize',
  'float',
  'ufloat',
  'double',
  'udouble',
  'char',
  'string',
  'bool',
  'void',
  'bit',
] as const;
export const BOOLEAN_CONSTANTS = ['true', 'false'] as const;
export const NULL_CONSTANTS = ['nullptr', 'null'] as const;
// Optionの値なしバリアント。bareのNoneは型名（PascalCase→entity.name.type）でなくプリミティブ型と同色で着色する
export const PRELUDE_VALUE_VARIANTS = ['None'] as const;
export const BUILTIN_FUNCTIONS = [
  'print',
  'println',
  'printf',
  'assert',
  'panic',
  'step',
  'exit',
  'eprintln',
  'eprint',
] as const;
export const PREPROCESSOR_BUILTINS = [
  'FILE',
  'LINE',
  'DATE',
  'TIME',
  'VERSION',
  'PLATFORM',
  'ARCH',
  'OS',
] as const;
export const ASM_CONSTRAINT_KEYWORDS = ['in', 'out', 'inout', 'clobber'] as const;

// ---- 共有ルール断片 ----
// 文字列内のエスケープシーケンス（"..." / asmブロック内文字列 / use文の文字列で共用）
export const ESCAPE_RULE: TmRule = {
  name: 'constant.character.escape.cm',
  match: '\\\\.',
};

// モジュール名（module/import/use文で共用）
export const MODULE_NAME_RULE: TmRule = {
  name: 'entity.name.type.module.cm',
  match: IDENT,
};

// プロトタイプ宣言の関数名（`name(...);` 形式。function-declarationとuse文で共用）
export const FUNCTION_PROTOTYPE_MATCH = `\\b(${IDENT})\\s*(?=\\([^)]*\\)\\s*;)`;

// コード文脈で使うincludeの共通並び。
// トップレベル・バッククォートブロック・文字列補間の3箇所で共用し、文脈差分はオプションで表現する。
export function codeIncludes(options: {
  comments?: boolean;
  functionDeclaration?: boolean;
  punctuation: '#punctuation' | '#string-interpolation-punctuation';
}): TmRule[] {
  const rules: TmRule[] = [];
  if (options.comments) {
    rules.push({ include: '#comments' });
  }
  rules.push(
    { include: '#keywords' },
    { include: '#constants' },
    { include: '#builtin-functions' },
  );
  if (options.functionDeclaration) {
    rules.push({ include: '#function-declaration' });
  }
  rules.push(
    { include: '#function-call' },
    { include: '#types' },
    { include: '#variables' },
    { include: '#operators' },
    { include: '#numbers' },
    { include: options.punctuation },
  );
  return rules;
}
