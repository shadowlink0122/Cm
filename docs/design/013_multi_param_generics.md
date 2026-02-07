# Multi-Parameter Generics Enhancement for `impl` Blocks

## 1. 問題概要

`impl<K, V> HashMap<K, V>` のような複数型パラメータを持つimplブロックのメソッドが正しくmonomorphize（具象化）されない。

### 現在の状況

| 機能 | 状態 |
|------|------|
| `struct Pair<T, U>` (2パラメータ構造体) | ✅ 動作 |
| `<T, U> make_pair(T a, U b)` (2パラメータ関数) | ✅ 動作 |
| `impl<V> HashMap<V>` (1パラメータimpl) | ✅ 動作 |
| `impl<K, V> HashMap<K, V>` (2パラメータimpl) | ❌ ハング/セグフォルト |

### 原因分析

`scan_generic_calls` 関数（monomorphization_impl.cpp）で、マングリングされた関数名からの型引数抽出が1パラメータのみを想定：

```cpp
// 現在の実装（1パラメータのみ対応）
std::string type_arg = remaining.substr(0, last_sep);  // "int" のみ
std::vector<std::string> type_args = {type_arg};       // 単一要素
```

`HashMap__int__int__put` のような名前から `[int, int]` を正しく抽出する必要がある。

---

## 2. 提案する変更

### Component: `src/mir/lowering/monomorphization_impl.cpp`

#### [MODIFY] scan_generic_calls (lines 167-216)

**変更内容**: マングリング名（`Base__Type1__Type2__method`）から複数型引数を抽出するロジック

```cpp
// 現在: remaining.rfind("__") で最後の__のみを使用
// 提案: generic_paramsの数に基づいて適切な数の型引数を抽出
```

---

#### [MODIFY] monomorphization_utils.cpp

**変更内容**: 型引数分割ユーティリティ関数の追加

```cpp
// 新しい関数: split_type_args(str) -> vector<string>
// "T, U" や "int, string" を ["T", "U"] や ["int", "string"] に分割
std::vector<std::string> split_type_args(const std::string& type_arg_str);
```

---

### Component: `std/collections/hashmap.cm`

#### [MODIFY] hashmap.cm

**変更内容**: `HashMap<V>` から `HashMap<K, V>` へ拡張

```cm
// 現在: struct Entry<V>, struct HashMap<V>
// 提案: struct Entry<K, V>, struct HashMap<K, V>
```

> **WARNING**: この変更は破壊的変更です。既存の `HashMap<V>` を使用するコードに影響します。

---

## 3. 検証計画

### 自動テスト

```bash
# 既存テストの確認（ベースライン）
make tip  # JITテスト全体

# 新規テストファイル用意済み
./cm run tests/test_programs/generics/multiple_type_params.cm

# HashMap<K, V> テスト（実装後）
./cm run tests/test_programs/collections/hashmap_test.cm
```

### 手動検証

1. シンプルな2パラメータimplテスト作成
2. HashMap<int, int> の基本操作（insert, get）確認
3. 異なる型の組み合わせ（HashMap<int, string>）確認

---

## 4. 実装フェーズ

1. **Phase 1**: `split_type_args` ユーティリティ関数追加
2. **Phase 2**: `scan_generic_calls` の複数型引数対応
3. **Phase 3**: HashMap<K, V> への拡張
4. **Phase 4**: テスト追加と検証
