# formatter — フォーマッタ統合テストの入力ソース

`tests/integration/formatter_test.cpp` が読み込むケースファイル。

## ファイル規約

| パターン | 役割 |
|---|---|
| `<name>.input.cm` | 整形前の入力（**意図的に未整形。`cm fmt` をかけないこと**） |
| `<name>.expected.cm` | `<name>.input.cm` の期待整形結果 |
| `<name>.cm` | 安定ケース（整形済みで、fmt適用後も変化しないことを検証） |

- テストは `format(input) == expected` に加えて、expected / 安定ケースへの再適用で変化しないこと（冪等性）も検証する
- expected を更新する場合は `cp <name>.input.cm <name>.expected.cm` の後に`cm fmt <name>.expected.cm` を実行して生成する（手書きしない）
- `make format` の走査対象（tests/common, libs）には含まれないが、リポジトリ全体へ一括で `cm fmt` をかけると `.input.cm` が壊れるので注意
