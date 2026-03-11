---
title: SystemVerilogバックエンド
parent: Tutorials
nav_order: 11
---

[English](../../en/compiler/sv.html)

# コンパイラ編 - SystemVerilogバックエンド

**難易度:** 🟡 中級  
**所要時間:** 45分

CmからSystemVerilog (SV) を生成し、FPGA上でハードウェアとして動作させることができます。Tang Console（Gowin）、Xilinx、Intel等のFPGAに対応しています。

---

## 目次

1. [最初の回路](#最初の回路)
2. [プラットフォームディレクティブ](#プラットフォームディレクティブ)
3. [型システム](#型システム)
4. [ポート宣言](#ポート宣言)
5. [ロジックブロック](#ロジックブロック)
6. [演算子](#演算子)
7. [定数リテラルとビット幅](#定数リテラルとビット幅)
8. [定数とlocalparam](#定数とlocalparam)
9. [制御構文](#制御構文)
10. [連接と複製](#連接と複製)
11. [列挙型 (FSM)](#列挙型-fsm)
12. [SV属性](#sv属性)
13. [暗黙的変換](#暗黙的変換)
14. [コンパイルと検証](#コンパイルと検証)
15. [全体例](#全体例)
16. [トークンリファレンス](#トークンリファレンス)

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
    logic [31:0] counter;

    always_ff @(posedge clk) begin
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

---

## プラットフォームディレクティブ

SVバックエンドを使用するには、ファイル先頭に **必ず** 記述します:

```cm
//! platform: sv
```

有効になる機能:
- SV固有キーワード (`posedge`, `negedge`, `wire`, `reg`, `always`, `assign`)
- 非合成型のバリデーション (`float`, `string`, ポインタ → コンパイルエラー)
- 暗黙的SV変換 (代入方式、リテラルビット幅付与 等)

---

## 型システム

### 基本型

| Cm型 | SV出力 | ビット幅 | 用途 |
|------|--------|---------|------|
| `bool` | `logic` | 1 | フラグ、制御信号 |
| `utiny` | `logic [7:0]` | 8 | 小さなカウンタ、状態 |
| `ushort` | `logic [15:0]` | 16 | アドレス |
| `uint` | `logic [31:0]` | 32 | カウンタ、データ |
| `ulong` | `logic [63:0]` | 64 | タイムスタンプ |
| `tiny` | `logic signed [7:0]` | 8 | 符号付き小数値 |
| `short` | `logic signed [15:0]` | 16 | 符号付き中間値 |
| `int` | `logic signed [31:0]` | 32 | 符号付きデータ |
| `long` | `logic signed [63:0]` | 64 | 符号付き大規模データ |

### SV固有型

| Cm型 | 用途 | SV出力 |
|------|------|--------|
| `posedge` | クロック立ち上がりエッジ信号 | `logic` (1-bit) |
| `negedge` | クロック/リセット立ち下がりエッジ信号 | `logic` (1-bit) |
| `wire<T>` | ワイヤ修飾（組み合わせ出力） | `T`のマッピングに準拠 |
| `reg<T>` | レジスタ修飾（順序回路出力） | `T`のマッピングに準拠 |

### カスタムビット幅

```cm
#[output] bit[4] nibble;      // → output logic [3:0] nibble
#[output] bit[12] address;    // → output logic [11:0] address
bit[26] counter;              // → logic [25:0] counter
```

### 非合成型 (コンパイルエラー)

`float`, `double`, `string`, `cstring`, `*T` (ポインタ), `&T` (参照) はSVバックエンドで **コンパイルエラー** になります。

---

## ポート宣言

```cm
// 入力ポート
#[input]  posedge clk;              // → input logic clk
#[input]  bool rst = false;         // → input logic rst
#[input]  utiny data_in;            // → input logic [7:0] data_in

// 出力ポート
#[output] bool led = false;         // → output logic led
#[output] utiny led_array = 0xFF;   // → output logic [7:0] led_array

// 双方向ポート
#[inout]  ushort bus;               // → inout logic [15:0] bus

// パラメータ（外部から上書き可能）
#[sv::param] uint WIDTH = 8;        // → parameter WIDTH = 32'd8;
```

---

## ロジックブロック

### 順序回路 (always_ff)

#### パターンA: `always` + エッジパラメータ （推奨）

```cm
always void counter_tick(posedge clk) {
    count = count + 1;
}
// → always_ff @(posedge clk) begin
//        count <= count + 32'd1;
//    end
```

#### パターンB: 非同期リセット（複数エッジ）

```cm
always void process(posedge clk, negedge rst_n) {
    if (rst_n == false) {
        count = 0;
    } else {
        count = count + 1;
    }
}
// → always_ff @(posedge clk or negedge rst_n) begin ...
```

#### パターンC: `void f(posedge clk)` （後方互換）

```cm
void blink(posedge clk) {
    led = !led;
}
// → always_ff @(posedge clk) begin led <= ~led; end
```

#### パターンD: `async func` （後方互換）

```cm
async func tick() {
    counter = counter + 1;
}
// → always_ff @(posedge clk) begin counter <= counter + 32'd1; end
```

> **注意:** `async func` は暗黙的に `clk` 変数を参照します。
> `clk` が未宣言の場合、自動的に `input logic clk` が追加されます。

### 組み合わせ回路 (always_comb)

エッジパラメータなしの関数:

```cm
always void decode() {
    out = 0;
    if (sel) { out = a; }
    else { out = b; }
}
// → always_comb begin ... end
```

後方互換: `void f()` / `func f()` も `always_comb` に変換されます。

### 代入の自動変換ルール

| ブロック種別 | Cmでの記述 | SV出力 |
|------------|----------|--------|
| `always_ff` (順序回路) | `x = expr;` | `x <= expr;` (ノンブロッキング) |
| `always_comb` (組み合わせ) | `x = expr;` | `x = expr;` (ブロッキング) |

Cmでは常に `=` で記述し、コンパイラが文脈に応じて適切な代入方式を選択します。

---

## 演算子

### 算術・ビット演算

| Cm | SV | 備考 |
|----|----|------|
| `+` `-` `*` `/` `%` | 同じ | 算術 |
| `&` `\|` `^` `~` | 同じ | ビット演算 |
| `<<` `>>` | 同じ | シフト |
| `==` `!=` `<` `<=` `>` `>=` | 同じ | 比較 |
| `&&` `\|\|` | 同じ | 論理演算 |
| `!x` | `~x` | **暗黙変換**: 論理否定→ビット反転に統合 |

> **重要:** Cmの `!` (論理否定) はSVでは `~` (ビット反転) にマッピングされます。多ビット信号に対して安全な `~` に統一しています。

---

## 定数リテラルとビット幅

リテラルは文脈の型に基づき **自動的にビット幅付き** に変換されます:

| Cmリテラル | 文脈の型 | SV出力 |
|-----------|---------|--------|
| `true` | `bool` | `1'b1` |
| `false` | `bool` | `1'b0` |
| `42` | `uint` (32-bit) | `32'd42` |
| `42` | `utiny` (8-bit) | `8'd42` |
| `-5` | `int` (符号付き32-bit) | `-32'sd5` |

### SVスタイルリテラル

```cm
utiny mask = 8'b10101010;     // → 8'b10101010
ushort addr = 16'hFF00;       // → 16'hFF00
```

### 数値区切り文字

```cm
const uint CLK_FREQ = 50_000_000;   // → localparam CLK_FREQ = 32'd50000000;
```

---

## 定数とlocalparam

### `const` → `localparam`

```cm
const uint CLK_FREQ = 27_000_000;
const uint CNT_MAX = CLK_FREQ / 2 - 1;
```
```systemverilog
localparam CLK_FREQ = 32'd27000000;
localparam CNT_MAX = CLK_FREQ / 2 - 32'd1;
```

### `#[sv::param]` → `parameter`

```cm
#[sv::param] const uint WIDTH = 8;
// → parameter WIDTH = 32'd8;
```

---

## 制御構文

### if / else if / else

```cm
if (rst) {
    counter = 0;
} else if (enable) {
    counter = counter + 1;
} else {
    // idle
}
```
```systemverilog
if (rst) begin
    counter <= 32'd0;
end else if (enable) begin
    counter <= counter + 32'd1;
end else begin
end
```

### switch → case

```cm
switch (state) {
    case 0: { next_state = 1; }
    case 1: { next_state = 2; }
    default: { next_state = 0; }
}
```
```systemverilog
case (state)
    32'd0: begin next_state <= 32'd1; end
    32'd1: begin next_state <= 32'd2; end
    default: begin next_state <= 32'd0; end
endcase
```

---

## 連接と複製

```cm
result = {a, b};         // → {a, b}
replicated = {3{a}};     // → {3{a}}
```

ビルトイン関数（`{...}` がブロックと曖昧な場合）:

```cm
result = concat(a, b);       // → {a, b}
wide = replicate(nibble, 3); // → {3{nibble}}
```

---

## 列挙型 (FSM)

Cmの `enum` はSVの `typedef enum logic` に変換されます。ビット幅はバリアント数から自動計算:

```cm
enum State { IDLE, RUN, DONE, ERROR }
```
```systemverilog
typedef enum logic [1:0] {
    IDLE = 2'd0, RUN = 2'd1, DONE = 2'd2, ERROR = 2'd3
} State;
```

---

## SV属性

| 属性 | 効果 | 例 |
|------|------|----|
| `#[input]` | 入力ポート | `#[input] posedge clk;` |
| `#[output]` | 出力ポート | `#[output] utiny led = 0xFF;` |
| `#[inout]` | 双方向ポート | `#[inout] ushort bus;` |
| `#[sv::param]` | `parameter`宣言 | `#[sv::param] uint WIDTH = 8;` |
| `#[sv::bram]` | `(* ram_style = "block" *)` | `#[sv::bram] utiny mem[1024];` |
| `#[sv::lutram]` | `(* ram_style = "distributed" *)` | `#[sv::lutram] utiny lut[16];` |
| `#[sv::clock_domain("name")]` | `async func`のクロック指定 | `#[sv::clock_domain("fast")]` |
| `#[sv::pipeline]` | パイプラインヒント | |
| `#[sv::share]` | リソース共有ヒント | |
| `#[sv::pin("XX")]` | ピン割り当て (XDC/CST) | `#[sv::pin("H11")]` |
| `#[sv::iostandard("YY")]` | IO電圧規格 | `#[sv::iostandard("LVCMOS33")]` |

---

## 暗黙的変換

SVバックエンドは、正しいSVコードを自動生成するために多数の暗黙的変換を行います。

### 代入方式の自動決定

| 文脈 | Cm | SV |
|------|----|----|
| `always_ff` | `x = expr;` | `x <= expr;` |
| `always_comb` | `x = expr;` | `x = expr;` |

### 論理否定の変換

| Cm | SV | 理由 |
|----|----|----|
| `!flag` | `~flag` | 多ビット信号に安全な `~` に統一 |

### リテラルのビット幅付与

| Cm | 代入先の型 | SV |
|----|-----------|-----|
| `counter = 0;` | `uint` | `counter <= 32'd0;` |
| `flag = true;` | `bool` | `flag <= 1'b1;` |

### クロック/リセットの自動追加

| 条件 | 動作 |
|------|------|
| `async func` 存在 & `clk` 未宣言 | `input logic clk` を自動追加 |
| `async func` 存在 & `rst` 未宣言 | `input logic rst` を自動追加 |

### MIR一時変数のインライン展開

MIRの `_tXXXX` 一時変数は元の式にインライン展開されます:

```
MIR:  _t1000 = counter + 1; result = _t1000;
SV:   result <= counter + 32'd1;
```

### `self.` プレフィックスの除去

`self.counter` → `counter` (SVに `self` は不要)

### `else if` の正規化

ネストした `else { if ... }` パターンを `else if` にフラット化。

### 冗長な三項演算子の除去

`cond ? x : x` を単純な `x` に最適化。

---

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

const uint CLK_FREQ = 27_000_000;
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
| `KwAlways` | `always` | ロジックブロック修飾子 |
| `KwAssign` | `assign` | 連続代入文 |
| `KwInitial` | `initial` | シミュレーション初期化 |
| `KwBit` | `bit` | 任意ビット幅型 |

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

**前の章:** [WASMバックエンド](wasm.html)  
**次の章:** [フォーマッタ](formatter.html)

---

**最終更新:** 2026-03-11
