# v0.15.0 Release - Cm言語コンパイラ

## 概要

v0.15.0は**SystemVerilog (SV) バックエンドの本格実装**と**プラットフォームディレクティブによるレキサーモード分離**を含むメジャーアップデートです。CmからFPGA向けのSystemVerilogコードを生成し、iverilogによるシミュレーション検証まで一貫して実行可能になりました。

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
| SV固有キーワード | `posedge`, `negedge`, `wire`, `reg`, `always`, `assign`, `bit`等（文脈キーワード） |
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

#### 三項演算子の自動最適化

if/elseが同一変数への単一代入のみの場合、`cond ? a : b` に自動変換。合成ツールの効率向上と可読性改善。

```sv
// 最適化前
if (sel) begin
    result = a;
end else begin
    result = b;
end
// 最適化後
result = sel ? a : b;
```

### プラットフォームディレクティブ（新規）

ソースファイル先頭の `//! platform: sv` ディレクティブまたは `--target=sv` CLIオプションにより、レキサーのキーワードテーブルをプラットフォームごとに切り替える仕組みを導入。

```cm
//! platform: sv
// この行以降、SV固有キーワードがトークンとして認識される
```

| モード | キーワード追加 | 用途 |
|--------|-------------|------|
| `LexerPlatform::Default` | なし | 通常のCmコード |
| `LexerPlatform::SV` | `posedge`, `negedge`, `wire`, `reg`, `always`, `always_ff`, `always_comb`, `always_latch`, `assign`, `initial`, `bit` | SVターゲット |

> 非SVモードでは`posedge`等は通常のIdent（変数名として使用可能）

### バグ修正

| 問題 | 修正内容 |
|------|----------|
| 型キーワードnamespace関数呼び出し失敗 | `parse_namespace()`/`current_text()`で`get_string()`→`token_kind_to_string()`に修正 |
| posedge/negedge/wire/regが非SVコードで型消費 | `parse_type()`のIdentテキスト比較ブロック削除、レキサーKwトークンに完全移行 |
| rstポート挿入位置の条件分岐 | `has_clk ? 1 : 1` → clk実位置を検索して挿入 |
| 非合成型チェック未呼び出し | `compile()`から`validateSynthesizableTypes()`を呼び出し、エラー時にコンパイル中止 |
| SV幅付きリテラル例外クラッシュ | `stoi`/`stoull`を`try-catch`で保護、値部空チェック・基数文字検証・フォールバック追加 |
| グローバル変数初期化子省略範囲 | SVポート型/アトリビュートのみ省略可能に限定 |
| Token/AST/HIR/MIR/codegenメモリ最適化 | SV幅付きリテラル情報を`std::optional<BitLiteralInfo>`に統一 |
| parameter二重宣言 | `sv::param`変数がparameter/logicで二重定義されるバグを修正 |

### ビルド高速化

| 変更 | 説明 |
|------|------|
| ccache自動検出 | `find_program(ccache)`で検出し`CMAKE_CXX_COMPILER_LAUNCHER`に設定 |
| Unity build | `CMAKE_UNITY_BUILD=ON`（バッチサイズ16）でヘッダパース回数を大幅削減 |
| ObjC++除外 | `gpu_runtime.mm`を`SKIP_UNITY_BUILD_INCLUSION`で個別コンパイル |
| SV生成物管理 | デフォルトSV出力を`.tmp/`に変更、`make clean`で`*.sv`/`*.vcd`/`*.vvp`削除 |
| コンパイラ警告0件達成 | 符号比較・演算子優先順位・未使用変数の警告を全解消 |

### VSCode拡張機能

| 変更 | 説明 |
|------|------|
| SV幅付きリテラルハイライト | `N'[dbh]VALUE`パターンを`constant.numeric.sv-literal.cm`として認識 |
| 文字リテラルパターン修正 | `'X'`(1文字のみ)にマッチするパターンに変更 |

### テスト基盤

| 変更 | 説明 |
|------|------|
| SV並列テスト | `unified_test_runner.sh`の`run_parallel_test`にSVバックエンド追加 (`make tsvp`) |
| CI iverilog追加 | `ci.yml`にiverilogインストール・SVシミュレーション検証を追加 |
| CI macOS安定化 | `brew install || true`廃止、失敗時にwarning+SVテストスキップへ明示的分岐 |
| tests/programs削除 | 全テストを`tests/`に統合、`tests/programs/`ディレクトリを廃止 |

### ドキュメント

| 変更 | 説明 |
|------|------|
| SVチュートリアル | `docs/tutorials/ja/compiler/sv.md` (ja/en) 新規作成 |
| リリースノート | `docs/releases/v0.15.0.md` 新規作成 |
| 設計書5件更新 | Phase 1 IMPLEMENTED反映、テストパス・ステータス更新 |

---

## 📁 変更ファイル一覧

### SVバックエンド（コード生成）

| ファイル | 変更内容 |
|---------|----------|
| `src/codegen/sv/codegen.cpp` | SVコード生成エンジン（インデント修正、一時変数最適化、rst挿入修正、非合成型チェック含む） |
| `src/codegen/sv/codegen.hpp` | SVCodeGenクラス定義 |

### パーサー / フロントエンド

| ファイル | 変更内容 |
|---------|----------|
| `src/frontend/lexer/token.hpp` | `BitLiteralInfo`構造体新設、Token構造体に`std::optional<BitLiteralInfo>`追加 |
| `src/frontend/lexer/lexer.hpp` | `LexerPlatform` enum定義、`add_sv_keywords()`/`detect_platform_directive()` |
| `src/frontend/lexer/lexer.cpp` | SVキーワード動的追加、SV幅付きリテラル例外防止（try-catch・基数検証） |
| `src/frontend/ast/expr.hpp` | AST LiteralExprに`std::optional<BitLiteralInfo>`追加 |
| `src/frontend/parser/parser_expr.cpp` | SV幅付きリテラルのパーサー対応、型キーワード名前空間修飾 |
| `src/frontend/parser/parser_type.cpp` | SV Identテキスト比較ブロック削除、KwPosedge等switch-case移行 |
| `src/frontend/parser/parser_stmt.cpp` | `is_type_start()`にKwPosedge等追加 |
| `src/frontend/parser/parser_decl.cpp` | `is_global_var_start()`にKwPosedge/KwNegedge対応 |
| `src/frontend/parser/parser_module.cpp` | namespace名取得の`get_string()`→`token_kind_to_string()`修正 |

### HIR / MIR

| ファイル | 変更内容 |
|---------|----------|
| `src/hir/nodes.hpp` | HirLiteralに`std::optional<BitLiteralInfo>`追加 |
| `src/hir/lowering/expr.cpp` | HIR lower_literalでbit_info伝搬 |
| `src/mir/nodes.hpp` | MirConstantに`std::optional<BitLiteralInfo>`追加 |
| `src/mir/lowering/expr_basic.cpp` | MIR lower_literalでbit_info伝搬 |

### ビルドシステム

| ファイル | 変更内容 |
|---------|----------|
| `CMakeLists.txt` | ccache自動検出、Unity build導入、ObjC++ SKIP_UNITY_BUILD_INCLUSION |
| `Makefile` | `make clean`に`*.sv`/`*.vcd`/`*.vvp`削除追加 |
| `.gitignore` | `*.vvp`追加、SV生成物セクション整理 |
| `src/codegen/buffered_codegen.hpp` | int/size_t符号比較警告修正 |
| `src/codegen/llvm/core/mir_to_llvm.cpp` | 未使用変数hasAsmOperands削除、isAsmReferencedにmaybe_unused |

### その他

| ファイル | 変更内容 |
|---------|----------|
| `vscode-extension/syntaxes/cm.tmLanguage.json` | SV幅付きリテラルハイライト、文字リテラル修正 |
| `vscode-extension/package.json` | バージョン0.15.0更新 |
| `.github/workflows/ci.yml` | SVテスト+iverilog追加、macOS brew install失敗時のスキップ分岐 |
| `tests/unified_test_runner.sh` | SVテスト並列実行対応 |
| `src/main.cpp` | SVデフォルト出力先を`.tmp/output.sv`に変更 |
| `VERSION` | 0.15.0 |

### 設計ドキュメント更新

| ファイル | 変更内容 |
|---------|----------|
| `docs/design/v0.15.0/systemverilog_backend.md` | Phase 1 → IMPLEMENTED |
| `docs/design/v0.15.0/systemverilog_codegen_pipeline.md` | Phase 1 → IMPLEMENTED |
| `docs/design/v0.15.0/method_chaining.md` | パーサー対応済み/型チェッカー未実装を明記 |
| `docs/design/v0.15.0/type_identity_and_name_resolution.md` | 未着手 → v0.16.0検討 |
| `docs/design/v0.15.0/module_separate_compilation.md` | v0.16.0先送り |

### テスト（新規23件）

| カテゴリ | テスト |
|---------|--------|
| sv/advanced | fsm, led_blinker, multi_clock, negedge_reset, parameterized, posedge_counter |
| sv/basic | adder, arithmetic, binary_bits, bitwise, counter, multi_expr, mux, shift, sv_width_literal, ternary, unary |
| sv/control | compare, nested_if, priority_encoder, shift_register, signed_ops |
| sv/memory | bram |

---

## 🧪 テスト状況

| バックエンド | 通過 | 失敗 | スキップ |
|------------|------|------|---------|
| JIT (O0) | 368 | 0 | 5 |
| SV | 23 | 0 | 0 |

---

## ✅ チェックリスト

- [x] SVバックエンド Phase 1-5 実装
- [x] SV固有型 (posedge/negedge/wire/reg) サポート
- [x] SV幅付きリテラル (N'd/N'b/N'h) パーサー対応
- [x] SV幅付きリテラルの元ベース形式保持
- [x] 一時変数インライン展開・不要logic宣言除去
- [x] SVコード生成インデント修正
- [x] プラットフォームディレクティブ (`//! platform: sv`) 実装
- [x] LexerPlatformモード (Default/SV) 導入
- [x] 非SVモードでのSVキーワード衝突解消
- [x] type_keyword_namespace バグ修正
- [x] テストディレクトリ統合 (tests/programs/ → tests/)
- [x] CI iverilog追加・SVシミュレーション検証
- [x] CI macOS brew install失敗時のスキップ分岐
- [x] VSCode拡張: SV幅付きリテラルハイライト
- [x] VSCode拡張: 文字リテラルパターン修正
- [x] SVテスト並列実行対応 (make tsvp)
- [x] SVバックエンドチュートリアル (ja/en)
- [x] v0.15.0リリースノート作成
- [x] 設計ドキュメント5件のステータス更新
- [x] SV幅付きリテラル例外防止 (stoi/stoull try-catch)
- [x] グローバル変数初期化子省略をSVポート型/属性に限定
- [x] BitLiteralInfo optional化 (Token→AST→HIR→MIR→codegen)
- [x] ccache自動検出・Unity build導入
- [x] make cleanにSV/VCD/VVP削除追加
- [x] SV出力先をルート→.tmp/に変更
- [x] コンパイラ警告0件達成
- [x] 三項演算子最適化 (if/else同一変数→cond?a:b)
- [x] parameter二重宣言修正
- [x] SVテスト追加 (ternary/fsm/parameterized, 20→23件)
- [x] CIランナー最新化 (ubuntu-latest/macos-15)
- [x] ローカルパス情報なし

---

**バージョン**: v0.15.0