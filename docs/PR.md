# v0.15.0 Release - Cm言語コンパイラ

## 概要

v0.15.0は**SystemVerilog (SV) バックエンドの本格実装**を含むメジャーアップデートです。CmからFPGA向けのSystemVerilogコードを生成し、Tang Console等のFPGAボードで直接動作させることが可能になりました。

---

## 🔥 v0.15.0 変更点

### SystemVerilogバックエンド（新規）

MIRからSystemVerilogへの変換パイプラインを新規実装。構造化CFG走査により、MIRの基本ブロックからif/else/case等のSV制御構文を正確に再構築します。

```bash
cm compile --target=sv program.cm -o output.sv
```

#### 主要機能

| 機能 | 説明 |
|------|------|
| ポート宣言 | `#[input]`/`#[output]`アトリビュートでI/Oポート宣言 |
| 組み合わせ回路 | 通常関数 → `always_comb begin ... end` |
| 順序回路 | `posedge`/`negedge`型引数 → `always_ff @(posedge clk)` |
| SV固有型 | `posedge`, `negedge`, `wire`, `reg`型 |
| 非ブロッキング代入 | 順序回路で`<=`を自動使用 |
| BRAM推論 | 配列をBlock RAMとして推論 |
| テストベンチ自動生成 | iverilog互換の`_tb.sv`を自動生成 |
| マルチクロックドメイン | `sv::clock_domain(clk_name)`アトリビュート |
| XDC制約ファイル生成 | `sv::pin`アトリビュートでピン配置指定 |

#### SV幅付きリテラル

SystemVerilog形式の幅付きリテラル(`N'd`, `N'b`, `N'h`)をCmパーサーで直接サポート。Token→AST→HIR→MIR→SV codegenの全段で元のベース形式を保持して出力。

```cm
out = 3'b101;   // → 3'b101 (2進数)
out = 8'hFF;    // → 8'hFF  (16進数)
out = 8'd170;   // → 8'd170 (10進数)
```

#### 一時変数の最適化

MIR→SV変換時に一時変数(`_tXXXX`)をインライン展開し、使用されなくなった変数のlogic宣言を自動除去。クリーンなSV出力を実現。

### VSCode拡張機能

| 変更 | 説明 |
|------|------|
| SV幅付きリテラルハイライト | `N'[dbh]VALUE`パターンを`constant.numeric.sv-literal.cm`として認識 |
| 文字リテラルパターン修正 | `'X'`(1文字のみ)にマッチするパターンに変更、`8'hFF`が文字として誤認識される問題を修正 |

### テスト基盤

| 変更 | 説明 |
|------|------|
| SV並列テスト | `unified_test_runner.sh`の`run_parallel_test`にSVバックエンド追加 (`make tsvp`) |
| CI追加 | `ci.yml`のintegration-testに`sv-o3`を追加 |

### ドキュメント

| 変更 | 説明 |
|------|------|
| SVチュートリアル | `docs/tutorials/ja/compiler/sv.md` (ja/en) 新規作成 |
| リリースノート | `docs/releases/v0.15.0.md` 新規作成 |

---

## 📁 変更ファイル一覧

### SVバックエンド（コード生成）

| ファイル | 変更内容 |
|---------|----------|
| `src/codegen/sv/codegen.cpp` | SVコード生成エンジン（インデント修正、一時変数最適化含む） |
| `src/codegen/sv/codegen.hpp` | SVCodeGenクラス定義 |

### パーサー / フロントエンド

| ファイル | 変更内容 |
|---------|----------|
| `src/frontend/lexer/token.hpp` | Token構造体にbit_width/bit_base/bit_original追加 |
| `src/frontend/lexer/lexer.cpp` | SV幅付きリテラル(`N'[dbh]VALUE`)のトークン化 |
| `src/frontend/ast/expr.hpp` | AST LiteralExprにbit_width/bit_base/bit_original追加 |
| `src/frontend/parser/parser_expr.cpp` | SV幅付きリテラルのパーサー対応 |

### HIR / MIR

| ファイル | 変更内容 |
|---------|----------|
| `src/hir/nodes.hpp` | HirLiteralにbit_width/bit_base/bit_original追加 |
| `src/hir/lowering/expr.cpp` | HIR lower_literalでbit_width/bit_base/bit_original伝搬 |
| `src/mir/nodes.hpp` | MirConstantにbit_width/bit_base/bit_original追加 |
| `src/mir/lowering/expr_basic.cpp` | MIR lower_literalでbit_width/bit_base/bit_original伝搬 |

### その他

| ファイル | 変更内容 |
|---------|----------|
| `vscode-extension/syntaxes/cm.tmLanguage.json` | SV幅付きリテラルハイライト、文字リテラル修正 |
| `vscode-extension/package.json` | バージョン0.15.0更新 |
| `.github/workflows/ci.yml` | SVテストをCIに追加 |
| `tests/unified_test_runner.sh` | SVテスト並列実行対応 |
| `VERSION` | 0.15.0 |

### テスト（新規20件）

| カテゴリ | テスト |
|---------|--------|
| sv/advanced | led_blinker, multi_clock, negedge_reset, posedge_counter |
| sv/basic | adder, arithmetic, binary_bits, bitwise, counter, multi_expr, mux, shift, sv_width_literal, unary |
| sv/control | compare, nested_if, priority_encoder, shift_register, signed_ops |
| sv/memory | bram |

---

## 🧪 テスト状況

| バックエンド | 通過 | 失敗 |
|------------|------|------|
| JIT (O0) | 399 | 1 (既存バグ) |
| SV (O3) | 20 | 0 |

---

## ✅ チェックリスト

- [x] SVバックエンド Phase 1-5 実装
- [x] SV固有型 (posedge/negedge/wire/reg) サポート
- [x] SV幅付きリテラル (N'd/N'b/N'h) パーサー対応
- [x] SV幅付きリテラルの元ベース形式保持
- [x] 一時変数インライン展開・不要logic宣言除去
- [x] SVコード生成インデント修正
- [x] VSCode拡張: SV幅付きリテラルハイライト
- [x] VSCode拡張: 文字リテラルパターン修正
- [x] SVテスト並列実行対応 (make tsvp)
- [x] CIにSVテスト追加
- [x] SVバックエンドチュートリアル (ja/en)
- [x] v0.15.0リリースノート作成
- [x] ローカルパス情報なし

---

**バージョン**: v0.15.0