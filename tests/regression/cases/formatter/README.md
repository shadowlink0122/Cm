# formatter — フォーマッタ統合テストの入力ソース

`tests/regression/formatter_test.cpp` が読み込むケースファイル。テーマ別のサブフォルダ（ifdef/ continuation/ literal/ style/）に配置し、テストからは `ifdef/top_level` のようにフォルダつきの名前で参照する。

## ファイル規約

| パターン | 役割 |
|---|---|
| `<name>.input` | 整形前の入力（**意図的に未整形**。`cm fmt` の一括適用対象にならないよう `.cm` 拡張子を付けない） |
| `<name>.expected.cm` | `<name>.input` の期待整形結果 |
| `<name>.cm` | 安定ケース（整形済みで、fmt適用後も変化しないことを検証） |

- テストは `format(input) == expected` に加えて、expected / 安定ケースへの再適用で変化しないこと（冪等性）も検証する
- expected を更新する場合は `cp <name>.input <name>.expected.cm` の後に `cm fmt <name>.expected.cm` を実行して生成する（手書きしない）
- 入力fixtureが `.cm` だった頃、リポジトリ全体への `cm fmt` 一括適用で入力が整形されテストが無意味化する事故が起きたため、拡張子を外している
