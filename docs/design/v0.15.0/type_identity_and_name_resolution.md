# v0.15.0 設計課題: 型同一性と名前解決の根本改善

> ステータス: **未着手** — v0.14.2の緩和策のみ適用済み、根本修正はv0.16.0で検討
> リスク: ❗ コンパイラ根幹の大規模リファクタ
> 2026-03-09更新

## 1. 型同一性による比較の導入

### 現状の問題

`stmt.cpp` の enum coercion 判定で `resolved_type->name == enum_name` という**文字列名比較**を行っている。
モノモーフ化エンジンが `Option<ulong>` → `Option__ulong` と名前を変換するため、
パターンのソースレベル名 `Option` と不一致が生じる。

v0.14.2では `get_enum_base_name()` で `__` 以前を抽出するベース名比較を導入したが、
これは命名規約に依存した緩和策であり、ユーザーが `__` を含む型名を使用した場合に誤動作のリスクがある。

### 目標設計

- 型を**文字列名ではなく型定義オブジェクトへの参照（ポインタ/ID）**で比較する
- `TypeChecker` に `enum_defs_` から `TypePtr` → `EnumDef*` への逆引きマップを持たせる
- coercion判定を `resolved_type` の enum定義と、パターンから解決した enum定義の**同一性**で行う

### 影響範囲

- `src/frontend/types/checking/stmt.cpp` — let文・return文のcoercion判定
- `src/frontend/types/checking/expr.cpp` — match式のexhaustiveness検査
- `src/frontend/types/type_checker.hpp` — 型同一性ヘルパーの追加

---

## 2. 名前解決プロトコルの統一

### 現状の問題

MIR lowering (`impl.cpp`) の関数定義側と、LLVM codegen (`utils.cpp`) の関数呼び出し側で
**異なる命名規約**を使用している。

- 定義側: `current_module_path + "::"` プレフィックスのみ除去（v0.14.2修正）
- 呼び出側: `declareExternalFunction` が単純名で `functions` テーブルを検索、
  見つからなければ `name + "_"` 前方一致フォールバック

v0.14.2では前方一致を一意候補限定にし、namespace剥がしをモジュール限定にしたが、
呼び出し側と定義側の命名ルールが統一されていない根本問題は残っている。

### 目標設計

- **完全修飾名の正規化ルール**を `common/` レベルでヘルパーとして定義
  - `normalize_symbol_name(module_path, raw_name)` → 正規化シンボル名
- MIR lowering と LLVM codegen の両方が同一のヘルパーを使用
- `declareExternalFunction` の前方一致フォールバックを段階的に廃止

### 影響範囲

- `src/mir/lowering/impl.cpp` — 関数定義名の正規化
- `src/codegen/llvm/core/utils.cpp` — `declareExternalFunction` の検索ロジック
- `src/codegen/llvm/core/mir_to_llvm.cpp` — 関数呼び出し時の名前解決
- `src/common/` — 共通ヘルパー（新規）
