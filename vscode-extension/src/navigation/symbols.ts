// ============================================================
// Cmソースのシンボル抽出（VSCode API非依存）
// ============================================================
// ホバー・定義ジャンプ・アウトライン表示のための軽量シンボルインデックスを正規表現ベースで構築する。
// コンパイラの完全なパーサではなく行単位のヒューリスティックであり、コメント・文字列を無害化した上で波括弧の深さとコンテキスト（struct/enum/interface/impl/use）を追跡する。

// ---- 公開型 ----

export type CmSymbolKind =
  | 'struct'
  | 'enum'
  | 'interface'
  | 'union'
  | 'typedef'
  | 'function'
  | 'method'
  | 'macro'
  | 'constant'
  | 'field'
  | 'variant'
  | 'module';

export interface CmSymbol {
  kind: CmSymbolKind;
  name: string;
  /// 所属コンテキスト名（implの対象型・struct名・enum名・useのライブラリ名など）
  container?: string;
  /// 宣言のシグネチャ（複数行宣言は1行へ連結、末尾の `{` / `;` は除去）
  signature: string;
  /// 直前の連続コメント行から抽出したドキュメント
  doc?: string;
  /// 宣言行（0始まり）
  line: number;
  /// 宣言行内の名前の開始位置（0始まり）
  char: number;
  /// ブロックを持つ宣言の閉じ波括弧行（アウトラインの範囲用。ブロックなしは宣言行と同じ）
  endLine: number;
}

// ---- 正規表現部品 ----

const NAME = '[A-Za-z_][A-Za-z0-9_]*';
// 型表記: 修飾名 + ジェネリック引数（1段ネストまで） + 配列 + ポインタ/参照
const TYPE = `${NAME}(?:::${NAME})*(?:\\s*<[^<>]*(?:<[^<>]*>[^<>]*)*>)?(?:\\s*\\[[^\\]]*\\])*(?:\\s*[*&])*`;
const VISIBILITY = '(?:export\\s+|private\\s+)?';
const STORAGE = '(?:static\\s+|extern\\s+|inline\\s+)*';
const GENERICS = '(?:<[^<>]*(?:<[^<>]*>[^<>]*)*>\\s*)?';

const TYPE_DECL_RE = new RegExp(`^${VISIBILITY}(struct|enum|interface|union)\\s+(${NAME})`);
const TYPEDEF_RE = new RegExp(`^${VISIBILITY}typedef\\s+(${NAME})\\s*${GENERICS}=`);
const IMPL_RE = new RegExp(`^${VISIBILITY}impl\\s*${GENERICS}\\s*(.+?)\\s*\\{?\\s*$`);
const MACRO_RE = new RegExp(`^${VISIBILITY}macro\\b.*?\\b(${NAME})\\s*=`);
const MODULE_RE = new RegExp(`^module\\s+([\\w.]+)\\s*;`);
const USE_RE = new RegExp(`^use\\s+("[^"]*"|${NAME})`);
const FUNCTION_RE = new RegExp(`^${VISIBILITY}${STORAGE}${GENERICS}(${TYPE})\\s+(${NAME})\\s*\\(`);
const CTOR_RE = /^(~?self)\s*\(/;
// 演算子宣言: `operator 戻り値型 記号(引数)`（例: operator bool ==(T other) / operator Vec2 +(Vec2 other)）
const OPERATOR_RE = new RegExp(`^${VISIBILITY}operator\\s*(?:${TYPE}\\s+)?([^\\s(]+)\\s*\\(`);
const CONSTANT_RE = new RegExp(`^${VISIBILITY}(?:const|constexpr)\\s+(${TYPE})\\s+(${NAME})\\s*=`);
const FIELD_RE = new RegExp(`^(${TYPE})\\s+(${NAME})\\s*[;=]`);
const VARIANT_RE = new RegExp(`^(${NAME})\\s*(?:[,({]|$)`);

// 関数宣言と誤認しやすい制御構文・宣言キーワードの先頭語
const STATEMENT_KEYWORDS = new Set([
  'if',
  'else',
  'while',
  'for',
  'switch',
  'case',
  'default',
  'match',
  'return',
  'break',
  'continue',
  'defer',
  'must',
  'try',
  'import',
  'module',
  'use',
  'typedef',
  'struct',
  'enum',
  'union',
  'interface',
  'impl',
  'macro',
  'operator',
  'overload',
  'template',
  'namespace',
  'sizeof',
  'typeof',
  'typename',
  'as',
  'in',
  'with',
  'where',
]);

// ---- コンテキストスタック ----

interface Context {
  type: 'struct' | 'enum' | 'interface' | 'impl' | 'use' | 'other';
  name?: string;
  /// このコンテキストを開いた宣言シンボル（閉じ波括弧でendLineを確定する）
  symbol?: CmSymbol;
}

// ---- 前処理: コメント・文字列/文字リテラルの中身を空白化（長さ維持） ----

export function sanitizeSource(text: string): string[] {
  const out: string[] = [];
  let carryBlockComment = false;
  for (const rawLine of text.split(/\r?\n/)) {
    let line = '';
    let i = 0;
    let state: 'code' | 'block-comment' | 'string' | 'char' = carryBlockComment
      ? 'block-comment'
      : 'code';
    while (i < rawLine.length) {
      const c = rawLine[i];
      const next = rawLine[i + 1];
      if (state === 'code') {
        if (c === '/' && next === '/') {
          line += ' '.repeat(rawLine.length - i);
          i = rawLine.length;
        } else if (c === '/' && next === '*') {
          state = 'block-comment';
          line += '  ';
          i += 2;
        } else if (c === '"') {
          state = 'string';
          line += '"';
          i++;
        } else if (c === "'") {
          state = 'char';
          line += "'";
          i++;
        } else {
          line += c;
          i++;
        }
      } else if (state === 'block-comment') {
        if (c === '*' && next === '/') {
          state = 'code';
          line += '  ';
          i += 2;
        } else {
          line += ' ';
          i++;
        }
      } else {
        // string / char: エスケープを考慮して中身を空白化
        if (c === '\\') {
          line += '  ';
          i += 2;
        } else if ((state === 'string' && c === '"') || (state === 'char' && c === "'")) {
          line += c;
          state = 'code';
          i++;
        } else {
          line += ' ';
          i++;
        }
      }
    }
    // 文字列・文字リテラルは行をまたがない前提で閉じる（ブロックコメントのみ持ち越す）
    carryBlockComment = state === 'block-comment';
    out.push(line);
  }
  return out;
}

// ---- ドキュメントコメント抽出 ----

function extractDoc(rawLines: string[], declLine: number): string | undefined {
  const docs: string[] = [];
  for (let i = declLine - 1; i >= 0; i--) {
    const t = rawLines[i].trim();
    if (/^#\[/.test(t)) {
      continue; // 属性行はドキュメントと宣言の間に挟まってよい
    }
    const m = /^\/\/[/!]?\s?(.*)$/.exec(t);
    if (!m) {
      break;
    }
    docs.unshift(m[1]);
  }
  // 区切り線だけのコメント（====等）は除外する
  const meaningful = docs.filter((d) => !/^[=\-*]{4,}\s*$/.test(d));
  const joined = meaningful.join('\n').trim();
  return joined.length > 0 ? joined : undefined;
}

// ---- シグネチャ抽出（複数行宣言を1行へ連結） ----

// 引数リストが閉じるまで後続行を連結した宣言文字列を返す（暴走防止に最大4行）
function joinDeclLines(cleanLines: string[], declLine: number): string {
  let sig = cleanLines[declLine].trim();
  let open = 0;
  for (const c of sig) {
    if (c === '(') open++;
    else if (c === ')') open--;
  }
  let line = declLine;
  while (open > 0 && line - declLine < 4 && line + 1 < cleanLines.length) {
    line++;
    const next = cleanLines[line].trim();
    for (const c of next) {
      if (c === '(') open++;
      else if (c === ')') open--;
    }
    sig += ` ${next}`;
  }
  return sig;
}

function extractSignature(cleanLines: string[], declLine: number): string {
  return joinDeclLines(cleanLines, declLine)
    .replace(/\s*\{\s*$/, '')
    .replace(/;\s*$/, '')
    .replace(/\s+/g, ' ')
    .trim();
}

// implの対象型名。Cmのimpl-forは `impl 対象型 for インターフェース` のため for の前が対象型
// （`Point for Printable` → `Point`、inherent implの `HashMap<K, V>` → `HashMap`）
function implTargetName(target: string): string {
  const beforeFor = target.includes(' for ') ? target.split(' for ')[0] : target;
  const base = beforeFor.split(/\s+where\s+/)[0].trim();
  const m = new RegExp(`^(${NAME})`).exec(base);
  return m ? m[1] : base;
}

// ---- 本体 ----

export function extractSymbols(text: string): CmSymbol[] {
  const rawLines = text.split(/\r?\n/);
  const cleanLines = sanitizeSource(text);
  const symbols: CmSymbol[] = [];
  const stack: Context[] = [];
  let pending: Context | undefined;

  const push = (kind: CmSymbolKind, name: string, lineNo: number, container?: string): CmSymbol => {
    const sym: CmSymbol = {
      kind,
      name,
      container,
      signature: extractSignature(cleanLines, lineNo),
      doc: extractDoc(rawLines, lineNo),
      line: lineNo,
      char: Math.max(0, rawLines[lineNo].indexOf(name)),
      endLine: lineNo,
    };
    symbols.push(sym);
    return sym;
  };

  for (let lineNo = 0; lineNo < cleanLines.length; lineNo++) {
    const trimmed = cleanLines[lineNo].trim();
    const top: Context | undefined = stack[stack.length - 1];
    const atTop = stack.length === 0;
    // 宣言のマッチはトップレベルか既知のコンテキスト直下のみ（関数本体などの'other'内は対象外）
    const matchable = atTop || top.type !== 'other';

    if (trimmed.length > 0 && matchable) {
      const firstWord = /^[A-Za-z_~][A-Za-z0-9_]*/.exec(trimmed)?.[0] ?? '';
      let m: RegExpExecArray | null;
      if (atTop && (m = MODULE_RE.exec(trimmed))) {
        push('module', m[1], lineNo);
      } else if (atTop && (m = TYPE_DECL_RE.exec(trimmed))) {
        const kind = m[1] as CmSymbolKind;
        const sym = push(kind, m[2], lineNo);
        const ctxType = m[1] === 'union' ? 'other' : (m[1] as Context['type']);
        pending = { type: ctxType, name: m[2], symbol: sym };
      } else if (atTop && (m = TYPEDEF_RE.exec(trimmed))) {
        push('typedef', m[1], lineNo);
      } else if (atTop && /^(export\s+)?impl\b/.test(trimmed) && (m = IMPL_RE.exec(trimmed))) {
        pending = { type: 'impl', name: implTargetName(m[1]) };
      } else if (atTop && trimmed.includes('{') && (m = USE_RE.exec(trimmed))) {
        pending = { type: 'use', name: m[1].replace(/"/g, '') };
      } else if (atTop && (m = MACRO_RE.exec(trimmed))) {
        push('macro', m[1], lineNo);
      } else if (atTop && (m = CONSTANT_RE.exec(trimmed))) {
        push('constant', m[2], lineNo);
      } else if (top?.type === 'impl' && (m = CTOR_RE.exec(trimmed))) {
        pending = { type: 'other', symbol: push('method', m[1], lineNo, top.name) };
      } else if (
        (top?.type === 'impl' || top?.type === 'struct' || top?.type === 'interface') &&
        (m = OPERATOR_RE.exec(trimmed))
      ) {
        const sym = push('method', `operator${m[1]}`, lineNo, top.name);
        if (!/;\s*$/.test(joinDeclLines(cleanLines, lineNo))) {
          pending = { type: 'other', symbol: sym };
        }
      } else if (!STATEMENT_KEYWORDS.has(firstWord) && (m = FUNCTION_RE.exec(trimmed))) {
        const isMethod =
          top?.type === 'impl' || top?.type === 'struct' || top?.type === 'interface';
        const sym = push(isMethod ? 'method' : 'function', m[2], lineNo, top?.name);
        if (!/;\s*$/.test(joinDeclLines(cleanLines, lineNo))) {
          pending = { type: 'other', symbol: sym };
        }
      } else if (
        top?.type === 'struct' &&
        !STATEMENT_KEYWORDS.has(firstWord) &&
        (m = FIELD_RE.exec(trimmed))
      ) {
        push('field', m[2], lineNo, top.name);
      } else if (
        top?.type === 'enum' &&
        !STATEMENT_KEYWORDS.has(firstWord) &&
        (m = VARIANT_RE.exec(trimmed))
      ) {
        push('variant', m[1], lineNo, top.name);
      }
    }

    // 波括弧の深さ追跡とコンテキストの開閉
    for (const c of cleanLines[lineNo]) {
      if (c === '{') {
        stack.push(pending ?? { type: 'other' });
        pending = undefined;
      } else if (c === '}') {
        const closed = stack.pop();
        if (closed?.symbol) {
          closed.symbol.endLine = lineNo;
        }
      }
    }
    // ブロックを開かずに文が終わった場合はpendingを破棄する（前方宣言・プロトタイプ等）
    if (pending && /;\s*$/.test(trimmed) && !trimmed.includes('{')) {
      pending = undefined;
    }
  }
  return symbols;
}

// ---- 検索補助 ----

// 型に属するメンバ（メソッド・フィールド・列挙子）の種別
const MEMBER_KINDS: ReadonlySet<CmSymbolKind> = new Set(['method', 'field', 'variant']);

/// メンバアクセス（`obj.x`）でのみ解決対象となる種別か判定する
export function isMemberKind(kind: CmSymbolKind): boolean {
  return MEMBER_KINDS.has(kind);
}

/// 主要シンボル（型・関数など）を優先し、なければフィールド・バリアントを返す
export function rankMatches(matches: CmSymbol[]): CmSymbol[] {
  const primary = matches.filter((s) => s.kind !== 'field' && s.kind !== 'variant');
  return primary.length > 0 ? primary : matches;
}
