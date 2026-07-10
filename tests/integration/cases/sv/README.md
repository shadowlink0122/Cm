# sv — SVコード生成ゴールデンテストの入力ソース

`tests/integration/sv_codegen_test.cpp` が読み込むCmソース断片。
ファイル名はテスト名のsnake_case（例: `SignedConstantComparison` →
`signed_constant_comparison.cm`）。

## E2Eテスト（tests/sv/）との役割の違い

| | 本ディレクトリ（C++統合テスト） | tests/sv/（E2E） |
|---|---|---|
| パイプライン | Lexer→Parser→HIR→MIR→SVCodeGen（preprocessor/型チェッカなし） | フルパイプライン |
| 検証対象 | **生成SVテキストの性質**（優先順位括弧・キャスト構文・lint_off・三項化等） | **動作**（verilator lint + iverilogシミュレーション値） |
| 期待値 | gtest内の部分文字列/完全一致アサーション | `.expect` ファイル（SIM_OK/TEST行） |

- `//! platform: sv` ディレクティブは不要（この経路では解釈されない）
- 型チェッカを通さないため、リテラルの型がフルパイプラインと異なる場合がある
  （例: 整数リテラルが `'sd` になる）
- 統合ランナー（unified_test_runner.sh）の走査対象外
