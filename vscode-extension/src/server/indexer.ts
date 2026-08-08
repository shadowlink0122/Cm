// ============================================================
// ワークスペース全.cmファイルのシンボルインデックス（fsベース）
// ============================================================
// 初回参照時に一度だけワークスペースを走査し、以後はdidOpen/didChange・ファイルウォッチの通知でファイル単位に差し替える。シンボル抽出はnavigation/symbols.ts（正規表現ベース・エディタ非依存）を再利用する。

import { promises as fs } from 'node:fs';
import * as path from 'node:path';
import { pathToFileURL } from 'node:url';
import { CmSymbol, extractSymbols } from '../navigation/symbols';

// 走査から除外するディレクトリ（依存物・生成物・一時ファイル置き場）
const EXCLUDED_DIRS = new Set(['node_modules', 'build', 'out', 'dist', 'target']);
const MAX_INDEX_FILES = 20000;

export interface IndexedSymbol {
  uri: string;
  symbol: CmSymbol;
}

export class WorkspaceIndex {
  private readonly files = new Map<string, CmSymbol[]>();
  private roots: string[] = [];
  private buildPromise: Promise<void> | undefined;

  // initialize時に受け取ったワークスペースフォルダ（fsパス）を設定する
  setRoots(roots: string[]): void {
    this.roots = roots;
  }

  getRoots(): string[] {
    return this.roots;
  }

  // 初回参照時に一度だけ全ルートを走査する（以後はイベント駆動の差分更新）
  ensureBuilt(): Promise<void> {
    if (!this.buildPromise) {
      this.buildPromise = this.buildAll();
    }
    return this.buildPromise;
  }

  private async buildAll(): Promise<void> {
    let remaining = MAX_INDEX_FILES;
    for (const root of this.roots) {
      remaining = await this.walk(root, remaining);
      if (remaining <= 0) {
        return;
      }
    }
  }

  // ディレクトリを再帰走査して.cmファイルをインデックスへ登録する（シンボリックリンクは循環防止のため辿らない）
  private async walk(dir: string, remaining: number): Promise<number> {
    let entries;
    try {
      entries = await fs.readdir(dir, { withFileTypes: true });
    } catch {
      return remaining; // 読み取り不能なディレクトリは対象外
    }
    for (const entry of entries) {
      if (remaining <= 0) {
        return 0;
      }
      if (entry.isSymbolicLink()) {
        continue;
      }
      const full = path.join(dir, entry.name);
      if (entry.isDirectory()) {
        if (entry.name.startsWith('.') || EXCLUDED_DIRS.has(entry.name)) {
          continue;
        }
        remaining = await this.walk(full, remaining);
      } else if (entry.isFile() && entry.name.endsWith('.cm')) {
        try {
          const text = await fs.readFile(full, 'utf8');
          this.setFile(pathToFileURL(full).toString(), text);
          remaining--;
        } catch {
          // 読み取り不能なファイルはインデックス対象外とする
        }
      }
    }
    return remaining;
  }

  setFile(uri: string, text: string): void {
    this.files.set(uri, extractSymbols(text));
  }

  removeFile(uri: string): void {
    this.files.delete(uri);
  }

  // ウォッチ通知等でディスク上の内容を反映する（消えていればインデックスから外す）
  async loadFromDisk(uri: string, toPath: (uri: string) => string): Promise<void> {
    try {
      const text = await fs.readFile(toPath(uri), 'utf8');
      this.setFile(uri, text);
    } catch {
      this.removeFile(uri);
    }
  }

  lookup(name: string): IndexedSymbol[] {
    const result: IndexedSymbol[] = [];
    for (const [uri, symbols] of this.files) {
      for (const symbol of symbols) {
        if (symbol.name === name) {
          result.push({ uri, symbol });
        }
      }
    }
    return result;
  }

  allSymbols(): IndexedSymbol[] {
    const result: IndexedSymbol[] = [];
    for (const [uri, symbols] of this.files) {
      for (const symbol of symbols) {
        result.push({ uri, symbol });
      }
    }
    return result;
  }
}
