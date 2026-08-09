# 括弧付き型と typeof を対象にした as キャスト（型文法の汎用合成）

## 背景（局所処理の検査）

`x as (typeof(VAL))` のような組み合わせが、コンパイラの局所的（特殊ケース）な処理ではなく汎用的な型文法（BNF）の合成として扱われるかを検査したところ、以下の非汎用な箇所が見つかった。

- `as` キャストの対象型は既に汎用の `parse_type()` を通っており、`x as typeof(y)`（括弧なし）はパースできていた。
- しかし**型文法に括弧の規則が無く**、`x as (int)` や `x as (typeof(y))`（ユーザー指定形）が「Expected type」でパースエラーになっていた。これは `as` 固有ではなく型文法全体の欠落。
- `typeof(式)` を型位置で使うと（`parser_type.cpp`）、内側の式を**パース後に破棄**し、名前 `__typeof__` の `Inferred` 型を返すだけのスタブになっている。この `__typeof__` は型チェッカ/HIR で解決されないため、`auto k = i as typeof(j)` の結果に `typeof(k)` を適用すると具体型ではなく `<inferred>` が返る。

## 対応（本変更）

型文法に**括弧で囲んだ型**の規則を追加した（`parse_type()`）。`(T)`・`((T))`・`(typeof(x))`・`(int*)` を、型が要求される任意の位置（`as`/`is`・宣言・型引数等）で受理する。括弧は文法上のグルーピングであり AST では内側の型に展開される（`x as (int)` は `x as int` と同じ AST）。閉じ括弧後のポインタサフィックス `(T)*` も許可する。

これにより `x as (typeof(y))` および `x as (int)` 等がパースできるようになり、`as` の対象型が汎用の型文法として合成される。ネイティブ系（interpreter/jit/llvm/llvm-wasm）では `as typeof(x)` の値キャストが具体型へ解決され幅も正しく適用される（例: `long(5e9) as (typeof(intVar))` は int 幅へ切り詰められる）。

## typeof 型の静的解決（本変更で対応）

当初「別課題」としていた typeof 型の静的解決を実装した。恒久対応どおり `ast::Type` に typeof の被演算式（`typeof_operand`）を保持させ（パーサは従来破棄していた被演算式を保存）、型チェッカのヘルパ `resolve_typeof` が被演算式を型検査して具体型へ解決する（ポインタ/参照/配列の要素側 typeof も再帰解決する）。解決サイトは以下:

- **as キャストの対象型**（`x as typeof(y)`）: 対象型ノードを解決済み具体型へ差し替えるため、後段の lowering も具体型を見る。これにより JS ターゲットでも typeof キャストが解決され、全バックエンドで幅・切り詰めが一致する（`long as typeof(intVar)` が 32bit へ、`as typeof(longVar)` が 64bit を保つ）。
- **宣言型**（`typeof(j) k = i;`）: `is_type_start` へ `typeof(...)` 直後に識別子（＋ポインタ/配列サフィックス）が続く場合を宣言開始として追加し（式文 `typeof(x);` とは閉じ括弧の次で区別）、`check_let` で宣言型を `resolve_typeof` で解決する。`auto` 経由の結果型・結果への `typeof()` も解決済み具体型を返す。

## 残る制限（別課題）

- **仮引数型**（`int g(typeof(1) x)`）は、シグネチャ登録が本体検査に先行するため未解決のまま残る（局所処理調査B3。宣言型・キャストとは解決タイミングが異なるため別途対応）。
- 括弧宣言 `(int) k`（typeof でない括弧型の宣言）は typeof 解決に依存しない最尤パース曖昧性の課題であり本変更のスコープ外（局所処理調査A1のparen分）。

## テスト

- `tests/common/types/casting/paren_type`: `d as (int)` / `d as ((int))`（全バックエンドで移植可能）。
- `tests/common/types/casting/typeof_cast`: `d as typeof(i)` / `d as (typeof(i))` / `big as (typeof(i))`（`//! platform: !sv`。JS を含む全非SVバックエンドで typeof キャストが解決されることを検査）。
- `tests/common/types/casting/typeof_decl`: `typeof(i) k` 宣言型・`typeof(long) big` の幅解決・`auto = as typeof(...)` の結果型解決（`//! platform: !sv`）。
