# 000. 配列フラット化 実装状況

## 概要

N次元配列を1次元配列として内部表現し、スタック上で効率的に管理する最適化。
ベアメタル環境対応のため、malloc/freeを使用しない設計。

## 実装状況

| Phase | 内容 | 状態 |
|-------|------|------|
| 1 | ドキュメント整備 | ✅ 完了 |
| 2 | 型システム拡張 | ✅ 完了 |
| 3 | HIR/MIR Lowering | ✅ 調査完了・方針確定 |
| 4 | LLVM コード生成 | ✅ 調査完了 |
| 5 | テスト・最適化 | ⏳ 次フェーズ |

---

## Phase 2: 型システム拡張（完了）

### 変更ファイル: `src/frontend/ast/types.hpp`

```cpp
// 追加フィールド
std::vector<uint32_t> dimensions;  // 各次元のサイズ

// 追加メソッド
bool is_multidim_array() const;       // 多次元配列判定
uint32_t get_flattened_size() const;  // フラットサイズ取得
TypePtr get_base_element_type() const; // 基底要素型取得
```

### ヒープ削除（ベアメタル対応）

| ファイル | 削除内容 |
|---------|---------|
| `mir_to_llvm.cpp` | `heapAllocatedLocals`、ヒープ割り当てブロック |
| `utils.cpp` | `freeHeapAllocatedLocals()` 関数 |
| `terminator.cpp` | ヒープ解放呼び出し (4箇所) |

---

## Phase 3: HIR/MIR Lowering（調査完了・方針確定）

### 調査結果
- MIR lowering: `ExprLowering::lower_index()` (expr_basic.cpp:651)
- 配列インデックスは `PlaceProjection::index(index)` として処理
- 多次元アクセス `arr[i][j]` はネストした `HirIndex` で表現

### パーサー改修試行→リバート
パーサーで `int[3][3]` を `int[9]` にフラット化する実装を試行:
- 問題: `matrix[i]` が `int` を返すので `[j]` で型エラー
- 結論: **パーサー段階でのフラット化は不可**

### 確定方針
1. 型はネスト配列として保持（後方互換性）
2. HIR lowering の `lower_index()` で多次元インデックス検出
3. `matrix[i][j]` → `matrix_flat[i*dims[1] + j]` に変換
4. LLVM生成時に単一GEPで最適化

---

## Phase 4: LLVM コード生成（調査完了）

### 現状分析

`convertPlaceToAddress()` (mir_to_llvm.cpp:2238) でインデックスプロジェクションを処理:

```cpp
// 現在の実装（ネストGEP）
addr = builder->CreateGEP(arrayType, addr, {zero, indexVal});
```

### 課題
- 多次元配列 `int[M][N]` は現在 `[M x [N x i32]]` としてネスト表現
- アクセス `arr[i][j]` は2段階のGEP命令で処理される
- フラット化には型情報の伝播が必要

### 今後の実装方針

1. **パーサー拡張**: 多次元宣言から `dimensions` ベクトル生成
2. **型チェッカー**: `dimensions` 情報の伝播
3. **LLVM生成**: フラットインデックス計算 `i*N + j`

---

## 関連ドキュメント

- [006_stack_array_flattening_architecture.md](./006_stack_array_flattening_architecture.md)
- [007_array_flattening_optimization.md](./007_array_flattening_optimization.md)
- [008_array_flattening_implementation_plan.md](./008_array_flattening_implementation_plan.md)
- [009_slice_optimization_design.md](./009_slice_optimization_design.md)
- [010_stack_array_summary.md](./010_stack_array_summary.md)
