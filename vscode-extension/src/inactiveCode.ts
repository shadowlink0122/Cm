// ============================================================
// 非アクティブコード判定ロジック（VSCode API非依存）
// ============================================================
// 明らかに無効なスコープ（未定義シンボルの #ifdef、定義済みシンボルの #ifndef、リテラル false の if 等）の行範囲を求める。
// 判定できない条件（マシン依存の __x86_64__ 等）は減光しない。
// vscodeモジュールに依存しないためNode単体でユニットテストできる（拡張本体はextension.tsでvscode.Rangeへ変換する）。

// __NAME__ 形式は処理系・環境依存の組み込み定義（真偽をエディタで判定できない）
const BUILTIN_LIKE = /^__[A-Za-z0-9_]+__$/;

// エディタ上で常に定義済みとみなすシンボル
const ALWAYS_DEFINED = new Set(['__CM__']);

// vscode.Rangeと同形の位置情報（行・桁は0始まり）
export interface InactiveRange {
  startLine: number;
  startChar: number;
  endLine: number;
  endChar: number;
}

function collectFileDefines(lines: string[]): Set<string> {
  const defines = new Set<string>();
  for (const line of lines) {
    const m = line.match(/^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)/);
    if (m) {
      defines.add(m[1]);
    }
    const u = line.match(/^\s*#\s*undef\s+([A-Za-z_][A-Za-z0-9_]*)/);
    if (u) {
      defines.delete(u[1]);
    }
  }
  return defines;
}

type CondState = 'active' | 'inactive' | 'unknown';

interface Frame {
  branchState: CondState; // 現在の分岐の状態
  condKnownTrue: boolean; // #ifdef/#ifndef の条件が「真と確定」か（#else側の判定用）
  dimStart: number | null; // 減光開始行（分岐内容の先頭）
}

function evalIfdef(
  symbol: string,
  negated: boolean,
  defined: Set<string>,
): { state: CondState; knownTrue: boolean } {
  const isDefined = defined.has(symbol) || ALWAYS_DEFINED.has(symbol);
  const isUnknown = !isDefined && BUILTIN_LIKE.test(symbol);
  if (isUnknown) {
    return { state: 'unknown', knownTrue: false };
  }
  const truthy = negated ? !isDefined : isDefined;
  return { state: truthy ? 'active' : 'inactive', knownTrue: truthy };
}

/**
 * 非アクティブ領域（行範囲）を求める。
 * - `#ifdef X`: X が未定義と確定していれば本体を減光
 * - `#ifndef X`: X が定義済みなら本体を減光
 * - `#else`: 対応する条件が真と確定していれば else 側を減光
 * - `if (false)` / `if (0)`: ブロック本体を減光
 */
export function findInactiveRanges(text: string, activeDefines: string[]): InactiveRange[] {
  const lines = text.split(/\r?\n/);
  const defined = collectFileDefines(lines);
  for (const d of activeDefines) {
    defined.add(d);
  }

  const ranges: InactiveRange[] = [];
  const stack: Frame[] = [];

  const pushDim = (startLine: number, endLine: number) => {
    if (endLine >= startLine) {
      ranges.push({
        startLine,
        startChar: 0,
        endLine,
        endChar: lines[endLine]?.length ?? 0,
      });
    }
  };

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    const ifdef = line.match(/^\s*#\s*(ifdef|ifndef)\s+([A-Za-z_][A-Za-z0-9_]*)/);
    if (ifdef) {
      const parentInactive = stack.some((f) => f.branchState === 'inactive');
      if (parentInactive) {
        // 親が減光済みならネスト内は判定不要（範囲は親がカバー）
        stack.push({ branchState: 'active', condKnownTrue: false, dimStart: null });
        continue;
      }
      const { state, knownTrue } = evalIfdef(ifdef[2], ifdef[1] === 'ifndef', defined);
      stack.push({
        branchState: state,
        condKnownTrue: knownTrue,
        dimStart: state === 'inactive' ? i + 1 : null,
      });
      continue;
    }
    if (/^\s*#\s*else\b/.test(line)) {
      const frame = stack[stack.length - 1];
      if (frame) {
        if (frame.branchState === 'inactive' && frame.dimStart !== null) {
          pushDim(frame.dimStart, i - 1);
        }
        // 条件が真と確定していた場合、else側は不到達
        frame.branchState = frame.condKnownTrue ? 'inactive' : 'active';
        frame.dimStart = frame.branchState === 'inactive' ? i + 1 : null;
      }
      continue;
    }
    if (/^\s*#\s*end\b/.test(line)) {
      const frame = stack.pop();
      if (frame && frame.branchState === 'inactive' && frame.dimStart !== null) {
        pushDim(frame.dimStart, i - 1);
      }
      continue;
    }

    // リテラル false の if ブロック（親が減光済みならスキップ）
    const falseIf = line.match(/\b(if|while)\s*\(\s*(false|0)\s*\)\s*\{/);
    if (falseIf && !stack.some((f) => f.branchState === 'inactive')) {
      const startCol = (falseIf.index ?? 0) + falseIf[0].length;
      let depth = 1;
      let endLine = i;
      let endCol = line.length;
      const col = startCol;
      outer: for (let j = i; j < lines.length; j++) {
        const l = lines[j];
        for (let k = j === i ? col : 0; k < l.length; k++) {
          const ch = l[k];
          if (ch === '{') {
            depth++;
          } else if (ch === '}') {
            depth--;
            if (depth === 0) {
              endLine = j;
              endCol = k;
              break outer;
            }
          }
        }
      }
      ranges.push({
        startLine: i,
        startChar: falseIf.index ?? 0,
        endLine,
        endChar: endCol + 1,
      });
    }
  }
  return ranges;
}
