// ============================================================
// コンパイラ組み込みメソッド・関数のレジストリ（VSCode API非依存）
// ============================================================
// 配列/スライスのmap等、コンパイラが提供する「デフォルトメソッド」はソース定義を持たない（コードジャンプ不可）。
// あらかじめ共有されている定義としてホバー表示するため、シグネチャと説明をこの表で管理する。
// 出典: src/internal/types/checking/call/method.cpp（配列/スライス/文字列メソッド）・auto_impl.cpp（Option/Result）。
// 追加・変更時はコンパイラ実装（method.cpp）と一致させること。

export interface BuiltinEntry {
  /// レシーバの種別（配列/スライス・文字列・Option<T>・Result<T, E> など）。関数は 'なし'
  receiver: string;
  /// 表示用シグネチャ（`レシーバ.メソッド(引数) -> 戻り値` 形式）
  signature: string;
  /// 一行の説明（挙動の注意点を含む）
  doc: string;
}

// ---- 組み込みメソッド（メンバアクセス `.method` で呼ぶもの） ----
// 同名メソッドが複数のレシーバ型に存在する場合は配列で全て列挙する（ホバーは全オーバーロードを表示する）。

export const BUILTIN_METHODS: Record<string, BuiltinEntry[]> = {
  // ---- 配列 / スライス共通 ----
  len: [
    {
      receiver: '配列 / スライス',
      signature: 'T[].len() -> int',
      doc: '要素数を返す。`size()` / `length()` も同義。',
    },
    {
      receiver: '文字列',
      signature: 'string.len() -> uint',
      doc: 'UTF-8コードポイント数を返す（H9でバイト数から変更）。バイト長は `byte_len()`。`size()` / `length()` も同義。',
    },
  ],
  size: [
    {
      receiver: '配列 / スライス',
      signature: 'T[].size() -> int',
      doc: '要素数を返す（`len()` の別名）。',
    },
    {
      receiver: '文字列',
      signature: 'string.size() -> uint',
      doc: 'UTF-8コードポイント数を返す（`len()` の別名）。',
    },
  ],
  length: [
    {
      receiver: '配列 / スライス',
      signature: 'T[].length() -> int',
      doc: '要素数を返す（`len()` の別名）。',
    },
    {
      receiver: '文字列',
      signature: 'string.length() -> uint',
      doc: 'UTF-8コードポイント数を返す（`len()` の別名）。',
    },
  ],
  dim: [
    {
      receiver: '配列 / スライス',
      signature: 'T[].dim() -> int',
      doc: '多次元配列の次元数を返す。',
    },
  ],

  // ---- スライス（動的配列）専用 ----
  cap: [
    {
      receiver: 'スライス（動的配列）',
      signature: 'T[].cap() -> int',
      doc: '確保済み容量を返す。`capacity()` も同義。',
    },
  ],
  capacity: [
    {
      receiver: 'スライス（動的配列）',
      signature: 'T[].capacity() -> int',
      doc: '確保済み容量を返す（`cap()` の別名）。',
    },
  ],
  push: [
    {
      receiver: 'スライス（動的配列）',
      signature: 'T[].push(elem: T) -> void',
      doc: '末尾に要素を追加する。容量超過時は自動拡張する。キャプチャ付きクロージャは格納できない（環境喪失のため診断エラー）。',
    },
  ],
  pop: [
    {
      receiver: 'スライス（動的配列）',
      signature: 'T[].pop() -> T',
      doc: '末尾の要素を取り除いて返す。',
    },
  ],
  remove: [
    {
      receiver: 'スライス（動的配列）',
      signature: 'T[].remove(index: int) -> void',
      doc: '指定インデックスの要素を削除する。`delete()` も同義。',
    },
  ],
  delete: [
    {
      receiver: 'スライス（動的配列）',
      signature: 'T[].delete(index: int) -> void',
      doc: '指定インデックスの要素を削除する（`remove()` の別名）。',
    },
  ],
  clear: [
    {
      receiver: 'スライス（動的配列）',
      signature: 'T[].clear() -> void',
      doc: '全要素を削除して空にする。',
    },
  ],

  // ---- 配列 / スライス 高階・検索 ----
  map: [
    {
      receiver: '配列 / スライス',
      signature: 'T[].map(fn: (T) -> U) -> U[]',
      doc: '各要素へ `fn` を適用した新しい配列を返す（要素型はコールバックの戻り値型で決まる）。',
    },
  ],
  filter: [
    {
      receiver: '配列 / スライス',
      signature: 'T[].filter(fn: (T) -> bool) -> T[]',
      doc: '`fn` が真を返す要素だけを集めた動的配列を返す。',
    },
  ],
  reduce: [
    {
      receiver: '配列 / スライス',
      signature: 'T[].reduce(fn: (int, T) -> int, init: int) -> int',
      doc: '`init` を初期値に各要素を畳み込んで単一の整数を返す。',
    },
  ],
  forEach: [
    {
      receiver: '配列 / スライス',
      signature: 'T[].forEach(fn: (T) -> void) -> void',
      doc: '各要素へ `fn` を適用する（戻り値なし）。',
    },
  ],
  find: [
    {
      receiver: '配列 / スライス',
      signature: 'T[].find(fn: (T) -> bool) -> T',
      doc: '`fn` が真を返す最初の要素を返す。',
    },
  ],
  findIndex: [
    {
      receiver: '配列 / スライス',
      signature: 'T[].findIndex(fn: (T) -> bool) -> int',
      doc: '`fn` が真を返す最初の要素のインデックスを返す（無ければ-1）。',
    },
  ],
  some: [
    {
      receiver: '配列 / スライス',
      signature: 'T[].some(fn: (T) -> bool) -> bool',
      doc: '`fn` が真を返す要素が1つでもあれば `true`。',
    },
  ],
  every: [
    {
      receiver: '配列 / スライス',
      signature: 'T[].every(fn: (T) -> bool) -> bool',
      doc: '全要素で `fn` が真なら `true`。',
    },
  ],
  indexOf: [
    {
      receiver: '配列 / スライス',
      signature: 'T[].indexOf(elem: T) -> int',
      doc: '`elem` と等しい最初の要素のインデックスを返す（無ければ-1）。',
    },
    {
      receiver: '文字列',
      signature: 'string.indexOf(sub: string) -> int',
      doc: '部分文字列 `sub` が最初に現れる位置を返す（無ければ-1）。',
    },
  ],
  includes: [
    {
      receiver: '配列 / スライス',
      signature: 'T[].includes(elem: T) -> bool',
      doc: '`elem` を含むなら `true`。`contains()` も同義。',
    },
    {
      receiver: '文字列',
      signature: 'string.includes(sub: string) -> bool',
      doc: '部分文字列 `sub` を含むなら `true`。',
    },
  ],
  contains: [
    {
      receiver: '配列 / スライス',
      signature: 'T[].contains(elem: T) -> bool',
      doc: '`elem` を含むなら `true`（`includes()` の別名）。',
    },
    {
      receiver: '文字列',
      signature: 'string.contains(sub: string) -> bool',
      doc: '部分文字列を含むなら `true`（`includes()` の別名）。',
    },
  ],
  sort: [
    {
      receiver: '配列 / スライス',
      signature: 'T[].sort() -> T[]',
      doc: '昇順にソートした動的配列を返す。',
    },
  ],
  sortBy: [
    {
      receiver: '配列 / スライス',
      signature: 'T[].sortBy(fn: (T) -> int) -> T[]',
      doc: '`fn` が返すキーでソートした配列を返す。',
    },
  ],
  reverse: [
    {
      receiver: '配列 / スライス',
      signature: 'T[].reverse() -> T[]',
      doc: '逆順にした動的配列を返す。',
    },
  ],
  get: [
    {
      receiver: '配列 / スライス',
      signature: 'T[].get(index: int) -> Option<T>',
      doc: '範囲内なら `Some(要素)`、範囲外なら `None` を返す安全アクセス（`arr[i]` の範囲外を避ける）。',
    },
  ],
  first: [
    { receiver: '配列 / スライス', signature: 'T[].first() -> T', doc: '最初の要素を返す。' },
    { receiver: '文字列', signature: 'string.first() -> char', doc: '最初の文字を返す。' },
  ],
  last: [
    { receiver: '配列 / スライス', signature: 'T[].last() -> T', doc: '最後の要素を返す。' },
    { receiver: '文字列', signature: 'string.last() -> char', doc: '最後の文字を返す。' },
  ],
  at: [
    {
      receiver: '文字列',
      signature: 'string.at(index: int) -> char',
      doc: '指定位置の文字を返す（`charAt()` の別名）。',
    },
  ],

  // ---- 文字列 ----
  byte_len: [
    {
      receiver: '文字列',
      signature: 'string.byte_len() -> uint',
      doc: 'バイト長をO(1)で返す（SDS方式の長さヘッダ、H9）。',
    },
  ],
  chars: [
    {
      receiver: '文字列',
      signature: 'string.chars() -> uint[]',
      doc: 'UTF-8コードポイントの配列を返す。',
    },
  ],
  codepoint_at: [
    {
      receiver: '文字列',
      signature: 'string.codepoint_at(index: int) -> uint',
      doc: '指定位置のコードポイント値を返す。',
    },
  ],
  charAt: [
    {
      receiver: '文字列',
      signature: 'string.charAt(index: int) -> char',
      doc: '指定位置の文字を返す。`at()` も同義。',
    },
  ],
  substring: [
    {
      receiver: '文字列',
      signature: 'string.substring(start: int, end: int) -> string',
      doc: '`[start, end)` の部分文字列を返す（添字はコードポイント単位）。`slice()` も同義。',
    },
  ],
  slice: [
    {
      receiver: '文字列',
      signature: 'string.slice(start: int, end: int) -> string',
      doc: '`[start, end)` の部分文字列を返す（`substring()` の別名）。',
    },
  ],
  toUpperCase: [
    {
      receiver: '文字列',
      signature: 'string.toUpperCase() -> string',
      doc: 'ASCII範囲を大文字化した文字列を返す。',
    },
  ],
  toLowerCase: [
    {
      receiver: '文字列',
      signature: 'string.toLowerCase() -> string',
      doc: 'ASCII範囲を小文字化した文字列を返す。',
    },
  ],
  trim: [
    {
      receiver: '文字列',
      signature: 'string.trim() -> string',
      doc: '前後の空白を除去した文字列を返す。',
    },
  ],
  startsWith: [
    {
      receiver: '文字列',
      signature: 'string.startsWith(prefix: string) -> bool',
      doc: '`prefix` で始まるなら `true`。',
    },
  ],
  endsWith: [
    {
      receiver: '文字列',
      signature: 'string.endsWith(suffix: string) -> bool',
      doc: '`suffix` で終わるなら `true`。',
    },
  ],
  repeat: [
    {
      receiver: '文字列',
      signature: 'string.repeat(n: int) -> string',
      doc: '文字列を `n` 回繰り返した文字列を返す。',
    },
  ],
  replace: [
    {
      receiver: '文字列',
      signature: 'string.replace(from: string, to: string) -> string',
      doc: '最初に一致した `from` だけを `to` へ置換した文字列を返す（全置換ではない）。',
    },
  ],

  // ---- Option<T> ----
  is_some: [
    {
      receiver: 'Option<T>',
      signature: 'Option<T>.is_some() -> bool',
      doc: '値を持つ（`Some`）なら `true`。',
    },
  ],
  is_none: [
    {
      receiver: 'Option<T>',
      signature: 'Option<T>.is_none() -> bool',
      doc: '値を持たない（`None`）なら `true`。',
    },
  ],

  // ---- Result<T, E> ----
  is_ok: [
    {
      receiver: 'Result<T, E>',
      signature: 'Result<T, E>.is_ok() -> bool',
      doc: '成功（`Ok`）なら `true`。',
    },
  ],
  is_err: [
    {
      receiver: 'Result<T, E>',
      signature: 'Result<T, E>.is_err() -> bool',
      doc: '失敗（`Err`）なら `true`。',
    },
  ],
  unwrap_err: [
    {
      receiver: 'Result<T, E>',
      signature: 'Result<T, E>.unwrap_err() -> E',
      doc: 'エラー値 `E` を取り出す（`Ok` のとき異常終了）。',
    },
  ],

  // ---- Option / Result 共通 ----
  unwrap: [
    {
      receiver: 'Option<T>',
      signature: 'Option<T>.unwrap() -> T',
      doc: '内部の値 `T` を取り出す（`None` のとき異常終了）。',
    },
    {
      receiver: 'Result<T, E>',
      signature: 'Result<T, E>.unwrap() -> T',
      doc: '成功値 `T` を取り出す（`Err` のとき異常終了）。',
    },
  ],
  unwrap_or: [
    {
      receiver: 'Option<T>',
      signature: 'Option<T>.unwrap_or(default: T) -> T',
      doc: '値があればそれを、無ければ `default` を返す。',
    },
    {
      receiver: 'Result<T, E>',
      signature: 'Result<T, E>.unwrap_or(default: T) -> T',
      doc: '成功値があればそれを、失敗なら `default` を返す。',
    },
  ],
  expect: [
    {
      receiver: 'Option<T>',
      signature: 'Option<T>.expect(msg: string) -> T',
      doc: '値を取り出す。`None` のとき `msg` を表示して異常終了する。',
    },
    {
      receiver: 'Result<T, E>',
      signature: 'Result<T, E>.expect(msg: string) -> T',
      doc: '成功値を取り出す。`Err` のとき `msg` を表示して異常終了する。',
    },
  ],
};

// ---- 組み込み関数（`.` なしで直接呼ぶもの） ----
export const BUILTIN_FUNCTIONS: Record<string, BuiltinEntry> = {
  print: {
    receiver: 'なし',
    signature: 'print(args...) -> void',
    doc: '改行なしで標準出力へ出力する。',
  },
  println: {
    receiver: 'なし',
    signature: 'println(args...) -> void',
    doc: '改行付きで標準出力へ出力する。文字列補間 `"{x}"` に対応。',
  },
  printf: {
    receiver: 'なし',
    signature: 'printf(format: string, args...) -> void',
    doc: '書式指定で標準出力へ出力する。',
  },
  eprint: {
    receiver: 'なし',
    signature: 'eprint(args...) -> void',
    doc: '改行なしで標準エラー出力へ出力する。',
  },
  eprintln: {
    receiver: 'なし',
    signature: 'eprintln(args...) -> void',
    doc: '改行付きで標準エラー出力へ出力する。',
  },
  panic: {
    receiver: 'なし',
    signature: 'panic(msg: string) -> void',
    doc: '`panic: <msg>` を表示して即座に異常終了する（exit(1)）。',
  },
  assert: {
    receiver: 'なし',
    signature: 'assert(cond: bool, msg: string) -> void',
    doc: '`cond` が偽なら `msg` を表示して異常終了する。',
  },
  exit: {
    receiver: 'なし',
    signature: 'exit(code: int) -> void',
    doc: '指定した終了コードでプログラムを終了する。',
  },
  step: {
    receiver: 'なし',
    signature: 'step(n: int) -> void',
    doc: 'SVバックエンドでクロックを `n` サイクル進める（シミュレーション用）。',
  },
};

// ---- 検索補助 ----

/// メソッドアクセス（`.method`）名から組み込みメソッドの全オーバーロードを返す
export function lookupBuiltinMethod(name: string): BuiltinEntry[] {
  return BUILTIN_METHODS[name] ?? [];
}

/// 自由識別子名から組み込み関数を返す
export function lookupBuiltinFunction(name: string): BuiltinEntry | undefined {
  return BUILTIN_FUNCTIONS[name];
}
