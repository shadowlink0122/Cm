# ジェネリックインスタンス化の診断

不正なジェネリックインスタンス化（型引数の個数不一致・存在しない型引数・型引数なしの使用・明示型引数の個数不一致）は、単相化より上流の型検査で診断付きエラーにする。単相化パス（`mono/scan.cpp`）は推論できない呼び出しを黙ってスキップする設計であるため、型検査で弾かなければ「未定義シンボルへのリンク失敗」という原因の分かりにくい下流エラーに化ける。加えて、ジェネリック関数の宣言時に本体を1回走査し、境界（`<T: Ord>`等）を全く宣言せずに型パラメータ値へ比較演算子を使うコードを警告する前倒し検査を持つ。

## 概要

診断は発生位置ごとに4系統ある。

| 不正の形 | 例 | 診断ID | 検査位置 |
|---|---|---|---|
| 型引数の個数不一致 | `Pair<int> p;` | `TypeGenericArgumentCountMismatch` | `is_valid_type` / `check_let` |
| 型引数なしの使用 | `Pair q;` | `TypeGenericTypeRequiresArguments` | `check_let` |
| 存在しない型引数 | `Pair<int, Nope> r;` | `TypeUnknownTypeArgument` | `is_valid_type` / `check_let` |
| 関数の明示型引数の個数不一致 | `size_of<A, B>()` | `TypeGenericFunctionArgumentCountMismatch` | `call/function.cpp` |
| 境界未宣言の演算子使用 | 境界なし`T`に`a < b` | `TypeGenericBoundMissing`（警告） | 宣言時の本体走査 |

診断IDの定義は`src/internal/base/messages/ids.hpp:317-322`にある。インスタンス化時の制約充足（`<T: Ord>`に対する実型の検査）は[../interface/static-dispatch.md](../interface/static-dispatch.md)で扱う`check_type_constraints`が担う。

## データ構造とアルゴリズム

### 型の妥当性検査での個数・存在の検証

`TypeChecker::is_valid_type`（`src/internal/types/checking/utils/compat.cpp:541`）は、名前の存在確認に加えて型引数の個数と各型引数の存在を検証する。

```cpp
// compat.cpp:570-584
auto sd_it = struct_defs_.find(type->name);
if (sd_it != struct_defs_.end() && sd_it->second && !type->type_args.empty() &&
    type->type_args.size() != sd_it->second->generic_params.size()) {
    error(current_span_,
          i18n::msgf(i18n::MsgId::TypeGenericArgumentCountMismatch, type->name, ...));
}
// 各型引数の存在を再帰検証する（Pair<int, Nope> の Nope 等を検出）
for (const auto& arg : type->type_args) {
    if (arg && !is_valid_type(arg)) {
        error(current_span_, i18n::msgf(i18n::MsgId::TypeUnknownTypeArgument, ...));
    }
}
```

個数検査は「型引数が明示されているのに定義と食い違う」場合のみ発火し、型引数なしの使用は推論の余地があるためこの経路では対象外にして誤検出を避ける。非ジェネリック型への型引数付与は「パラメータ数0との不一致」として同じ経路で検出される。検査は`type_args`へ再帰するため、ネストした型引数（`Box<Pair<int, Nope>>`）内の未定義名も検出される。

### 変数宣言での検証

ローカル変数宣言は`is_valid_type`を通らないため、`check_let`（`src/internal/types/checking/stmt.cpp:238`）が独立に検査する。typedefを解決した実型に対して、個数不一致（`stmt.cpp:246-252`）、ジェネリック構造体を型引数なしで使う宣言（`stmt.cpp:253-259`、`Pair p;`は推論材料が無いためエラー）、各型引数の存在（`stmt.cpp:260-268`）を順に検証する。型引数なし検査は変数宣言位置に限定しており、構造体フィールド等の宣言位置は自己参照や推論の余地があるため対象外である。マングル済み名（`__`を含む）も特殊化名との誤検出を避けるため除外する（`stmt.cpp:256`）。

### 関数呼び出しの明示型引数の検証

`size_of<WorkerArg>()`のような明示型引数付き呼び出しは、パーサーが`size_of<WorkerArg>`という識別子名を生成するため、`call/function.cpp`が`<`で分割して基底名と型引数をパースする（`src/internal/types/checking/call/function.cpp:92-114`）。パース後に個数の一致を検証する。

```cpp
// call/function.cpp:116-122
if (explicit_type_args.size() != base_gen_it->second.size()) {
    error(current_span_,
          i18n::msgf(i18n::MsgId::TypeGenericFunctionArgumentCountMismatch,
                     base_name, std::to_string(base_gen_it->second.size()),
                     std::to_string(explicit_type_args.size())));
}
```

推論経由の呼び出しは`infer_generic_call`（`src/internal/types/checking/generic/infer.cpp:16`）が実引数の個数（デフォルト引数考慮、`infer.cpp:36-48`）と、推論された型引数の制約充足（`infer.cpp:134-157`）を検証する。

### 境界未宣言の演算子使用の前倒し検査

`check_generic_operator_bounds`（`src/internal/types/checking/generic/bounds.cpp:41`）は、ジェネリック関数の登録時（`src/internal/types/checking/decl.cpp:270`から呼ばれる）に本体ASTを1回走査する。型パラメータ型の引数・ローカル変数への比較演算子の使用を収集し、`<` `<=` `>` `>=`は`Ord`、`==` `!=`は`Eq`を要求能力とする（`bounds.cpp:24-37`）。対象の型パラメータに境界が全く宣言されていない場合のみ`TypeGenericBoundMissing`を警告として発行する（`bounds.cpp:96-98`）。境界が1つでも宣言されていればそのインターフェイスが演算子を提供しうるため対象外とする保守的検査であり、検査が宣言時に前倒しされるため全インスタンス化へ一律に効く。警告に留めるのは、既存の正当なコードがインスタンス化側の制約検査で守られており、破壊的変更を避けるためである。

### 診断の動作例

```cm
struct Pair<T, U> { T first; U second; }

int main() {
    Pair<int> p;        // エラー: Pair は2個の型引数を要求（1個が与えられた）
    Pair q;             // エラー: Pair には型引数が必要（推論材料が無い）
    Pair<int, Nope> r;  // エラー: 型引数 'Nope' は未定義
    return 0;
}
```

境界未宣言の演算子使用は宣言時に検出される。

```cm
<T> T max_of(T a, T b) {   // 境界 Ord が無いのに < を使用
    if (a < b) { return b; }
    return a;
}
```

```
warning: type parameter 'T' uses operator '<' but no interface bound is declared (declare <T: Ord>)
```

`<T: Ord> T max_of(T a, T b)`と宣言した形は警告なしで通り、インスタンス化側では推論された実型が`Ord`を満たすかが別途検証される。

## 実装箇所

| ファイル | 役割 |
|---|---|
| `src/internal/types/checking/utils/compat.cpp` | `is_valid_type`での個数・存在の再帰検証 |
| `src/internal/types/checking/stmt.cpp` | `check_let`での宣言位置の検証（個数・引数なし・存在） |
| `src/internal/types/checking/call/function.cpp` | 明示型引数のパースと個数検証 |
| `src/internal/types/checking/generic/infer.cpp` | 推論経由呼び出しの引数個数・制約充足の検証 |
| `src/internal/types/checking/generic/bounds.cpp` | 宣言時の演算子能力と境界の突き合わせ |
| `src/internal/types/checking/decl.cpp` | ジェネリック関数登録と境界検査の起動 |
| `src/internal/base/messages/ids.hpp` | 診断IDの定義 |

## 落とし穴とケア

- 防ぐバグのクラス: 「黙って壊れる」下流エラー。単相化のスキャナは型引数を推論できない呼び出しをデバッグログのみで黙ってスキップし（`src/internal/mir/lowering/mono/scan.cpp:56-59`）、特殊化が生成されないまま呼び出し名が残ると未定義シンボルのリンク失敗になる。不正なインスタンス化は必ず型検査段階で診断付きエラーにし、単相化スキップ経路を診断の代替にしないこと。
- 誤検出回避の境界線: 型引数なし検査を変数宣言以外（フィールド宣言・戻り値等）へ広げると、自己参照型や推論可能な位置で誤検出する。検査位置を広げる場合は既存スイート（`tests/common/generics/`全体）で誤検出ゼロを確認すること。
- 維持すべき不変条件: `check_let`と`is_valid_type`の検査は重複して発火しうるため、片方を変更する際はもう片方との診断重複・漏れを確認すること。マングル済み名（`__`入り）の除外条件を外すと特殊化名の宣言が誤ってエラー化する。
- 境界検査の限界: `check_generic_operator_bounds`は境界「不足」（`<T: Clone>`で`<`を使う等）を検出せず、メソッド要求（`t.foo()`）やジェネリックimplメソッド本体も対象外である。検査対象を広げる場合はユーザー定義インターフェイスの演算子宣言との包含判定が必要になる。
- 回帰テスト: `tests/common/errors/generic_arg_count.cm`（個数不一致）、`tests/common/errors/generic_no_args.cm`（引数なし使用）、`tests/common/errors/generic_unknown_arg.cm`（未定義型引数）、`tests/common/generics/generic_bound_satisfied.cm`（正しい境界宣言の非退行）、`tests/common/generics/generic_constraints.cm`・`interface_bounds.cm`（制約検査の非退行）。

## 関連資料

- [ジェネリックインスタンス化の診断](../../archive/v0.17.0/diagnostics/generic-instantiation-diagnostics.md) — 導入時の設計文書
- [monomorphization.md](monomorphization.md) — 診断の下流にある単相化経路
- [../interface/static-dispatch.md](../interface/static-dispatch.md) — インスタンス化時の制約充足検査（`check_type_constraints`）
