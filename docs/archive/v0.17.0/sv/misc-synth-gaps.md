# SV-N8: 小粒ギャップ集（$readmemb・type'(expr)・SV task・$time/final・native reg/2-state bit・#[sv::packed]）

**分類:** 混在（idiom・新機能・設計選択）
**優先度:** Low
**ステータス:** 完了（実装3件＋設計判断の記録3件）

個別に設計文書を割くには小粒だが、SV機能の網羅性のため記録する項目群。各項目は独立に実装/判断可能。

## 1. `$readmemb`（2進メモリファイル）

現状 `$readmemh`（16進）のみ生成（`#[sv::memfile]`は常にhex）。2進初期化ファイル向けに`$readmemb`と`--emit-memfile`のbin出力を追加する。属性で基数を選択（`#[sv::memfile("f.bin", radix: bin)]`）。優先度Low（hexで代替可能）。

## 2. 型キャスト `type'(expr)`（enum/struct）

現状は整数幅キャスト `N'(expr)` と `$signed`/`$unsigned` のみ（`codegen.cpp:955-957`）。enum型/packed struct型への明示キャスト `MyEnum'(x)`・`MyStruct'(bits)` は非対応。ビット→enum、生ビット→packed structの再解釈で有用。実装は式lowering のキャスト出力へ型名キャスト分岐を追加。SV-N6（packed union）と用途が重なる。

## 3. SV `task` の生成

現状、戻り値のないユーザー関数は`function`/`task`でなく `always_comb`/`always_ff` ブロック化される（`analyze.cpp:915-916`）。複数always内から呼べる**再利用可能な手続き**としてのSV `task`（`task automatic ...; ... endtask`）は生成手段がない。これは「各void関数＝1プロセス」という現行の設計選択の帰結。再利用手続きが必要なら、`function automatic`（戻り値あり・副作用なし）が既に使えるため優先度Low。taskを導入するかは設計判断（voidヘルパをtask化する属性など）。

## 4. `$time` / `final` ブロック（シミュレーション補助）

`$time`（現在シミュレーション時刻）と`final`ブロック（シミュレーション終了時実行）は非生成。テストベンチのデバッグ出力・サマリ表示で有用だが、`$display`＋`$finish`で概ね代替可能。優先度Low。

## 5. native `reg` 宣言・2-state `bit`型

現状、内部信号は全て`logic`（4-state）で出力され、`reg`宣言は生成されない（`logic`がSV-2012で`reg`を包含するため機能的に問題なし）。また`bit`型（2-state）も`logic`へ写像される。2-stateはシミュレーション速度で有利な場合があるが合成結果は同じ。native `reg`出力・2-state `bit`保持はいずれも優先度Low（現状で正しく動く）。

## 6. `#[sv::packed]` によるpacked/unpacked制御

現状は全structを一律`typedef struct packed`で出力する（TODO `analyze.cpp:863`）。unpacked structが要る場合（配列レイアウト・ツール制約）に選べるよう、`#[sv::packed]`/`#[sv::unpacked]`属性でpacked性を制御する。SV-N6（packed union）と併せてstruct/unionのレイアウト属性として整理するとよい。優先度Low。

## 実装方針（共通）

いずれもSVコード生成の局所的な追加で、既存経路への分岐で対応できる。優先度は他項目（SV-N1〜N7）より低い。実装時は各項目ごとに`tests/sv/`へ最小の生成物grep＋（該当すれば）iverilogシミュレーション値検証を追加する。

## テスト計画

各項目の実装時に個別テストを追加:
- `$readmemb`: bin memfile読み込みの値検証
- `type'(expr)`: ビット→enum/struct再解釈の値検証
- SV task: 導入する場合、複数always からの呼び出し
- `#[sv::packed]`: packed/unpacked両出力の生成物確認

## 実装記録

6項目を「実装3件」と「設計判断の記録3件」で処置し、本文書を完了した。

### 実装した項目

- **1. `$readmemb`**: `#[sv::memfile("f.bin", radix: bin)]` の基数指定を追加した。radix: bin 指定の配列は `initial $readmemb("f.bin", var);` で出力され、`--emit-memfile` は要素幅ぶんの2進数字を1行1要素で書き出す（既定は従来どおりhex/$readmemh）。属性引数はパーサの key:value 形式（"radix:bin"）で保持され、SVコード生成の `memfileRadixIsBin` が判定する。テスト: `tests/sv/memory/readmemb.cm`（COMPILE_OK＝verilator/iverilogビルド検証）・実機で$readmemb出力と2進memfile生成を確認。
- **2. 型名キャスト `type'(expr)`（struct分）**: packed structへの `as` キャスト（`raw as Pair`）を、暗黙代入（`p = raw;`）でなくSVの型名キャスト `p = Pair'(raw);` で出力するようにした（ビット→packed struct再解釈の明示）。SVコード生成のCast出力に、対象型が `struct_defs_` に在るstruct／`enum_typedef_names_`（typedef enumとして出力される値enum名の集合を`compile()`で構築）に在るenumのとき `Name'(...)` を出力する分岐を追加した。**enum分は前提待ち**: 値enumの `as Color` はMIR到達時点で対象型がintへ正規化されており（局所の型名が残らない）、分岐が発火しない。enum同一性の正規化遅延（[method-resolution-unification.md](../method-resolution-unification.md)の残課題）の実施後に自動で有効になる（分岐自体は実装済み）。テスト: `tests/regression/cases/sv/expr/struct_name_cast.cm`＋`SVCodegenTest.StructNameCast`。
- **6. `#[sv::unpacked]` によるpacked性制御**: 構造体属性のAST→HIR→MIR伝搬（`HirStruct::attributes`・`MirStruct::attributes` を新設）を通し、`#[sv::unpacked]` 付きstructは `typedef struct {`（unpacked）、既定は従来どおり `typedef struct packed {` で出力する（analyze.cpp:863のTODOを解消）。`sv::unpacked` を属性検証レジストリへ登録した（`sv::packed` は既登録・明示付与は既定と同じpacked出力）。テスト: `tests/regression/cases/sv/module/struct_packed_control.cm`＋`SVCodegenTest.StructPackedControl`（unpacked/packed両出力の生成物検証）。

### 設計判断として見送り・現状維持を記録した項目

- **3. SV `task` の生成: 見送り**。「各void関数＝1プロセス（always化）」はCmのSVバックエンドの中核設計であり、再利用可能な手続きは `function automatic`（戻り値あり関数）で既に表現できる。taskの導入は複数always からの共有手続き呼び出しという新しい実行モデルを持ち込み、合成対象サブセットの単純さを損なうため採用しない（必要になった場合はvoidヘルパをtask化する属性として再検討する）。
- **4. `$time` / `final` ブロック: 見送り**。テストベンチ生成はCm側で完結しており（`//! test:` 期待値・$display・$finish）、シミュレーション時刻表示・終了時サマリは既存機構で代替できる。Cm言語側に対応する概念が無く、構文追加に見合う需要が無い。
- **5. native `reg` 宣言・2-state `bit` 型: 現状維持**。SV-2012では `logic` が `reg` を包含し、生成SVは全信号 `logic` で機能的に正しい。2-state `bit` の保持はシミュレーション速度の最適化にすぎず合成結果は同一のため、型写像の複雑化（4-state/2-state混在の代入規則）に見合わない。

### 本文書の完了条件

実装3件はテスト付きで完了、見送り3件は判断理由を本記録に固定した。SVギャップ調査の新規実装項目でnative出力・新機能に関わるものはSV-N4〜N7（generate/インスタンス配列/packed union/SVA）のみが残る。
