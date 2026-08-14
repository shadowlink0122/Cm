# v0.17.2 セルフホスティング向け標準ライブラリ整備計画

## 目的

Cmコンパイラ自身をCmで記述する（セルフホスティング）ための土台として、コンパイラ実装に必要な標準ライブラリを棚卸しし、v0.17.2で整備する範囲を定める。
コンパイラの各フェーズ（字句解析→構文解析→AST→型検査→コード生成）が要求するデータ構造・ユーティリティを基準に不足を洗い出す。

## 現状の棚卸し（v0.17.1時点）

| 分類 | 提供済み | 備考 |
|---|---|---|
| コレクション | `Vector<T>`・`HashMap<K,V>`・`TreeMap<K,V>`・`Queue<T>` | Set系・Stack/Dequeなし。Vector.sortはバブル・プリミティブ限定 |
| 文字列 | `StringBuilder`・`split/lines`・`from_bytes`・組み込みメソッド（startsWith/endsWith/includes/indexOf/substring/replace/trim/repeat/toLowerCase/toUpperCase/charAt/len/byte_at/codepoint_at） | 文字単位の分類（is_digit等）なし |
| 数値変換 | `parse_int`（`std::io`内・`Option<int>`返し） | 配置が不自然（io内）。parse_float/基数指定なし |
| メモリ | `alloc/dealloc/size_of`・`Allocator`インターフェース・グローバルアロケータ差し替え・`UniquePtr/SharedPtr`（v0.17.1） | Arena/Poolなし・WeakPtrなし |
| イテレータ | `std::iter`（traits/adapters/range/array） | |
| OS連携 | `fs`・`path`・`env`・`process`・`bytes`・args | 行単位の逐次読みなし（全読みのみ） |
| その他 | `json`・`core`（min/max/clamp/abs/panic）・`core::time`（now_ms/sleep）・native系（http/net/sync/gpu/math） | |
| エラー処理 | 組み込み`Result/Option`・`?`演算子・panic | 十分 |

## ギャップ分析（コンパイラのフェーズ別）

- **字句解析**: 文字分類（is_digit/is_alpha等）が無く、比較演算の手書きが必要になる。数値リテラル解析にparse_int/parse_floatの基数対応が要る。
- **構文解析・AST**: ノードの所有管理はUniquePtr/SharedPtrで可能になったが、コンパイラ用途では**Arenaアロケータ**（フェーズ一括解放・個別free不要）が定石で、move規律の負担も減る。
- **型検査**: 集合（訪問済み・シンボル集合）にSetが無くHashMap<K,bool>で代用が必要。スコープスタックはVectorで代用可能（push/pop既存）。
- **コード生成**: StringBuilder・fsは既存で足りる。
- **横断**: ソートの計算量（バブル）と比較関数対応、計測用Stopwatch。

## v0.17.2 実装提案（優先度順）

### P0: セルフホスティングの直接前提

1. **`std::mem::arena` — Arenaアロケータ**
   - `Arena`（可変長チャンクの単方向リスト）: `alloc(size)`・`reset()`・dtorで一括解放。`Allocator`インターフェース実装で`Vector`等にも注入可能。
   - AST構築の定石（個別freeなし・フェーズ終了で一括破棄）。UniquePtr/SharedPtrのmove規律が不要になる領域を広げる。
2. **`std::strings::chars` — 文字分類・変換**
   - `is_digit/is_alpha/is_alnum/is_space/is_upper/is_lower/is_hex_digit(char)`・`to_upper/to_lower(char)`・`digit_value(char, int base) -> Option<int>`。
   - ASCII範囲を対象（識別子・リテラルはASCIIで十分。非ASCIIはcodepoint_atで別途扱う）。
3. **`std::strings::parse` — 数値解析の集約**
   - `parse_int(string) -> Option<int>` を`std::io`から移設（io側は再エクスポートで互換維持）。
   - `parse_int_radix(string, int base)`・`parse_long`・`parse_float` を追加（リテラル解析の基盤）。
4. **`std::collections::HashSet<T> / TreeSet<T>`**
   - 既存HashMap/TreeMapのキーのみ薄ラッパー（insert/contains/remove/len/clear）。

### P1: 品質・利便性

5. **`Vector<T>`の強化**: `sortBy(比較ラムダ)`対応と挿入ソート化（小規模前提の改善）・`last()/top()`（スタック用途の明確化）・`reserve(n)`。
6. **`std::fs`の行読み**: `read_lines(path) -> Vector<string>`（全読み+split流用）と、大ファイル向け`FileReader`（チャンク読み+行イテレータ）。
7. **`std::core::time::Stopwatch`**: `start()/elapsed_ms()`（now_msの薄ラッパー。コンパイラのフェーズ計測用）。

### P2: スマートポインタ第2弾（言語ガードと連動）

8. **`WeakPtr<T>`**: 制御ブロックをstrong/weak二重カウント構造体へ拡張（循環参照対策）。
9. **Atomic参照カウント版SharedPtr**: `std::sync`（native）のAtomicで`AtomicSharedPtr<T>`。単一スレッド版と別型として提供。

## 言語側の前提課題（stdlib外・別設計で扱う）

- **dtorを持つstructの暗黙コピーを警告/エラー化**: `b = a;` の浅いコピーで両dtorが走る二重解放をコンパイラが検出する（UniquePtr/SharedPtrのmove/clone規律の強制）。
- **所有型の`return local;`検出**: dtor持ちローカルの非move返却（dangling）を警告する。
- 上記2つはスマートポインタ設計（archive/v0.17.1/smart-pointers.md）の既知制約であり、v0.17.2の言語側設計として別文書化する。

## テスト計画

- 各モジュールに `tests/common/stdlib/` 配下のintegrationテストを追加（native系。ポインタを使うもの＝arena/smartは `//! platform: !js|sv`、文字分類・parse・Set等の純粋ロジックは全バックエンド）。
- Arenaは確保・reset・一括解放・Allocator注入（Vector連携）を検証。
- parse系は境界（空文字・符号・オーバーフロー・基数16）をResult/Optionで検証。

## 非対象（明示）

- 正規表現・Unicode正規化: コンパイラ実装に不要。
- `std::fmt`: 文字列補間で充足。
- 診断・ソース位置管理: コンパイラ本体のドメインでありstdへは置かない。
