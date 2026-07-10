---
title: SVバックエンド - 状態初期化とシミュレーション
parent: Tutorials
nav_order: 16
---

[English](../../../en/compiler/sv/state-sim.html)

# SVバックエンド - 状態初期化とシミュレーション

[SystemVerilogバックエンド](index.html) の詳細ページです。レジスタ初期値、initialブロック、テストベンチ自動生成、テストの実行方法を解説します。

---

## レジスタ宣言初期値

モジュールレベル変数の宣言初期値は、SVのレジスタ宣言初期値として出力されます:

```cm
uint state = 0;
uint counter = 42;
```
```systemverilog
logic [31:0] state = 32'd0;
logic [31:0] counter = 32'd42;
```

効果:
- **FPGA合成**: レジスタの電源投入時初期値として扱われます（Gowin/Xilinx/Intel対応）
- **シミュレーション**: `X` 伝播を防ぎ、iverilog / Verilator でそのまま実行できます

> **旧バージョンの注意:** 以前は初期値が出力されず、シミュレーションで全レジスタが
> `X` のままFSMが起動しない問題がありました（v0.15.1 2026-07-04修正）。
> 回帰テスト: `tests/sv/advanced/reg_init`。

> **制限:** 配列（BRAM）の初期値は未対応です。

---

## initialブロック

シミュレーション用の初期化ブロックを記述できます:

```cm
initial {
    counter = 0;
}
```
```systemverilog
initial begin
    counter = 0;
end
```

> **対応している文:** 代入文・変数宣言・if文。
> 表示系タスク（`$display`等）は未対応です。

---

## テストベンチ自動生成

`//! test:` ディレクティブを書くと、テストベンチ（`*_tb.sv`）が自動生成されます。

### 組み合わせ回路のテスト

```cm
//! platform: sv
//! test: a=255, b=15 -> band=15, bor=255

#[input]  int a = 0;
#[input]  int b = 0;
#[output] int band = 0;
#[output] int bor = 0;

void bitops() {
    band = a & b;
    bor = a | b;
}
```

各 `//! test:` 行が1つのテストケースになり、入力を設定して出力を検証します。

### 順序回路のテスト（cycles指定）

```cm
//! test: cycles=1 -> sum=6
```

`cycles=N` でNクロック進めてから出力を検証します。
クロック（`clk`）は自動生成（10ns周期）、リセット（`rst`/`rst_n`）があれば
リセットシーケンスも自動挿入されます。

> **注意:** 複数の `//! test:` ケースは同一シミュレーション内で連続実行されます。
> レジスタ状態はケース間でリセットされません。

---

## テストの実行

```bash
# SVテストのみ実行
make test-sv        # または make tsv

# SVテスト（並列実行）
make test-sv-parallel   # または make tsvp

# 全テスト実行（SVを含む）
make test
```

テストランナーの検証は3段階です:

1. **コンパイル**: `cm compile --target=sv` が成功すること
2. **リント**: `verilator --lint-only`（fallback: `iverilog -g2012`）が通ること — `.expect` に `COMPILE_OK`
3. **シミュレーション**: `iverilog + vvp` を実行し `TEST k: name=val` 行を `.expect` と比較 — `.expect` に `SIM_OK` + `TEST` 行

エラーテストは `foo.cm` + `foo.error`（期待するエラーの説明）を置くと、
コンパイルが**失敗すること**を検証します。

### x86_64デバッグ（macOS開発者向け）

```bash
make build-x86    # x86_64用コンパイラをビルド
make test-x86     # x86_64でテスト実行（Rosetta経由）
make debug-x86 FILE=tests/sv/basic/adder.cm
```

---

← [データ構造](data.html) | [意味論保証](semantics.html) →

## アサーション（std::debug::assert）

`std::debug::assert` は SVターゲットでは**即時アサーション**として出力されます。
シミュレーションで検証され、合成ツールでは無視されます:

```cm
import std::debug::assert;

async void check(posedge clk) {
    assert(value < 100, "value out of range");
    out = value;
}
```

```systemverilog
always @(posedge clk) begin
    assert (value < 100) else $error("assertion failed: value out of range");
    out <= value;
end
```

- 実行系バックエンド（JIT/native/WASM/JS）では標準ライブラリの実装が実行され、
  違反時に `assertion failed: <msg>` を出力して `exit(1)` します
- SVのみ、ハードウェアに `exit` が存在しないため即時アサーションへ変換されます
  （標準ライブラリの関数定義自体はSV出力されません）

回帰テスト: `tests/sv/simulation/assert_immediate`

## #[test] テスト関数（v0.16.0）

`//! test:` の単発ベクタでは書けない**系列刺激**を、Cmの関数として記述できます。
`#[test]` を付けた**直後の関数**がテスト対象になります（`#ifdef`/`#end` で
囲う必要はありません）。SVでは各テスト関数がテストベンチのinitialブロックに
宣言順で変換されます:

```cm
import std::debug::assert;
import std::io::println;

#[test]
void latch_sequence() {
    din = 5;
    step(1);                      // 1クロック進める
    assert(dout == 5, "first value latched");
    din = 7;
    step(2);
    assert(dout == 7, "second value latched");
    println("sequence done");
}
```

`#[test]` 関数は**テストモードでのみコンパイル**されます（Rustの
`#[cfg(test)]` + `#[test]` に相当）。通常の `cm compile` / `cm run` では
型チェック前に除去されるため、合成用ビルドへの影響はゼロです。

テストの実行は `cm test` が `//! platform:` ディレクティブを見て
バックエンドを自動選択します:

```bash
cm test design.cm     # //! platform: sv → SV+TB生成 + iverilog/vvp実行
cm test logic.cm      # platform指定なし → 各#[test]関数をJITで直接実行
```

- SVプラットフォーム: 全 `#[test]` 関数を**宣言順に同一initialブロックで
  逐次実行**します（DUT状態を共有）
- native/JIT: 関数ごとに**独立実行**（状態隔離）し、完了ごとに
  `[PASS] <関数名>` を表示します。`step()` はクロック概念がないため使えません
  （`//! platform: sv` を指定してください）
- 既存フローに組み込む場合は `cm compile --target=sv --test ...` で
  `#[test]` 関数を含めてSV+TBだけ生成できます

生成されるTB（抜粋）:

```systemverilog
din = 5;
repeat (1) @(posedge clk);
#1; // NBA確定待ち
if (!((dout == 5))) begin
    $display("FAIL: first value latched");
    $fatal(1);
end else begin
    $display("PASS: first value latched");
end
```

- **`step(n)`**: nクロック待機（`#[test]` 関数・SVプラットフォーム専用の組み込み）
- **`assert(cond, msg)`**: 成立でPASS表示、不成立でFAIL表示+`$fatal`
  （シミュレーションが非0終了するためテストランナーが失敗を検出）
- **`println("...")`**: `$display`（文字列リテラルのみ）
- 代入はブロッキング代入としてDUT入力を駆動します
- クロックポートが `clk` 以外の名前（`pixel_clk` 等）でも、プロセスのクロックに使われている入力ポートを自動検出します
- `#[test]` 関数がある場合、`//! test:` ベクタより優先されます
- `#[test]` 関数は引数なし・戻り値 `void` である必要があります
- テストランナーでは `.expect` に `SIM_OK`（完走期待）または
  `SIM_FAIL_EXPECTED`（assert不成立を期待する失敗系テスト）を書きます


### 実回路のテストパターン（#ifdef TEST）

実機ではOSC/PLLから供給されるクロックを、テスト時だけ外部注入に
切り替えるには `#ifdef` を組み合わせます。テストモード
（`cm test` / `--test`）では定義 `TEST` が自動で追加されます:

```cm
#ifdef TEST
#[input] posedge clk;            // テスト: クロック注入
const uint DEBOUNCE_COUNT = 2;   // タイミング短縮
#end
#ifndef TEST
extern struct OSC { ... }        // 実機: 内蔵オシレータ
bool clk = false;
OSC osc_inst;
const uint DEBOUNCE_COUNT = 525000;
#end
```

```bash
cm test design.cm                              # テスト（TESTが自動定義）
cm compile --target=sv design.cm -o design.sv  # 合成用（テストコードは除去）
```

`-D SIM` など任意の名前のユーザー定義も従来通り併用できます。


---

<!-- nav -->
← 前: [SVバックエンド - 実機I/O（ピン制約・トライステート・CDC）](board-io.html) ｜ [目次](index.html) ｜ 次: [SVバックエンド - 意味論保証](semantics.html) →
