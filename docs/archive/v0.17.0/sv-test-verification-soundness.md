# R15: SVテスト検証の健全性（`//! test:`期待値が非検証・assertのx楽観性）

**ステータス:** 修正済み（期待値アサート生成＋assertのx安全比較化）
**重大度:** Critical（`//! test:`非検証）/ High（x楽観性）

`cm test`でSVをシミュレーションした際の合否が信頼できない。期待値の突合が生成テストベンチに一切出力されない（Critical）か、不定値(x)でアサートが誤PASSする（High）。過去のHDMIタイトル末尾ゴミバグの回帰テストが2回連続で素通りした（[[sv-test-x-optimism]]）のと同じ構造的リスクの根。

## 症状（実測: cm 0.17.0、プローブ `.tmp/bughunt7/{sv,verify}/`）

### バグ1【Critical】`//! test: ... -> 期待値`が生成テストベンチで一度もアサートされない

```cm
//! platform: sv
//! test: a=3, b=5 -> result=999
#[input] int a = 0;
#[input] int b = 0;
#[output] int result = 0;
void add() { result = a + b; }
```
実測: `TEST 1: result=8` → `✓ SV test passed`（rc=0）。期待値999と実測8が不一致でも合格になる。正しい回路に誤った期待値を書いても、回路を意図的に壊しても常にPASSする。生成テストベンチ（`*_tb.sv`）を確認すると`$display("TEST 1: result=%0d", result);`のみで、`999`・比較演算・`$fatal`が一切生成されていない。

真因: `src/internal/codegen/sv/testbench.cpp:296-300`。コメントは「出力値の表示と検証」だが実装は期待値`val`をパースするだけで`$display`しか出さず、比較も`$fatal`も生成しない（`val`が未使用）。`cm test`（`src/cmd/cm/util.cpp`）はシミュレータの終了コード0＝PASSと判定するため、`$fatal`が絶対に出ないこの経路は常時PASS。

影響: `//! test:`ディレクティブを使う`tests/sv/{basic,advanced,control}`の大半。`cm test`単体の「PASS」は無意味で、実際の検証は外部`unified_test_runner.sh`の`.expect`突合に依存している。アサーションが1本も無いため、x楽観性以前に確定値でも見逃す。R7の「不正spec→常時PASSの空テストベンチ」はこの経路では特殊ケースでなく全件がそう。

### バグ2【High】`#[test]`+`assert`経路のx楽観性

`#[test]`関数＋明示`assert()`は確定値では健全（誤期待で正しくFAIL＋`$fatal`＋rc=1）。しかし対象信号がx（不定）だとアサートが誤PASSする。

```cm
//! platform: sv
import std::debug::assert;
#[input] posedge clk;
#[output] uint dout;        // 初期化子なし → x
async void ff(posedge clk) {}
#[test]
void tb() { step(1); assert(dout == 12345, "..."); }
```
実測: `PASS` → rc=0。`dout`はxで12345でないのに合格（`x == 12345`→x、`if (!(x))`→else＝PASS分岐）。初期化子付き出力（`= 0`）では確定値なので同assertは正しくFAILする。

## 修正方針

- **バグ1（最優先）**: testbench.cppで各`//! test:`の期待値ごとに`if (name !== expected) $fatal(1, "TEST n: name=%0d expected %0d", name, expected);`を生成する（xも`!==`で不一致扱いにできる`===`/`!==`を使い、x楽観性を同時に封じる）。合否をシミュレータ終了コードだけに委ねず、明示アサーションで判定する。
- **バグ2**: `assert()`のSV変換（testbench.cpp）で`==`比較を`x`検出込みに強化するか、アサート前に対象信号が既知値（非x）であることを検査する。`--sv-warn-nba`と同様のx混入警告を検討。

## テスト計画

- **negative check必須**（[[sv-test-x-optimism]]）: 誤期待値・故意に壊した回路・未駆動(x)信号のそれぞれで`cm test`が正しくrc=1でFAILすることを回帰で固定する。バグを一時再現させてFAILを確認してからPASS版を残す。
- `tests/sv/`へ`//! test:`期待値の突合がアサートされることのマトリクス回帰（確定値一致/不一致/x）。

## 実装記録

- **バグ1（`//! test:`非検証）**: `src/internal/codegen/sv/testbench.cpp`のテストシナリオ生成で、各期待値ごとに従来の`$display("TEST n: name=%0d", name)`に続けて`if (name !== (expected)) begin $display("FAIL: TEST n: name=%0d expected=…", name); $fatal(1); end`を生成するようにした。`!==`の4値比較でx/zも不一致（FAIL）になり、x楽観性をこの経路では構造的に封じている。既存の`$display`行は維持したため`unified_test_runner.sh`の`.expect`突合（`TEST k:`行）は不変で、tests/svスイート125件は無修正で全PASS（＝既存の期待値はすべて正しかったことの確認にもなった）。
- **バグ2（assertのx楽観性）**: `#[test]`関数の`assert(cond, msg)`変換を`if (!(cond))`から`if ((cond) !== 1'b1)`へ変更した。cond=xのとき旧形式は`if(x)`=偽で誤PASSしていたが、新形式は`x !== 1`=真でFAILする。condはCm側でbool（1-bit比較結果）が型保証されるため多ビット真値の意味変化はない。
- **併発修正（ディレクティブの行中誤認識）**: `//! test:`の検出が`line.find`のみで、通常コメント中の言及（`// //! test: の説明…`）までテストケースとして誤パースされていた。行頭（空白許容）のみ認識するよう修正した。既存の`tests/sv/simulation/directive_multi.cm`等はコメント中に言及があり、従来は空のファントムテストケース（無害な待ち時間）を生成していた。
- **cm test側の合否判定は変更不要**: シミュレータ終了コード0=PASSの判定（`src/cmd/cm/util.cpp`）は、$fatalが確実に生成されるようになったことで健全化した（vvpは$fatalで非0終了する）。
- **テスト**: `tests/cmtest/`へnegative check 4本（期待値一致=PASS・期待値不一致=FAIL・未駆動x出力=FAIL・assert対象x=FAIL）を追加し`tests/test_cm_test.sh`で終了コードとFAIL出力を検証（cm test E2E 8件PASS）。`tests/regression/sv_codegen_test.cpp`へアサート生成の固定（`!== (期待値)`/`$fatal`/`FAIL:`行、assertの`!== 1'b1`形と旧`if (!(`形の不在）を追加。tests/sv 125件・regression・cm-test全PASS。
- **チュートリアル**: `docs/tutorials/{ja,en}/compiler/sv/state-sim.md`の`//! test:`節へ期待値がアサートされること（FAIL表示・$fatal・xも不一致）を明記し、`#[test]`のTB抜粋とassert説明を`!== 1'b1`形へ更新。
- **残課題**: 組み込み宣言の`assert`をSVモジュール本体で使う即時アサーション経路（`assert (...) else $error`）は本修正の対象外（シミュレーション継続の警告用途で、テスト合否には従来から関与しない）。`--sv-warn-nba`同様のx混入警告の一般化は将来課題。
