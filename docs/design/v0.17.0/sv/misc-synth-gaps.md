# SV-N8: 小粒ギャップ集（$readmemb・type'(expr)・SV task・$time/final・native reg/2-state bit・#[sv::packed]）

**分類:** 混在（idiom・新機能・設計選択）
**優先度:** Low
**ステータス:** 未実装（v0.17.0 SVギャップ調査で検出）

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

いずれもSVコード生成の局所的な追加で、既存経路への分岐で対応できる。優先度は本ラウンドの他項目（SV-N1〜N7）より低い。実装時は各項目ごとに`tests/sv/`へ最小の生成物grep＋（該当すれば）iverilogシミュレーション値検証を追加する。

## テスト計画

各項目の実装時に個別テストを追加:
- `$readmemb`: bin memfile読み込みの値検証
- `type'(expr)`: ビット→enum/struct再解釈の値検証
- SV task: 導入する場合、複数always からの呼び出し
- `#[sv::packed]`: packed/unpacked両出力の生成物確認
