// ============================================================
// TextMate文法の最小型定義
// ============================================================
// cm.tmLanguage.json生成に必要な範囲のみを定義する（tmlanguageスキーマの完全な再現はしない）。
// キーの宣言順がそのままJSON出力順になるため、プロパティ順は実ファイルの慣例（comment→name→begin/end→captures→patterns）に合わせている。

export interface TmCaptures {
  [index: string]: { name: string } | { patterns: TmRule[] };
}

export interface TmRule {
  comment?: string;
  name?: string;
  match?: string;
  begin?: string;
  end?: string;
  captures?: TmCaptures;
  beginCaptures?: TmCaptures;
  endCaptures?: TmCaptures;
  patterns?: TmRule[];
  include?: string;
}

export interface TmRepositoryEntry {
  comment?: string;
  begin?: string;
  end?: string;
  beginCaptures?: TmCaptures;
  endCaptures?: TmCaptures;
  patterns?: TmRule[];
}

export interface TmGrammar {
  $schema: string;
  name: string;
  scopeName: string;
  fileTypes: string[];
  patterns: TmRule[];
  repository: { [key: string]: TmRepositoryEntry };
}
