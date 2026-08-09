// ============================================================
// 候補シンボルの絞り込み（呼び出し文脈・到達性）
// ============================================================
// 正規表現インデックスは同名シンボルを全件返すため、参照側の文脈で候補を絞り一意性を高める。
// 型推論は行わないので完全な一意化はできないが、(1) 呼び出し文脈の種別ゲート と (2) ファイル到達性 で実用上の曖昧さを大きく減らす。

import * as path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { CmSymbolKind, isMemberKind } from '../navigation/symbols';
import { AccessKind } from './words';

// ---- 呼び出し文脈による種別ゲート ----

// 呼び出し文脈に合致しない候補を除外する（`write()` に全メソッド定義が混ざる問題への対処）
// - member    (`obj.write`): メソッド・フィールド・列挙子のみ。合致が無ければ空を返しビルトインメソッド解決へ委ねる
// - bare      (`write()`)  : トップレベル宣言のみ（メソッド等を除外）。合致が無ければ従来どおり全件を返す
// - qualified (`T::write`) : 種別で絞らない（既に修飾で曖昧性が低いため）
export function scopeByAccess<T extends { symbol: { kind: CmSymbolKind } }>(
  entries: T[],
  access: AccessKind,
): T[] {
  if (access === 'member') {
    return entries.filter((e) => isMemberKind(e.symbol.kind));
  }
  if (access === 'bare') {
    const topLevel = entries.filter((e) => !isMemberKind(e.symbol.kind));
    return topLevel.length > 0 ? topLevel : entries;
  }
  return entries;
}

// ---- ファイル到達性による絞り込み ----

// 相対パスimport（`import ./path/module::{...};` / `import ../m;` / `import ./m as X;`）の先頭パス部分を切り出す
// 名前空間import（`import std::io::x;`）はモジュール解決を要するため対象外
const RELATIVE_IMPORT_RE = /^\s*import\s+(\.\.?\/[^\s;:]+)/;

// 現在ファイルが相対パスimportで直接参照する.cmファイルのURI集合を求める
export function importedUris(text: string, documentUri: string): Set<string> {
  const result = new Set<string>();
  let baseDir: string;
  try {
    baseDir = path.dirname(fileURLToPath(documentUri));
  } catch {
    return result; // file以外のURIはパス解決不能
  }
  for (const line of text.split(/\r?\n/)) {
    const m = RELATIVE_IMPORT_RE.exec(line);
    if (!m) {
      continue;
    }
    const resolved = path.resolve(baseDir, `${m[1]}.cm`);
    result.add(pathToFileURL(resolved).toString());
  }
  return result;
}

// 参照元ファイルからの到達性で候補を絞る（最初の非空層を採用する）
// 1. 同一ファイル（ローカル定義はスコープを遮蔽するので常に最優先）
// 2. 相対パスimportで到達可能なファイル
// 3. 上記が空ならワークスペース全体（発見性のためのフォールバック）
export function scopeByReachability<T extends { uri: string }>(
  entries: T[],
  documentUri: string,
  text: string,
): T[] {
  if (entries.length <= 1) {
    return entries;
  }
  const sameFile = entries.filter((e) => e.uri === documentUri);
  if (sameFile.length > 0) {
    return sameFile;
  }
  const imported = importedUris(text, documentUri);
  if (imported.size > 0) {
    const reachable = entries.filter((e) => imported.has(e.uri));
    if (reachable.length > 0) {
      return reachable;
    }
  }
  return entries;
}
