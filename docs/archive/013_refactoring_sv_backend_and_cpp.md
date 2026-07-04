# svバックエンド・コンパイラ実装 リファクタリング調査と改善提案

> **アーカイブ済み（2026-07-04）**: 本ドキュメントの提案は大部分が実装完了した。
>
> - §2の全コード生成欠陥: **修正済み**（v0.15.1リリースノート参照）
> - §3のテスト: **追加済み**
> - §4.3-2 monitoringクラスタのhpp/cpp分離: **実装済み**（未使用の guard.hpp / buffered.hpp / buffered_block.hpp は削除）
> - §4.3-3 naming.hpp の重複・死蔵コード: **削除済み**
> - §4.3-5 グローバル可変状態: compilation_guard の inline thread_local を関数ローカルへ移動済み（g_module_resolver / debug.hpp は未対応）
> - CMakeテストターゲットのソース共有化: **実装済み**（コンポーネント別ソースリスト方式）
> - 追加で発見・修正: MirOperand::constant のmove後参照による定数型消失、符号付き定数比較の unsigned 化（D8）
> - **未実装の提案**は docs/design/sv_backend_missing_features.md へ引き継ぎ

作成日: 2026-07-04
対象: Cmコンパイラ svバックエンド / CmCPU HDMIデザイン / コンパイラC++実装

本ドキュメントは以下3点の調査結果、実施済みの修正、および今後のリファクタリング提案をまとめたものである。

1. CmCPU（HDMI出力等）における Cm ↔ Verilog/SystemVerilog の意味的ギャップ
2. svバックエンドのコード生成欠陥の検証とテスト追加
3. コンパイラC++実装のベストプラクティス違反の修正

---

## 1. 調査方法

- CmCPU の `src/hdmi/**`（`hdmi_text_top.cm`、`encoder.cm`、`text_renderer.cm`、`animation_ctrl.cm` 等）と、生成済み `build/hdmi/hdmi_text.sv`（約6,300行）/`hdmi_colorbar.sv` を突き合わせて意味差を精査
- svバックエンド実装 `src/codegen/sv/codegen.{hpp,cpp}` の全域レビューと、最小再現ケースによる実挙動確認（verilator lint / iverilog+vvp シミュレーション）
- C++実装は `src/` 全体の統計（ヘッダー/実装比率、巨大TU、グローバル状態、スマートポインタ利用状況等）とビルド設定（CMake）を確認

---

## 2. 発見した意味的ギャップ・生成コード欠陥（修正済み）

### 2.1 【CRITICAL】演算子優先順位の括弧欠落 — TMDSエンコーダが機能不全

**症状**: Cmソースで `if ((r_qm & 256) == 0)` と正しく括弧を付けても、生成SVでは
`if (r_qm & 32'd256 == 32'd0)` と括弧が失われていた。
SystemVerilog では `==` が `&` より優先されるため `r_qm & (256 == 0)` = 恒偽となり、
**HDMIのTMDS DCバランス補正分岐が3チャンネルすべてで一度も実行されない**状態だった
（`hdmi_text.sv` / `hdmi_colorbar.sv` で各6箇所）。verilator は `--lint-only` のため
「合法だが意味が違う」このバグを検出できなかった。

**根本原因**: MIRの一時変数（`_tNNNN`）をインライン展開する際、代入文の右辺には
優先順位を考慮した括弧付与ロジック（`inline_temps`）が適用されるが、
**`if`/`case` 等の制御文の行は括弧なしの素置換**になっていた
（alwaysブロック経路と `function automatic` 経路の2箇所）。

**修正** (`src/codegen/sv/codegen.cpp`):
- 制御文経路の素置換ループを廃止し、代入文と同じ括弧付与ロジック
  （`inline_temps` / `fn_inline_temps`）を通すよう変更
- 併せて `get_outermost_operator` が `[]`（ビット選択・配列インデックス）内の演算子を
  最外演算子と誤認する問題を修正（`[` `]` も深さとしてカウント）

修正後の生成例: `if (((r_qm & 32'd256) == 32'd0))` — CmCPUの両HDMIデザインで確認済み。

### 2.2 【HIGH】レジスタ宣言初期値の消失 — シミュレーション不能・X伝播

**症状**: `uint state = 0;` のようなモジュールレベル変数の初期値が生成SVに一切
出力されず（`initial` ブロックもリセットロジックもなし）、シミュレータでは全レジスタが
`X` のまま FSM が起動しない。Gowin実機ではFF/BSRAMが0で電源投入されるため
「実機でだけ動く」状態になっており、ビルドフローがlint止まりである遠因になっていた。

**修正**: 属性なしモジュールレベル変数のレジスタ宣言に、宣言初期値を
`logic [31:0] state = 32'd0;` の形式で出力するようにした（FPGA合成では初期値として
扱われ、シミュレーションではX伝播を防ぐ）。配列（BRAM等）は対象外（今後の課題参照）。

### 2.3 【HIGH】キャスト（`as`）が完全に無視される — 式の値が変わる

**症状**: `MirRvalue::Cast` はオペランドをそのまま出力するだけで、縮小・拡大・符号変更の
いずれも生成SVに反映されなかった。代入の右辺全体なら代入時の暗黙切り捨てで偶然一致するが、
**式の途中の縮小キャストでは計算結果そのものが変わる**:

```cm
wide = ((a + 300) as utiny) + 1000;  // a=0 のとき正しくは 44+1000=1044
```

旧出力は `a + 32'd300 + 32'd1000` = 1300 で誤り。

**修正**: 整数型への幅変更キャストに SV サイズキャスト `N'(expr)` を出力
（例: `8'((a + 32'd300)) + 32'd1000`）。符号性が変わる場合は `$signed()`/`$unsigned()` を
併用。オペランド型が不明な場合も安全側でサイズキャストを出力する。
なお、オペランド型は `MirOperand::type` が未設定のケースがあるため、
`func.locals` から型を解決するヘルパー `resolve_operand_type` を追加した。

### 2.4 【HIGH】符号付き `>>` が論理シフトになる

**症状**: Cmの `>>` は符号付き型では算術シフト（LLVMバックエンドは `CreateAShr`、
JIT実行で `-8 >> 2 == -2` を確認）だが、svバックエンドは常に `>>`（SVでは論理シフト）を
出力しており、負数のシフト結果が巨大な正の値になっていた。

**修正**: 左オペランドが符号付き整数型のとき `>>>`（算術シフト）を出力。

### 2.5 【MEDIUM】enumの明示タグ値がビット幅計算に反映されない

**症状**: enumのビット幅を「メンバー数」から計算していたため、
`enum Status { IDLE = 0, ERROR = 100 }` が `typedef enum logic { ..., ERROR = 1'd100 }` と
**1ビット幅に100を詰めた不正なSV**になっていた。

**修正**: 最大タグ値とメンバー数-1の大きい方からビット幅を計算（上例は `7'd100`）。

### 2.6 【MEDIUM】配列型ポートのアンパックド次元消失

**症状**: `#[output] uint[4] data;` が `output logic [31:0] data` になり次元 `[0:3]` が
消失。本体の `data[idx] <= ...` は「配列要素への代入」ではなく「ビット選択」として解釈され、
意味が完全に壊れる（WIDTHTRUNCを抑止しているためlintでも発覚しない）。

**修正**: `SVPort` に `array_suffix` フィールドを追加し、ポートリスト・テストベンチの
信号宣言の両方でアンパックド次元を保持（`output logic [31:0] data [0:3]`）。

### 2.7 問題なしと確認した項目

| 項目 | 確認結果 |
|---|---|
| 文字列→packedベクトルのパッキング | `TITLE[(42 - idx) * 8 +: 8]` 形式で正しい（エンディアン問題なし） |
| `int` → `logic signed [31:0]` の符号比較 | encoderの視差FSM（`cnt_r > 0` / else）で正しく符号付き比較になる |
| 配列範囲外読み出し | ガード付きternaryで安全 |
| ラッチ推論・ゼロ除算 | 該当なし（combinational関数はデフォルト代入済み、除算は定数のみ） |
| 論理否定 `!` の多ビット誤変換 | 型検査が `!` をbool限定にしているため言語レベルで到達不能 |

---

## 3. 追加したテスト（tests/sv/）

いずれも iverilog+vvp による**シミュレーション値検証**付き（array_portのみコンパイル検証）。
修正前のコンパイラでは全て失敗する回帰テストである。

| テスト | 検証内容 |
|---|---|
| `basic/precedence_mask` | `(a & 256) == 0` の括弧保持（alwaysブロック経路と関数経路の両方） |
| `basic/cast_truncate` | 式の途中の縮小キャスト `((a + 300) as utiny) + 1000` |
| `control/signed_shift` | 負数の算術右シフト `-8 >> 2 == -2` |
| `advanced/enum_explicit` | 明示タグ値100を持つenumのビット幅 |
| `advanced/reg_init` | レジスタ宣言初期値が電源投入時に見えること |
| `memory/array_port` | 配列型ポートの次元保持（lint通過） |

実行結果: `tests/unified_test_runner.sh -b sv` — **88件中 82 PASS / 0 FAIL**
（6 SKIPは従来から `.expect` が無いもの）。

---

## 4. C++ベストプラクティス調査と実施した修正

### 4.1 現状の全体像

- `src/` 配下: `.hpp` 127 / `.cpp` 116。**57ヘッダーが実装持ちのヘッダーオンリー**
- `CMAKE_UNITY_BUILD ON`（バッチ16）のため、ヘッダー実装のODR/インクルード漏れが顕在化しにくい
- includeガード（`#pragma once`）・スマートポインタ利用・仮想デストラクタは概ね健全
- 警告は `-Wall -Wextra -Wpedantic` 有効（`-Werror` は return-type のみ）

### 4.2 実施した修正

1. **`src/mir/nodes.hpp` の実装分離** → 新規 `src/mir/nodes.cpp`
   - `BasicBlock::update_successors` / `MirFunction::build_cfg` /
     `MirEnum::max_payload_size` / `MirProgram::find_function{,_qualified}` /
     `find_struct` / `find_vtable` の実装を移動。
   - MIRノードはほぼ全コンパイラフェーズから include される中心ヘッダーであり、
     アルゴリズム変更のたびに広範囲が再コンパイルされる状態を解消。
2. **`src/codegen/buffered_codegen.hpp` の実装分離** → 新規 `src/codegen/buffered_codegen.cpp`
   - `BufferedCodeGenerator` / `TwoPhaseCodeGenerator` / `ScopedCodeSection` の
     非テンプレートメンバを移動（テンプレートの `append_formatted` のみヘッダー残置）。
   - 従来ヘッダーは `std::cerr` 使用にもかかわらず `<iostream>` を include しておらず
     unity buildで隠蔽されていた問題も解消（`<iostream>` は .cpp 側でinclude）。
3. **`src/codegen/llvm/native/target.cpp` の `auto& builder = *new llvm::IRBuilder<>(ctx); ... delete &builder;`**
   をスタック変数に修正（例外時リークと紛らわしい所有権表現を排除）。
4. `SVPort` 集成体初期化の `-Wmissing-field-initializers` 警告を解消。
5. 新規 .cpp を `CMakeLists.txt` の `CM_SOURCES` に登録。フルビルド・全svテスト・JITスモークで回帰なしを確認。

### 4.3 残る提案（優先度順）

1. **svバックエンドの構造改革（最重要・中期）**
   `codegen.cpp`（約3,500行）は「一度SVテキストを出力してから文字列操作で
   温度修正する」設計（一時変数インライン展開、else-if正規化、ternary変換、
   `always_comb`/`always_latch` 推論が全て文字列 `find`/`replace`）。
   今回の優先順位バグはこの設計の必然的帰結である。
   **式ツリー（またはSV用の小さなAST）を構築してから一括でプリティプリントする**
   構造に置き換えることを強く推奨。ラッチ推論も「行内の `if (` 数を数える」
   テキストヒューリスティックではなく、MIRの代入完全性解析に基づくべき。
2. **ヘッダーオンリー実装の計画的分離（低リスクから順に）**
   `src/codegen/llvm/monitoring/*.hpp` クラスタ（8ファイル前後）、
   `src/frontend/parser/generic_inference.hpp`、`src/frontend/types/generic_context.hpp` 等。
   unity buildを維持するとしても、ヘッダー実装はインクリメンタルビルドと
   依存関係の見通しを悪化させる。
3. **重複・死蔵コードの整理**: `src/lint/naming.hpp` はどこからも include されておらず、
   同等ロジックが `TypeChecker`（`frontend/types/checking/utils.cpp`）に重複実装されている。
   どちらかに一本化して削除する。
4. **巨大TUの分割**: `codegen/llvm/core/mir_to_llvm.cpp`（約5,000行）、`main.cpp`（約2,000行、
   コマンドラインエントリとしては過大）、`mir/lowering/lowering.cpp` 等。
5. **グローバル可変状態の削減**: `g_module_resolver`（`module/resolver.hpp`）、
   `g_debug_mode`/`g_lang`/`g_debug_level`（`common/debug.hpp` の inline 可変グローバル）。
   コンテキストオブジェクトへの集約を推奨。
6. **エラー処理方針の統一**: 例外（throw 65箇所）と `optional`/`Result` が混在。
   コンパイラ本体は診断API + `Result` へ寄せる方針を明文化する。

---

## 5. svバックエンドの残課題（未修正・提案）

1. **string型の関数引数/戻り値が24bit固定**（`mapType`/`getBitWidth` が `String → logic [23:0]`）。
   const グローバル文字列は実長で出力されるが、関数境界を越えると3文字前提になる。
   型に長さ情報を持たせるか、コンパイルエラーにするべき。
2. **符号付きサイズ付きリテラルの拡幅**（`4'shF` → `8'shF` は符号拡張されず +15 になる）と、
   `target_width` 既知時に `'sd` が `'d` に落ちる問題。符号付き定数の出力経路の見直しが必要。
3. **関数ローカル変数のモジュールスコープ昇格**: 生成SVでは Cm の関数ローカルが
   モジュールレベル reg に昇格され、クロックブロック内でブロッキング代入される。
   複数プロセスが同名テンポラリを共有した場合に多重ドライバとなるリスクがある。
   プロセスごとの名前空間分離（プレフィックス付与）を推奨。
4. **配列（BRAM）の初期値**: 2.2の修正はスカラのみ。配列初期値は `initial` ブロックまたは
   `$readmemh` での対応を検討。
5. **出力ポートのデフォルト値**（`#[output] bool x = false`）はポート宣言に反映されない。
6. **lint設定の見直し**: 生成SVが `WIDTHTRUNC`/`WIDTHEXPAND`/`UNDRIVEN` を一括 lint_off して
   おり、幅不一致系の不具合を自ら隠している。2.3の明示キャスト出力が入ったため、
   段階的に lint_off を外すことを推奨。
7. **CmCPU側ビルドフローへのシミュレーション組み込み**: `builder.sh` は verilator
   `--lint-only` のみで生成テストベンチを一度も実行していない。2.2の修正により
   シミュレーションが可能になったため、`iverilog + vvp`（または verilator --binary）の
   スモークをMakefileターゲットに追加することを推奨。

---

## 6. 変更ファイル一覧

**コンパイラ修正**
- `src/codegen/sv/codegen.cpp` — 優先順位括弧・キャスト・算術シフト・enum幅・
  レジスタ初期値・配列ポート・`[]`深さ対応・型解決ヘルパー
- `src/codegen/sv/codegen.hpp` — `SVPort::array_suffix` 追加
- `src/mir/nodes.cpp`（新規）/ `src/mir/nodes.hpp` — 実装分離
- `src/codegen/buffered_codegen.cpp`（新規）/ `src/codegen/buffered_codegen.hpp` — 実装分離
- `src/codegen/llvm/native/target.cpp` — IRBuilderのスタック確保化
- `CMakeLists.txt` — 新規cpp登録

**テスト追加**
- `tests/sv/basic/precedence_mask.{cm,expect}`
- `tests/sv/basic/cast_truncate.{cm,expect}`
- `tests/sv/control/signed_shift.{cm,expect}`
- `tests/sv/advanced/enum_explicit.{cm,expect}`
- `tests/sv/advanced/reg_init.{cm,expect}`
- `tests/sv/memory/array_port.{cm,expect}`

**CmCPU側（本リポジトリ外・参考）**
- `build/hdmi/hdmi_text.sv` / `build/hdmi/hdmi_colorbar.sv` を修正版コンパイラで再生成し、
  TMDS分岐の括弧が正しく出力されること・verilator lint 通過を確認済み。
  **実機書き込み前に再合成が必要**（従来ビットストリームはTMDS DCバランスが崩れている）。
