# v0.17.2 セルフホスティング補強: 文字列ハッシュ・基数フォーマッタ・ソート・Interner

標準ライブラリ充足度評価（native/jit）で見つかった不足の実装計画。
優先度はP0（正しさ）→P1（性能・欠落API）→P2（周辺整備）。

## 背景（評価で判明した問題）

- **P0**: `HashMap<string, V>` / `HashSet<string>` はハッシュが `key as int`（文字列では実質ポインタ値）のため、実行時生成キーのルックアップが外れる（実測2000件中ヒット1件）。リテラルキー同士はintern共有で偶然一致するため既存テストでは顕在化しない。シンボルテーブル用途が直撃する。
- **P1**: 数値→基数文字列（to_hex等）が無い（parseの逆方向）。Vector.sort/sortByが挿入ソート（O(n²)）で大規模入力に弱く、スライスにはソートAPI自体が無い。
- **P2**: 大出力向けの書き込み経路（コード生成の出力性能）が未検証。文字列interning（Symbol化）の部品が無い。

## 設計判断: 文字列ハッシュはジェネリック分岐でなく専用型で提供する

ジェネリックな `HashMap<K, V>` 内でKの型により内容ハッシュへ分岐する案は、現行言語では成立しないことを実験で確認した。

- フリー関数オーバーロードは非対応（専用診断で拒否）。
- `__typename__(T)` はジェネリック文脈ではcheck時に文字列 "T" へ解決され、mono後の実型名にならない（`__sizeof__(T)` のようなmonoマーカー機構が無い）。
- 仮に実型名で分岐できても、K=intの特殊化で文字列分岐側の `key as string` が型検査を通らない（`int as string` は不可）ため、デッドブランチがコンパイルできない。

よって専用型 `StringMap<V>` / `StringSet`（キー型を文字列に固定）で内容ハッシュ（FNV-1a）を提供する。
`HashMap<string, V>` の是正（`__typename__` のmono時解決＋デッドブランチ剪定、または組み込みハッシュintrinsic）は言語側の設計課題として記録する。

## P0: std::strings::hash と StringMap / StringSet

### std::strings::hash（新設）

- `export int hash_string(string s)` — FNV-1a 32bit。`byte_len()`/`byte_at(i)` で内容を走査し、long演算＋0xFFFFFFFFマスクで32bitラップを再現、最上位ビットを落として非負intを返す。

### std::collections::strmap（新設・StringMap<V>）

HashSetの二重解放（dtor持ち構造体の暗黙コピー）の教訓から、**スライスバックでdtor無し**の構造にする（TreeMapで実証済みのパターン）。

- 構造: 密配列 `string[] keys` / `V[] vals` / `bool[] alive` ＋ 探索表 `int[] slots`（密配列indexを保持、-1=空、-2=墓石）＋ `int[] free_idx`（削除済み密indexの再利用）。
- 探索表は線形探索のオープンアドレッシング、負荷率50%で2倍成長（成長時はaliveキーのみ再挿入、密配列は不変）。
- API: `insert(key, val)` / `get(key) -> Option<V>` / `get_or(key, default)` / `contains` / `remove` / `len` / `is_empty` / `clear` / `keys() -> string[]`（生存キーの挿入順）。
- 削除は墓石方式＋密スロット再利用（vals[idx]への上書き代入で確保済みスロットを再利用し、デフォルト値V()を要求しない）。

### std::collections::strset（新設・StringSet）

`StringMap<bool>` の薄ラッパー（insert/contains/remove/len/is_empty/clear/values）。
StringMapはdtorを持たないため、コンストラクタの `self.map = m;` はTreeSetと同じく安全。

### 既存HashMap/HashSetの扱い

実装は変更しない（intキャスト可能なキー専用として維持）。
モジュールヘッダとチュートリアルへ「文字列キーは StringMap / StringSet を使う」制約を明記する。

## P1: std::strings::format と ヒープソート

### std::strings::format（新設）

- `export string to_radix(long v, int base)` — 基数2〜36（小文字、負数は'-'前置）。ulong経由でlong最小値も正しく扱う。
- `export string to_hex(long v)` / `to_bin(long v)` / `to_oct(long v)` — to_radixの別名。
- `export string pad_left(string s, int width, char fill)` / `pad_right` — 幅揃え（コード生成の桁揃え用）。
- 実装は `utiny[]` へ逆順に桁を詰めて反転し `from_bytes` で文字列化（O(桁数)）。

### ソートのO(n log n)化

- `Vector<T>.sort()` / `sortBy(cmp)` をヒープソートへ変更（in-place・追加確保なし・非安定）。`sort()` は `<` 比較、`sortBy` は比較関数。private sift-downヘルパーを比較方式ごとに持つ。
- `std::slices`（新設）: `export <T> void sort(T[] xs)` / `sort_by(T[] xs, int*(T, T) cmp)` — スライス向けの同アルゴリズム。
- 非安定ソートであることと、所有権型（dtor持ち）要素は要素コピーが発生するため対象外であることをコメントで明記。

## P2: 書き込み性能の検証 と Interner

### 大出力の書き込み検証

- StringBuilder（償却O(1)追記）で数MB級のテキストを構築し `write_file` で書き出すパフォーマンステストを追加（コード生成の出力経路の回帰検知）。
- `BufWriter`（native::io::stream）の行追記経路も同量で検証する。

### std::strings::intern（新設・Interner）

- `export struct Interner { private StringMap<int> ids; private string[] names; }`
- `intern(s) -> int`（既存なら同じid、新規なら採番）/ `name_of(id) -> string` / `len()`。
- コンパイラのシンボルテーブル・識別子管理の基礎部品。

## テスト計画

- 機能: strmap（挿入・上書き・削除・墓石再利用・成長・keys順序）、strset、hash_string（既知値・空文字列）、format（基数・負数・パディング・long境界）、slices/Vectorソート（逆順・重複・既ソート・1要素・空）、intern（重複intern・name_of往復）。
- 正しさ回帰: **実行時生成キー**（補間で構築）でのStringMapルックアップ2000件全ヒット（P0バグの再現条件）。
- パフォーマンス: StringMap 10000件挿入・参照スモーク、ソート20000件（挿入ソートではタイムアウトする規模）、書き込み数MB。
- 全バックエンドスイート（interpreter/llvm/js/wasm/sv）通過。

## 言語側の設計課題（今回はスコープ外として記録）

- `__typename__(T)` のmono時解決（sizeof_for_Tマーカーと同型の機構）と、mono時定数条件によるデッドブランチ剪定。両方が揃うと `HashMap<K, V>` 内での型分岐ハッシュが書けるようになり、StringMapをHashMapへ統合できる。
- フリー関数オーバーロード（意図的に非対応としている現状仕様の再検討）。
