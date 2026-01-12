# Cm Examples

Cmの機能を示す公式サンプル集です。

## カテゴリ

### 01_basics - 基本機能
- `hello_world.cm` - 出力と文字列補間
- `variables.cm` - 変数、型、構造体
- `control_flow.cm` - if/for/while/switch/match

### 02_functions - 関数
- `callbacks.cm` - コールバック（関数ポインタ/interface/ラムダ）
- `generics.cm` - ジェネリクスと制約

### 03_ownership - 所有権
- `move_semantics.cm` - ムーブセマンティクス
- `borrowing.cm` - 借用と参照

### 04_memory - メモリ管理
- `raii_pattern.cm` - RAIIパターン（デストラクタ）
- `heap_allocation.cm` - ヒープ確保（FFI + RAII）

### 05_data_structures - データ構造
- `linked_list.cm` - 単方向リンクリスト
- `priority_queue.cm` - 優先度付きキュー（最小ヒープ）

### 06_algorithms - アルゴリズム
- `memoization_dp.cm` - メモ化DP（フィボナッチ、最小コイン問題）
- `dijkstra.cm` - ダイクストラ法（最短経路）

## 実行方法

```bash
# インタプリタで実行
cm run examples/01_basics/hello_world.cm

# LLVMコンパイル & 実行
cm run -t llvm examples/06_algorithms/dijkstra.cm
```

## 機能カバレッジ

| 機能 | サンプル |
|------|----------|
| 関数ポインタ | callbacks.cm |
| interface | callbacks.cm, generics.cm |
| ジェネリクス | generics.cm |
| move | move_semantics.cm |
| 借用 | borrowing.cm |
| RAII | raii_pattern.cm, heap_allocation.cm |
| FFI | heap_allocation.cm, linked_list.cm, priority_queue.cm |
| メモ化DP | memoization_dp.cm |
| グラフ探索 | dijkstra.cm |
