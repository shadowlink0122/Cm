# 括弧付き型と typeof を対象にした as キャスト（型文法の汎用合成）

## 背景（局所処理の検査）

`x as (typeof(VAL))` のような組み合わせが、コンパイラの局所的（特殊ケース）な処理ではなく汎用的な型文法（BNF）の合成として扱われるかを検査したところ、以下の非汎用な箇所が見つかった。

- `as` キャストの対象型は既に汎用の `parse_type()` を通っており、`x as typeof(y)`（括弧なし）はパースできていた。
- しかし**型文法に括弧の規則が無く**、`x as (int)` や `x as (typeof(y))`（ユーザー指定形）が「Expected type」でパースエラーになっていた。これは `as` 固有ではなく型文法全体の欠落。
- `typeof(式)` を型位置で使うと（`parser_type.cpp`）、内側の式を**パース後に破棄**し、名前 `__typeof__` の `Inferred` 型を返すだけのスタブになっている。この `__typeof__` は型チェッカ/HIR で解決されないため、`auto k = i as typeof(j)` の結果に `typeof(k)` を適用すると具体型ではなく `<inferred>` が返る。

## 対応（本変更）

型文法に**括弧で囲んだ型**の規則を追加した（`parse_type()`）。`(T)`・`((T))`・`(typeof(x))`・`(int*)` を、型が要求される任意の位置（`as`/`is`・宣言・型引数等）で受理する。括弧は文法上のグルーピングであり AST では内側の型に展開される（`x as (int)` は `x as int` と同じ AST）。閉じ括弧後のポインタサフィックス `(T)*` も許可する。

これにより `x as (typeof(y))` および `x as (int)` 等がパースできるようになり、`as` の対象型が汎用の型文法として合成される。ネイティブ系（interpreter/jit/llvm/llvm-wasm）では `as typeof(x)` の値キャストが具体型へ解決され幅も正しく適用される（例: `long(5e9) as (typeof(intVar))` は int 幅へ切り詰められる）。

## 残る制限（別課題）

- `typeof` を**宣言型**として使う `typeof(j) k = i;` は、文が `typeof` で始まる宣言を文ディスパッチが認識せずパースエラーになる。
- `typeof` 型の**静的解決**は未実装（`__typeof__` を型チェッカで具体型へ解決していない）。このため `auto` 経由の結果型や結果への `typeof()` は `<inferred>` を返す。値としてのキャストはネイティブ系では効くが、JS ターゲットは `typeof` キャストを解決しない。
- 恒久対応には `ast::Type` に typeof の被演算式（または解決済み型）を保持させ、型チェッカで具体型へ解決する必要がある（AST/型基盤に跨る変更のため本変更のスコープ外）。

## テスト

- `tests/common/types/casting/paren_type`: `d as (int)` / `d as ((int))`（全バックエンドで移植可能）。
- `tests/common/types/casting/typeof_cast`: `d as typeof(i)` / `d as (typeof(i))` / `big as (typeof(i))`（`//! platform: !js|sv`。ネイティブ系と wasm で typeof キャストが解決されることを検査）。
