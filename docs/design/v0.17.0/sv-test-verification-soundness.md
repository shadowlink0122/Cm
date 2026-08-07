# R15: SVテスト検証の健全性（`//! test:`期待値が非検証・assertのx楽観性）

**ステータス:** 未修正（第7ラウンド検出）
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

影響: `//! test:`ディレクティブを使う`tests/sv/{basic,advanced,control}`の大半。`cm test`単体の「PASS」は無意味で、実際の検証は外部`unified_test_runner.sh`の`.expect`突合に依存している。アサーションが1本も無いため、x楽観性以前に確定値でも見逃す。第6ラウンドR7の「不正spec→常時PASSの空テストベンチ」はこの経路では特殊ケースでなく全件がそう。

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
