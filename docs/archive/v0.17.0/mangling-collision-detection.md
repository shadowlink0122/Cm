# マングリング名の衝突検出（実装済み）

## 対象所見

| # | 領域 | 所見 | 状態 |
|---|------|------|------|
| C16 | 識別子 | `Struct__method`マングリングが自由関数・モジュール修飾名（`A::b`→`A__b`）と同一名前空間で、同名の自由関数があるとメソッド本体が黙って消える（全バックエンド共通誤コンパイル） | 実装済み |

## 背景と根本原因

Cmはメソッド・自由関数・モジュール修飾名・特殊化を、いずれも `__` を含むフラットなシンボル名へマングルし、それらを単一のシンボル空間へ登録する。
衝突ガードが無いため、同名が生じると片方が黙って上書き・消滅していた。

`lower_impl`（`src/internal/mir/lowering/impl.cpp`）はメソッドを `hir_functions[mir_func->name] = method.get();` へ登録し、`std::unordered_map`の同名キーへの再代入は無警告で上書きする。
ユーザーが構造体 `Struct` にメソッド `method` を持たせつつ、自由関数 `Struct__method`（あるいはモジュール `Struct` の関数 `method`＝`Struct::method`→`Struct__method`）を定義すると、両者は同一シンボルへ縮退し、後勝ちで一方の本体が消えていた。

`src/internal/types/checking/decl.cpp` の自由関数重複検出（`defined_function_sigs_`）は「自由関数 対 自由関数」に限定され、メソッド・モジュール修飾・特殊化のマングル名は登録対象外だった。

## 実装した設計

### マングル規則の共有ヘルパ（Phase 1）

`src/internal/base/mangle.hpp` に `mangle::method_name` / `ctor_name` / `dtor_name` / `flatten_qualified`（`::`→`__`）を集約し、以下の生成箇所を同一規則へ寄せた（挙動不変）。
- メソッド定義: `src/internal/mir/lowering/impl.cpp`（メソッド・演算子）
- メソッド呼び出し: `src/internal/hir/lowering/expr_member.cpp`
- コンストラクタ/デストラクタ: `src/internal/hir/lowering/decl.cpp`
- 型検査の登録側: `src/internal/types/checking/decl.cpp`（`register_impl`）

### 単一シンボルテーブルと衝突のハードエラー化（Phase 2）

型検査に `mangled_symbols_`（最終マングル名 → 由来・シグネチャ・宣言位置）を追加し、`register_mangled_symbol` で登録時に衝突を検査する。
- 登録対象: 本体を持つ非ジェネリック自由関数（モジュール修飾名は`::`→`__`へフラット化して登録）・implメソッド・コンストラクタ/デストラクタ。
- 由来と シグネチャが完全一致する再登録（モジュールflattenによる同一定義の複数出現）は許容する。
- 別由来・別シグネチャの同名は `MsgId::TypeMangledSymbolCollision` の診断付きハードエラー（両者の由来を提示、非ゼロexit）。

### ジェネリック特殊化名（Phase 3）

特殊化名とユーザー識別子の衝突は、[[type-identity-recursive-keys]] の実装で「フラット名がユーザー定義構造体と同名になる場合は`$`区切りエンコード名へ退避」する方式により原理的に回避されるため、シンボルテーブルへの特殊化名の登録は不要になった（設計時に想定した `mangle_specialization_user_type` のエラー化は行わず、共存が正当に動作する）。

## 診断出力の例

```
error: symbol 'Holder__method' is defined more than once: method 'Holder.method' conflicts with function 'Holder__method' (both lower to the same linkage name)
  int Holder__method() {
      ^
```

## テスト

- `tests/common/errors/mangle_method_free_fn_collision.cm` + 空 `.error`: メソッド `Holder.method` と自由関数 `Holder__method()` の衝突がコンパイルエラーになることを検証。
- `tests/common/errors/mangle_module_method_collision.cm` + 空 `.error`: モジュール `holder2` の関数 `method`（`holder2::method`）と自由関数 `holder2__method` の衝突を検証（補助モジュールは `mangle_mod/holder2.cm`）。
- 回帰: 既存の正当なプログラム（`tests/common/impl/`・`advanced_modules/`・libs）が誤検出でエラー化しないことを全バックエンドスイートで確認。

## 残課題（後続へ引き継ぎ）

- 演算子実装（`Type__op_eq` 等）のシンボルテーブル登録は、OperatorKind→op名テーブルの共有化と併せて後続対応とする（衝突の発生確率が低く、誤検出リスクの方が高いため）。
- `sanitizeIdentifier` 縮退後キーの二次登録（L1・M2、設計時Phase 4）は未実装。まず警告として導入し誤検出が無いことを確認してから昇格する方針は維持する。

## 関連

- [[type-identity-recursive-keys]]（C7/C8/C9）: 特殊化名の名前空間分離。C8の防波堤を共有する。
- 監査レポート `large-scale-bottleneck-audit.md` の「識別子/名前解決」節、ロードマップ第2段6「マングリング衝突検出」。
- 関連所見 M2（同名シンボルの多重import先勝ち）・L1（グローバル変数の`sanitizeIdentifier`縮退衝突）。
