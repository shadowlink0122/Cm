# ジェネリックインスタンス化の診断（実装済み）

## 対象所見

| # | 領域 | 所見 | 状態 |
|---|------|------|------|
| H15 | ジェネリクス | 不正なインスタンス化（存在しない型引数・引数個数不足・引数なし使用）が無診断で通過し、下流の紛らわしいエラーか誤動作になる | 実装済み |
| L8 | ジェネリクス | ジェネリック本体で未宣言の演算子（`<`等）を使ってもインスタンス化側で拒否されない（`<T: Ord>`宣言時の検査自体は良好） | 実装済み（境界が全く無い場合を警告として検出。段階導入でエラー昇格は後続。境界不足の細分は残課題） |

## 背景と根本原因

インスタンス化時（使用箇所）に、型引数の個数・存在・制約充足を検証する経路が欠けていた。
型の妥当性検査 `TypeChecker::is_valid_type`（`src/internal/types/checking/utils/compat.cpp`）は「名前が `struct_defs_` 等に存在するか」だけを見ており、`type->type_args` の個数一致・各型引数の存在は検査されなかった。
単相化側 `mono/scan.cpp` は個数不一致を黙ってスキップし、特殊化が生成されず下流で「未定義シンボル」等の紛らわしいエラーになっていた。
逆方向の制約検査（本体が要求する演算子と宣言境界の突き合わせ）も存在しなかった（L8）。

## 実装した設計

### H15: 個数・存在の検証

- 型引数の個数不一致（`Pair<int>` 等）: `is_valid_type` と `check_let` で `TypeGenericArgumentCountMismatch` を診断（Phase 1で実装済み）。非ジェネリック型への型引数付与も個数0との不一致として同じ経路で検出される。
- 引数なし使用（`Pair q;`）: `check_let` でジェネリック構造体を型引数なしで変数宣言に使った場合に `TypeGenericTypeRequiresArguments` を診断。フィールド等の宣言位置は推論・自己参照の余地があるため対象外とし、誤検出を避ける。
- 存在しない型引数（`Pair<int, Nope>`）: `is_valid_type` が `type_args` へ再帰し、`check_let` でも各型引数を検証して `TypeUnknownTypeArgument` を診断。
- ジェネリック関数の明示型引数（`size_of<A, B>()` 等）: `call/function.cpp` の明示型引数パース後に個数一致を検証し `TypeGenericFunctionArgumentCountMismatch` を診断。

### L8: 本体が要求する能力と境界の突き合わせ

`src/internal/types/checking/generic/bounds.cpp` の `check_generic_operator_bounds` が、ジェネリック関数の登録時に本体ASTを一度走査する。
- 型パラメータ型の引数・ローカル変数に対する比較演算子の使用を収集する（`<` `<=` `>` `>=` → `Ord`、`==` `!=` → `Eq`）。
- 対象の型パラメータに境界が全く宣言されていない場合のみ `TypeGenericBoundMissing` を警告にする（既存の正当なコードはインスタンス化側の制約検査で守られているため、破壊的変更回避の方針に従いまず警告として導入し、誤検出が無いことを確認した後にエラーへ昇格する。境界が1つでもあれば対象外とする保守的検査。ユーザー定義インターフェースの演算子宣言との包含判定は残課題）。
- 検査は宣言時に前倒しされるため、全インスタンス化に対して一律に効く。

## 構文例・出力例

```
struct Pair<T, U> { T first; U second; }

int main() {
    Pair<int> p;        // エラー: Pair は2個の型引数を要求（1個が与えられた）
    Pair q;             // エラー: Pair には型引数が2個必要
    Pair<int, Nope> r;  // エラー: 型引数 'Nope' は未定義
    return 0;
}
```

L8（境界未宣言の演算子使用）:

```
<T> T max_of(T a, T b) {   // 境界 Ord が無いのに < を使用
    if (a < b) { return b; }
    return a;
}
```

診断（警告）:

```
warning: type parameter 'T' uses operator '<' but no interface bound is declared (declare <T: Ord>)
```

`<T: Ord>` と宣言した形は従来どおり通る。

## テスト

- `tests/common/errors/generic_arg_count.cm`: 個数不一致（Phase 1、実装済み）。
- `tests/common/errors/generic_no_args.cm`: 引数なし使用のエラー化。
- `tests/common/errors/generic_unknown_arg.cm`: 未定義型引数のエラー化。
- L8の警告はテストランナーがstderrを出力比較へ含める制約のため専用テストを置かず、既存 `basic_generics.cm` の境界明記（`<T: Ord>`）と手動確認で担保する。
- `tests/common/generics/generic_bound_satisfied.cm` + `.expect`: `<T: Ord>`・`<T: Eq>` を正しく宣言したコードの回帰保護。
- 既存の `generic_constraints.cm`・`interface_bounds.cm` を含む全バックエンドスイートが誤検出なしで通ることを確認。

## 残課題（後続へ引き継ぎ）

- L8の警告からエラーへの昇格（誤検出ゼロの確認後）と境界「不足」判定の細分化（例: `<T: Clone>` で `<` を使うケース）。ユーザー定義インターフェースの演算子宣言と要求能力の包含判定が必要。
- メソッド要求（`t.foo()` に対するインターフェース境界の検査）。
- `mono/scan.cpp` の個数不一致スキップの内部診断化（型検査で先に弾かれるため実質到達しないが、防御的な不変条件チェックは未着手）。
- ジェネリックimplメソッド本体への L8 検査の拡張（現在は自由関数のみ）。

## 関連

- [[type-identity-recursive-keys]]（C7/C8/C9）: 同じ単相化経路の型キー構造化。
- [[mangling-collision-detection]]（C16）: 型検査での集約診断という同じ設計思想を共有。
- 監査レポート `large-scale-bottleneck-audit.md` の High所見 H15・Low所見 L8。
