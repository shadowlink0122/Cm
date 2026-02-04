# テストカバレッジ分析と未解決問題

## 概要

- **テストカテゴリ数**: 50
- **テストファイル数**: 370
- **スキップ中テスト**: 24件

---

## 発見した問題（要修正）

### 高優先度

| 問題 | カテゴリ | 詳細 | 影響 |
|------|----------|------|------|
| typedef long + int 複合代入 | types | `long += int`でLLVM型不一致（i64とi32） | 型変換 |
| enum + match guard | enum | タグ付きenumでmatchがハング | パターンマッチ |
| char* → string 変換 | io | 4テストがスキップ中 | 文字列処理 |

### 中優先度

| 問題 | カテゴリ | 詳細 |
|------|----------|------|
| malloc cast | casting | 2テストがスキップ |
| address interpolation | memory | 1テストがスキップ |
| reimport | advanced_modules | モジュール再インポート |

---

## スキップ中テスト詳細一覧（24件）

### JS非互換（10件）

| ファイル | 理由 |
|---------|------|
| `memory/.skip` | JS非対応 |
| `array/array_pointer.skip` | JS非対応 |
| `target/native_only.skip` | JS非対応 |
| `basic/return.skip` | JS非対応 |
| `dynamic_array/.skip` | JS非対応 |
| `ffi/.skip` | JS非対応 |
| `arrays/multidim_array.skip` | JS非対応 |
| `allocator/.skip` | JS非対応 |
| `pointer/.skip` | JS非対応 |
| `modules/std_io_import.skip` | JS非対応 |
| `casting/malloc_cast.skip` | JS非対応 |
| `casting/allocator_cast.skip` | JS非対応 |

### 機能未実装（6件）

| ファイル | 理由 |
|---------|------|
| `io/line_read.skip` | char*→string変換問題 |
| `io/runtime_io.skip` | char*→string変換問題 |
| `io/buffered_io.skip` | char*→string変換問題 |
| `file_io/file_read_write.skip` | std::fs依存問題 |
| `fs/file_io_test.skip` | ファイルI/O未実装 |
| `enum/simple_compare.skip` | Tagged Union型とenum値の直接比較未サポート |

### 特殊条件（4件）

| ファイル | 理由 |
|---------|------|
| `memory/address_interpolation.skip` | アドレス値が実行毎に変化 |
| `io/input.skip` | 入力待ちが必要 |
| `target/js_only.skip` | JS専用テスト |
| `must/must_deadcode.skip` | 意図的（最適化確認用） |

### バックエンド非互換（3件）

| ファイル | 理由 |
|---------|------|
| `advanced_modules/test_reimport.skip` | llvm, llvm-wasm非対応 |
| `advanced_modules/test_math_simple.skip` | 全バックエンド非対応 |
| `advanced_modules/test_import_features.skip` | 空（要調査） |


---

## 未テスト領域（BNFカバレッジ）

BNF規則数: 101 / パーサ関数: 194

### 構文別テスト状況

| 構文 | テスト数 | 状態 |
|------|---------|------|
| typedef | 10 | ⚠️ 複合代入未テスト |
| macro | 3 | ⚠️ 関数マクロのみ |
| enum | 10 | ⚠️ match guardでハング |
| generics | 15 | ✅ 十分 |
| interface | 23 | ✅ 十分 |
| match | 7 | ✅ 基本動作OK |
| must | 2 | ⚠️ 1スキップ |

### 型カテゴリ別（スキル基準）

| 型 | テスト有無 | 備考 |
|----|----------|------|
| プリミティブ | ✅ | int, double等 |
| typedef | ⚠️ | 複合代入問題 |
| 構造体 | ✅ | 十分 |
| 列挙型 | ⚠️ | match guardでハング |
| 配列 | ✅ | 20テスト |
| ポインタ | ✅ | 15テスト |
| 参照 | ⚠️ | テスト少ない |
| ジェネリック | ✅ | 15テスト |

---

## 推奨アクション

### 即時対応

1. [ ] typedef long + int 型変換修正
2. [ ] enum + match guard ハング調査
3. [ ] char* → string 変換実装

### 短期対応

4. [ ] malloc_cast テスト修正
5. [ ] 参照型テスト追加
6. [ ] 関数マクロテスト拡充

### 長期対応

7. [ ] io カテゴリ全体の見直し
8. [ ] advanced_modules reimport修正
9. [ ] ファイルI/O実装完了
