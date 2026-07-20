---
title: マングリング名の衝突検出
parent: v0.17.0 Design
---

# マングリング名の衝突検出

## 対象所見

| # | 領域 | 所見 | 状態 |
|---|------|------|------|
| C16 | 識別子 | `Struct__method`マングリングが自由関数・モジュール修飾名（`A::b`→`A__b`）と同一名前空間で、同名の自由関数があるとメソッド本体が黙って消える（全バックエンド共通誤コンパイル） | 未着手 |

## 背景と根本原因

Cmはメソッド・自由関数・モジュール修飾名・特殊化を、いずれも `__` を含むフラットなシンボル名へマングルし、それらを単一のシンボル空間へ登録する。
衝突ガードが無いため、同名が生じると片方が黙って上書き・消滅する。

### マングル名の生成箇所

- メソッド定義: `MirLowering::lower_impl`（`src/internal/mir/lowering/impl.cpp` 362-363行）で `mir_func->name = type_name + "__" + method->name;` を生成する。
- メソッド呼び出し: `lower_member`（`src/internal/hir/lowering/expr_member.cpp` 1032行）で `hir->func_name = method_type_name + "__" + mem.member;` を生成する（呼び出し側）。
- 演算子実装: `impl.cpp` 75行 `mir_func->name = type_name + "__" + op_name;`。
- コンストラクタ/デストラクタ: `src/internal/hir/lowering/decl.cpp` 338行・304行で `target_type + "__ctor"` / `"__dtor"`。
- モジュール修飾名: `src/internal/hir/lowering/impl.cpp`（namespace flatten, 139行）で `full_namespace + "::" + original_name` を生成し、コード生成側で `::` を `__` へ変換する（`src/internal/codegen/js/types.hpp` の `sanitizeIdentifier` 37-39行、LLVM側は `src/internal/codegen/llvm/core/terminator/call.cpp` 108-122行）。
- ジェネリック特殊化: [[type-identity-recursive-keys]] 参照（`typeinfo.cpp` の `make_specialized_name` 等）。

### 衝突が黙殺される地点

`lower_impl`（`impl.cpp`）はメソッドを `hir_functions[mir_func->name] = method.get();`（369行）へ登録し、`mir_program.functions.push_back(...)`（380行）へ積む。
`hir_functions` は `std::unordered_map` であり、同名キーへの再代入は無警告で上書きする。
したがって、ユーザーが構造体 `Struct` にメソッド `method` を持たせつつ、自由関数 `Struct__method`（あるいはモジュール `Struct` の関数 `method`＝`Struct::method`→`Struct__method`）を定義すると、両者は同一シンボルへ縮退し、後勝ちで一方の本体が消える。

`src/internal/types/checking/decl.cpp` 267-286行には自由関数同士の重複検出（`defined_function_sigs_`、シグネチャ完全一致は許容し不一致はエラー）が既にあるが、これは「自由関数 対 自由関数」に限定され、メソッド・モジュール修飾・特殊化のマングル名は登録対象外である。

さらに `sanitizeIdentifier`（`types.hpp` 27-60行）は `::`→`__`（37-39行）、`[]`→`_arr`（42-44行）、`<>, []`→`_`（46-52行）という多対一の縮退を行うため、マングル前は異なる名前が生成コード上で同名になりうる（L1・M2の縮退と同根）。

## 設計方針

全マングル名を単一のシンボルテーブルへ登録し、衝突をハードエラー化する。

### データ構造

型検査フェーズに、生成予定の全リンケージ名を集約する `MangledSymbolTable` を追加する。
- キー: 最終マングル名（`Struct__method`・`A__b`・特殊化名）。
- 値: 由来（`Method` / `FreeFunction` / `ModuleQualified` / `Constructor` / `Specialization`）・宣言位置 `Span`・元の非マングル名。
- 登録時に既存キーがあれば、由来と`Span`を添えて診断付きハードエラーを発行する。ただし既存の自由関数重複規則（`decl.cpp` 267-286行）と同様に「同一シグネチャの再登録（モジュールflattenによる重複出現）」は許容する。

### 検出を挟む位置

型検査の宣言登録（`TypeChecker::register_declaration`、`decl.cpp` 236行〜）を単一の集約点とする。
- 自由関数登録（242-286行）: 既存の`defined_function_sigs_`を`MangledSymbolTable`へ発展させ、マングル名（自由関数は素の名前）で登録。
- impl登録: メソッド・演算子・ctor/dtorのマングル名（`impl.cpp`と同じ規則 `type_name + "__" + method_name`）を、型検査時に先回りで計算して登録する。マングル規則は`impl.cpp`と重複させず、共有ヘルパ `mangle_method_name(type, method)` に切り出して両者から呼ぶ。
- モジュール修飾名: `::`→`__`変換後の名前で登録し、メソッドマングル名と同一空間で突き合わせる。
- 特殊化名: [[type-identity-recursive-keys]] の可逆エンコードキーで登録し、原理的な縮退を排除したうえで最終防波堤とする。

`sanitizeIdentifier` の多対一縮退（`types.hpp` 27-60行）についても、縮退後の生成コード名で二次登録を行い、マングル前は別名だが生成後に衝突する組（L1）を検出する。ただしこれはコード生成直前の検査とし、正当な型名（`int[]`等）が過剰にエラー化しないよう縮退後キーは警告レベルから始める。

### 診断

- エラーメッセージは i18n テーブル（`kMessages[msg][lang]`、断片連結禁止のプロジェクト方針に従う）へ新規 `MsgId` を追加する。
- 例: 「メソッド `Struct.method`（マングル名 `Struct__method`）が自由関数 `Struct__method`（<位置>）と衝突します」。両宣言の位置を提示する。

## 構文例・出力例

該当なし（言語構文の追加は無い）。診断出力の例のみ示す。

現状は黙って一方が消えるプログラム:

```
struct Struct { int x; }
impl Struct {
    int method(self) { return self.x; }
}
int Struct__method() { return -1; }   // 自由関数がメソッドと同名マングル

int main() {
    Struct s;
    s.x = 7;
    println("{s.method()}");   // 現状: どちらが呼ばれるか非決定、本体が黙って消える
    return 0;
}
```

設計後の期待診断（コンパイル失敗・非ゼロexit）:

```
error: シンボル 'Struct__method' が重複しています
  method Struct.method  ...:  （メソッド定義）
  Struct__method()      ...:  （自由関数定義）
```

## 実装の段階分割

- Phase 1: `mangle_method_name` 共有ヘルパを抽出し、`impl.cpp`（362-363行・75行）と `expr_member.cpp`（1032行）・`decl.cpp`（ctor/dtor）を同一規則へ寄せる（挙動不変のリファクタ）。
- Phase 2: `MangledSymbolTable` を型検査へ追加し、自由関数・メソッド・ctor/dtor・モジュール修飾名を登録して衝突をハードエラー化。既存の`defined_function_sigs_`をこれへ統合。
- Phase 3: ジェネリック特殊化名（[[type-identity-recursive-keys]]の可逆キー）を登録し、C8の名前空間分離と二重で守る。
- Phase 4: `sanitizeIdentifier`縮退後キーの二次登録（L1・M2）。まず警告、誤検出が無いことを確認してからハードエラーへ昇格。

## テスト計画

- `tests/common/errors/mangle_method_free_fn_collision.cm` + 空 `.error`: 上記の `Struct.method` と `Struct__method()` の衝突がコンパイルエラー（非ゼロexit）になることを検証。
- `tests/common/errors/mangle_module_method_collision.cm` + 空 `.error`: モジュール `Struct` の関数 `method`（`Struct::method`）と型 `Struct` のメソッド `method` の衝突を検証。
- `tests/common/errors/mangle_specialization_user_type.cm` + 空 `.error`: ユーザー型 `Vector__int` とジェネリック `Vector<int>` 特殊化の衝突検出（C8連携）。
- `tests/regression/cases/hir_lowering/mangling/`: `mangle_method_name` ヘルパの単体（型名・メソッド名・ジェネリック型からの生成規則）と、`MangledSymbolTable` の重複許容（同一シグネチャ再登録）／拒否（別由来同名）をgtestで検証。
- 回帰: 既存の正当なプログラム（`tests/common/impl/`・`advanced_modules/`）が誤検出でエラー化しないこと。特にモジュールflattenによる同一定義の複数出現が許容されること。
- 検証観点: 衝突時に非ゼロexit＋位置付き診断、正当コードは全バックエンドで従来どおり通ること。

## リスクと非互換性

- 「黙って壊れていた」プログラムがコンパイルエラーになるため、表面上は新たにエラーが増える。ただしこれらは従来から誤コンパイルされており正しく動いていなかったため、実質的な破壊的変更ではない（監査の構造的テーマ1「黙って壊れる」の是正）。
- モジュールflattenで同一定義が重複出現する既存挙動を誤ってエラー化しないよう、シグネチャ一致の再登録許容を厳密に保つ必要がある。
- `sanitizeIdentifier`縮退後キーの検査は誤検出リスクがあるため、段階導入（警告→エラー）とする。

## 関連

- [[type-identity-recursive-keys]]（C7/C8/C9）: 特殊化名の可逆エンコードキー。本設計のシンボルテーブルへ登録する。
- 監査レポート `docs/design/v0.17.0/large-scale-bottleneck-audit.md` の「識別子/名前解決」節（119-122行）、構造的テーマ、ロードマップ第2段6「マングリング衝突検出」。
- 関連所見 M2（同名シンボルの多重import先勝ち）・L1（グローバル変数の`sanitizeIdentifier`縮退衝突）。
