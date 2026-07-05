---
title: SystemVerilogバックエンド
parent: Tutorials
nav_order: 11
has_children: false
---

[English](../../../en/compiler/sv/index.html)

# コンパイラ編 - SystemVerilogバックエンド

**難易度:** 🟡 中級  
**所要時間:** 45分（全ページ通読の場合 2時間）

CmからSystemVerilog (SV) を生成し、FPGA上でハードウェアとして動作させることができます。Tang Console（Gowin）、Xilinx、Intel等のFPGAに対応しています。

---

## 詳細ページ一覧

SVバックエンドの詳細はトピック別のページに分かれています:

| ページ | 内容 |
|--------|------|
| [型とポート](types.html) | 型マッピング、ポート宣言、配列ポート、リテラル、localparam、SV属性 |
| [プロセスと代入](processes.html) | always_ff/comb/latch、代入の自動変換、暗黙的変換 |
| [制御構文とループ](control-flow.html) | if/case、whileループ再構成、break、演算子と優先順位保証 |
| [データ構造](data.html) | 連接・複製、enum FSM、配列とBRAM、文字列 |
| [状態初期化とシミュレーション](state-sim.html) | レジスタ初期値、initialブロック、テストベンチ自動生成、アサーション、テスト実行 |
| [メモリ初期化](memory.html) | 配列初期値、#[sv::memfile]/$readmemh、--emit-memfile |
| [モジュール階層](hierarchy.html) | //! sv: hierarchy によるサブモジュールのインスタンス化 |
| [意味論保証](semantics.html) | Cm↔SVの意味論対応の保証事項まとめ（キャスト・符号付き演算等） |

---

## 最初の回路

```cm
//! platform: sv

#[input]  posedge clk;
#[input]  bool rst = false;
#[output] bool led = false;

uint counter = 0;

void blink(posedge clk) {
    if (rst) {
        counter = 0;
        led = false;
    } else {
        if (counter == 49999999) {
            counter = 0;
            led = !led;
        } else {
            counter = counter + 1;
        }
    }
}
```

コンパイル:
```bash
cm compile --target=sv blink.cm -o blink.sv
```

生成されるSV:
```systemverilog
`timescale 1ns / 1ps

module blink (
    input logic clk,
    input logic rst,
    output logic led
);
    logic [31:0] counter = 32'd0;

    always @(posedge clk) begin
        if (rst) begin
            counter <= 32'd0;
            led <= 1'b0;
        end else begin
            if (counter == 32'd49999999) begin
                counter <= 32'd0;
                led <= ~led;
            end else begin
                counter <= counter + 32'd1;
            end
        end
    end
endmodule
```

> **ポイント:** Cmの `=` は自動的にSVの `<=` (ノンブロッキング代入) に変換されます。
> `!led` もSVの `~led` (ビット反転) に変換されます。
> 変数の宣言初期値（`uint counter = 0;`）は電源投入時初期値として出力されます。

---

## プラットフォームディレクティブ

SVバックエンドを使用するには、ファイル先頭に **必ず** 記述します:

```cm
//! platform: sv
```

有効になる機能:
- SV固有キーワード (`posedge`, `negedge`, `wire`, `reg`, `always`, `assign`)
- 非合成型のバリデーション (ポインタ → コンパイルエラー)
- 暗黙的SV変換 (代入方式、リテラルビット幅付与 等)

---

## SVターゲットのコンパイルオプション

| オプション | 内容 |
|---|---|
| `--emit-memfile` | 配列リテラル初期値を `.hex` ファイルとして書き出す |
| `--sv-strict-lint` | lint_off抑止を出力しない（幅警告を可視化して潰す作業用） |

## コンパイルと検証

```bash
# SV コード生成
cm compile --target=sv blink.cm -o blink.sv

# Verilatorで構文チェック
verilator --sv --lint-only blink.sv

# Icarus Verilogでシミュレーション
iverilog -g2012 -o sim blink.sv blink_tb.sv
vvp sim

# FPGA ビルド (Gowin EDA)
gw_sh gowin_build.tcl
```

### ターゲットFPGA

| ボード | チップ | ツール |
|--------|--------|--------|
| Tang Console 138K | Gowin GW5AST | Gowin EDA |
| Tang Nano 9K | Gowin GW1NR-9 | Gowin EDA |
| Arty A7 | Xilinx Artix-7 | Vivado |
| DE10-Lite | Intel MAX 10 | Quartus |

---

## 全体例

```cm
//! platform: sv

#[input]  posedge clk;
#[input]  negedge rst_n;
#[output] bool led = false;

const uint CLK_FREQ = 27000000;
const uint CNT_MAX = CLK_FREQ / 2 - 1;

uint counter = 0;

always void blink(posedge clk, negedge rst_n) {
    if (rst_n == false) {
        counter = 0;
        led = false;
    } else {
        if (counter == CNT_MAX) {
            counter = 0;
            led = !led;
        } else {
            counter = counter + 1;
        }
    }
}
```

---

## トークンリファレンス

### SV固有トークン

| トークン | キーワード | 用途 |
|---------|---------|------|
| `KwPosedge` | `posedge` | 立ち上がりエッジ |
| `KwNegedge` | `negedge` | 立ち下がりエッジ |
| `KwWire` | `wire` | ワイヤ修飾型 |
| `KwReg` | `reg` | レジスタ修飾型 |
| `KwAlways` | `always` | ロジックブロック修飾子（自動判別） |
| `KwAlwaysFF` | `always_ff` | 順序回路（明示指定） |
| `KwAlwaysComb` | `always_comb` | 組み合わせ回路（明示指定） |
| `KwAlwaysLatch` | `always_latch` | ラッチ（明示指定） |
| `KwAssign` | `assign` | 連続代入文 |
| `KwInitial` | `initial` | シミュレーション初期化ブロック |
| `KwBit` | `bit` | 任意ビット幅型 `bit[N]` |

### 既存トークンのSVでの意味

| トークン | 通常(LLVM)の意味 | SVでの意味 |
|---------|-----------------|-----------|
| `async` | JS非同期関数 | `always_ff` (後方互換) |
| `func` | 関数宣言 | `always_comb` |
| `void` | 戻り値なし関数 | ブロック生成 |
| `=` | 変数代入 | ff: `<=`, comb: `=` |
| `!` | 論理否定 | `~` (ビット反転に統合) |
| `const` | 定数宣言 | `localparam` |
| `switch/case` | パターンマッチ | `case/endcase` |
| `enum` | 列挙型 | `typedef enum logic` |

---

**前の章:** [WASMバックエンド](../wasm/index.html)  
**次の章:** [型とポート](types.html)

---

**最終更新:** 2026-07-04

---

<!-- nav -->
← 前: [CmからJavaScriptへのコンパイル](../js/index.html) ｜ [目次](../index.html) ｜ 次: [SVバックエンド - 型とポート](types.html) →
