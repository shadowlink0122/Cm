---
title: ジェネリックインスタンス化の診断
parent: v0.17.0 Design
---

# ジェネリックインスタンス化の診断

## 対象所見

| # | 領域 | 所見 | 状態 |
|---|------|------|------|
| H15 | ジェネリクス | 不正なインスタンス化（存在しない型引数・引数個数不足・引数なし使用）が無診断で通過し、下流の紛らわしいエラーか誤動作になる | 一部実装（型引数の個数不一致の診断を実装済み。存在検証・引数なし使用・制約充足は未着手） |
| L8 | ジェネリクス | ジェネリック本体で未宣言の演算子（`<`等）を使ってもインスタンス化側で拒否されない（`<T: Ord>`宣言時の検査自体は良好） | 未着手 |

## 実装済み: 型引数の個数検証（Phase 1）

型引数が明示されている構造体型（`Box<int, string>` 等）について、定義の `generic_params` 数と `type_args` 数の不一致を型検査で診断する（`TypeGenericArgumentCountMismatch`）。
- 変数宣言経路: `check_let`（`types/checking/stmt.cpp`）に個数検証を追加。
- フィールド・引数・戻り値・グローバル・typedef経路: `is_valid_type`（`types/checking/utils/compat.cpp`）に個数検証を追加。
- 誤検出回避のため、型引数を明示していない使用（`type_args` が空）は推論の余地があるとして対象外にしている（後続Phaseで文脈依存に扱う）。
- テスト: `tests/common/errors/generic_arg_count.cm`（非ゼロexit期待）。
残りの「存在しない型引数の検証」「引数なし使用の文脈依存診断」「本体要求能力と境界の突き合わせ（L8）」は未着手。

## 背景と根本原因

インスタンス化時（使用箇所）に、型引数の個数・存在・制約充足を検証する経路が欠けている。

### 型引数の個数・存在の未検証（H15）

型の妥当性検査 `TypeChecker::is_valid_type`（`src/internal/types/checking/utils/compat.cpp` 541-578行）は、`Struct`/`Interface`/`Generic` について「名前が `struct_defs_` 等に存在するか」だけを見る（560-574行）。
`type->type_args` の個数がジェネリックパラメータ数と一致するか、各型引数が有効な型かは一切検査しない。
`is_valid_type` は各宣言位置から呼ばれる（`decl.cpp` 361・440・505・550・575・581・1014・1023行）が、いずれも型引数の妥当性まで踏み込まない。

宣言登録時にジェネリックパラメータ数は `generic_structs_[st->name]`（`decl.cpp` 292行）・`generic_functions_[func->name]`（244行）へ保持されているが、使用側でこれと突き合わせる検査が無い。

単相化側では `src/internal/mir/lowering/mono/scan.cpp` 163行で `if (type_args.size() != generic_params.size()) continue;` と、個数不一致を**黙ってスキップ**する（診断を出さない）。
その結果、特殊化が生成されず未特殊化呼び出しが残り、下流で「未定義シンボル」等の紛らわしいエラーになる。
`src/internal/mir/lowering/mono/driver.cpp` 61-104行の不動点ループは `MAX_PASSES=64` 超過や新規特殊化ゼロで無診断脱出するため、取りこぼしはそのまま流出する。

### 制約充足検査の非対称（L8）

宣言側の制約検査は良好である。
ジェネリック関数呼び出しでは `src/internal/types/checking/generic.cpp` 134-157行で、推論された型引数が `generic_function_constraints_` の制約（`Ord`等）を満たすか `check_type_constraints`（`compat.cpp` 510-518行）で検査し、違反時に位置付きエラーを出す。
`check_constraint`（`generic.cpp` 249-258行）も同様。

問題は逆方向で、「ジェネリック本体が `<` 等の演算子を使っているのに、型パラメータに `Ord` 等の境界が宣言されていない」ケースをインスタンス化側で拒否しない（L8）。
本体が要求する能力（演算子・メソッド）と、宣言された境界の突き合わせが無いため、境界宣言を省いても具体型がたまたま演算子を持てば通り、持たなければ下流で失敗する。

## 設計方針

「インスタンス化時に、型引数の個数・存在・制約充足を検証し、明確な診断を出す」ことを型検査フェーズへ集約する。

### H15: 個数・存在の検証

`is_valid_type`（`compat.cpp`）を拡張し、`Struct`/`Generic` 型について以下を追加検査する。
1. 名前がジェネリック構造体（`generic_structs_`）である場合、`type->type_args.size()` がパラメータ数と一致するかを検査。不一致なら「型 `Foo` は N 個の型引数を要求しますが M 個が与えられました」。
2. 引数なし使用（`Box` を `Box<...>` なしで使用）は、パラメータ数 > 0 かつ `type_args` 空をエラー化。
3. 各 `type_args[i]` について再帰的に `is_valid_type` を適用し、存在しない型引数（未定義の型名）を検出。
4. 非ジェネリック型に型引数が付いている場合もエラー化（`int<string>` 等）。

ジェネリック関数呼び出しの明示型引数（`foo<int>(...)`）についても、`src/internal/types/checking/call/function.cpp`（85-96行で `generic_functions_` を引く箇所）で個数一致を検査する。
単相化側 `scan.cpp` 163行の黙殺スキップは、型検査で弾いた後の到達不能ケースとなる想定だが、防御的に `debug_msg` から診断可能な内部エラーへ格上げする（正常経路では発火しない不変条件チェック）。

### L8: 本体が要求する能力と境界の突き合わせ

ジェネリック本体（関数・メソッド）を一度、型パラメータを抽象型のまま走査し、各型パラメータに対して「本体が要求する能力集合」を収集する。
- 二項演算子 `<` `<=` `>` `>=` は `Ord` を要求。
- `==` `!=` は `Eq` を要求。
- ハッシュ利用（HashMapキー等）は `Hash` を要求。
- メソッド呼び出し `t.foo()` は対応するインターフェースメソッドを要求。

収集した要求集合が、宣言された境界（`generic_params_v2` の制約）に包含されるかを宣言時に検査し、境界不足をエラー化する（「型パラメータ `T` は `<` を使用していますが境界 `Ord` が宣言されていません」）。
これにより検査は宣言時に前倒しされ、全インスタンス化に対して一律に効く（インスタンスごとの検査より診断が安定する）。
既存の宣言時制約検査（`generic.cpp` 134-157行）と同じ `check_type_constraints` 基盤を再利用する。

## 構文例・出力例

H15（個数不足・引数なし・存在しない型引数）:

```
struct Pair<T, U> { T first; U second; }

int main() {
    Pair<int> p;        // エラー: Pair は2個の型引数を要求（1個が与えられた）
    Pair q;             // エラー: Pair は型引数が必要
    Pair<int, Nope> r;  // エラー: 型 'Nope' は未定義
    return 0;
}
```

期待: いずれも位置付きコンパイルエラー（非ゼロexit）。

L8（境界未宣言の演算子使用）:

```
<T> T max_of(T a, T b) {   // 境界 Ord が無いのに < を使用
    if (a < b) { return b; }
    return a;
}
```

期待診断:

```
error: 型パラメータ 'T' は演算子 '<' を使用していますが、境界 'Ord' が宣言されていません
  <T: Ord> と宣言してください
```

正しい形（従来どおり通る）:

```
<T: Ord> T max_of(T a, T b) {
    if (a < b) { return b; }
    return a;
}
```

## 実装の段階分割

- Phase 1: `is_valid_type`（`compat.cpp` 560-574行）に型引数の個数一致・引数なし使用・非ジェネリックへの型引数付与の検査を追加。新規 `MsgId` を i18n テーブルへ追加（H15の構造検査）。
- Phase 2: 型引数の再帰的存在検査（`is_valid_type` を `type_args` へ再帰）と、ジェネリック関数の明示型引数個数検査（`call/function.cpp`）。`scan.cpp` 163行の黙殺スキップを不変条件チェックへ格上げ。
- Phase 3: L8の能力収集パス（ジェネリック本体走査 → 要求能力集合）を追加し、宣言時に境界包含を検査。まず演算子（`Ord`/`Eq`）から対応し、メソッド要求（インターフェース境界）は後続。
- Phase 4: 診断メッセージの整備（要求箇所の位置提示・修正提案の付与）と、`driver.cpp` 不動点ループの無診断脱出（88-91行・MAX_PASSES到達）に対する内部診断の追加。

## テスト計画

- `tests/common/errors/generic_arg_count_mismatch.cm` + 空 `.error`: `Pair<int>`（個数不足）がエラーになること。
- `tests/common/errors/generic_no_args.cm` + 空 `.error`: `Pair` を型引数なしで使用するとエラーになること。
- `tests/common/errors/generic_unknown_arg.cm` + 空 `.error`: `Pair<int, Nope>` の未定義型引数がエラーになること。
- `tests/common/errors/generic_missing_bound.cm` + 空 `.error`: 境界 `Ord` 無しで `<` を使うジェネリック関数がエラーになること（L8）。
- `tests/common/generics/generic_bound_satisfied.cm` + `.expect`: `<T: Ord>` を正しく宣言したコードが従来どおり通り、正しい結果を出すこと（回帰保護）。
- `tests/regression/cases/hir_lowering/generic_diagnostics/`: 能力収集パスの単体（本体AST/HIRから要求能力集合を導出）と、`is_valid_type` の型引数個数検査をgtestで検証。
- 検証観点: 不正インスタンス化は非ゼロexit＋位置付き診断、正当な制約付きジェネリックは全バックエンドで従来どおり動作すること。既存の `tests/common/generics/generic_constraints.cm`・`interface_bounds.cm` が誤検出しないこと。

## リスクと非互換性

- 従来は無診断で「通っていた」不正インスタンス化がエラーになる。これらは下流で誤動作・紛らわしいエラーになっていたコードであり、正しく動く既存プログラムは対象外のため、実質的な破壊的変更は無い（監査テーマ1の是正）。
- L8の境界検査は、境界宣言を省いていた既存ジェネリックコードをエラー化しうる。修正は境界を1つ付けるだけ（`<T>`→`<T: Ord>`）で、意味論は変わらないため移行は容易。段階導入とし、能力収集の誤検出（過剰要求）が無いことをテストで担保してからエラー昇格する。
- 能力収集パスは本体走査コストを追加するが、宣言ごと一度きりで、インスタンス数に依存しないためコンパイル時間への影響は限定的。

## 関連

- [[type-identity-recursive-keys]]（C7/C8/C9）: 同じ単相化経路の型キー構造化。個数検証と併せて未特殊化残留を根絶する。
- [[mangling-collision-detection]]（C16）: 型検査での集約診断という同じ設計思想を共有。
- 監査レポート `docs/design/v0.17.0/large-scale-bottleneck-audit.md` の High所見 H15・Low所見 L8、「ジェネリクス/モノモーフ化」節（103-108行）、および健全確認点（148行「`<T: Ord>`制約宣言時の違反検出」）。
