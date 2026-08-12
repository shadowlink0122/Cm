---
title: SVバックエンド - 型とポート
parent: Tutorials
nav_order: 12
---

[English](../../../en/compiler/sv/types.html)

# SVバックエンド - 型とポート

[SystemVerilogバックエンド](index.html) の詳細ページです。型マッピング、ポート宣言、リテラル、定数、SV属性を解説します。

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

ポインタ型 (`*T`) はSVバックエンドで **コンパイルエラー** (`error[SV002]`) になります。`float`/`double` も **コンパイルエラー** (`error[SV004]`、v0.16.0で警告からエラーに変更。合成にはIPコアが必要) になります。`string` は const 定数としてのみ実用的です（3文字超・非constは `error[SV005]`。[データ構造](data.html#文字列) 参照）。

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
```

### 配列型ポート

配列型のポートはアンパックド次元を保持して出力されます:

```cm
#[output] uint[4] data;   // → output logic [31:0] data [0:3]
```

> **注意:** 次元が保持されない旧バージョンでは `data[idx]` がビット選択として
> 解釈される問題がありました（v0.15.1で修正済み）。

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
| `0` | `int` との比較 | `32'sd0` |

### 符号付き定数は `'sd` で出力される

SVでは比較の片方が unsigned だと **比較全体が unsigned** になります。Cmは定数を型に従って符号付き（`'sd`）で出力するため、`s < 0` のような負数判定が正しく動作します:

```cm
int s;
if (s < 0) { ... }   // → if ((s < 32'sd0))  ※ 32'd0 だと常に偽になる
```

### SVスタイルリテラル

```cm
utiny mask = 8'b10101010;     // → 8'b10101010
ushort addr = 16'hFF00;       // → 16'hFF00
```

---

## 定数とlocalparam

```cm
const uint CLK_FREQ = 27_000_000;
const uint CNT_MAX = CLK_FREQ / 2 - 1;
```
```systemverilog
localparam logic [31:0] CLK_FREQ = 32'd27000000;
localparam logic [31:0] CNT_MAX = CLK_FREQ / 2 - 32'd1;
```

> **注意:** `const` は常に `localparam` にマッピングされます。
> モジュールパラメータは `#[sv::parameter] const` で宣言します
> （v0.16.0で対応。[モジュール階層](hierarchy.html)参照）。
> 通常の `const` は従来どおり `localparam` になります。

### 派生定数式とfloat式の`$rtoi`変換（v0.17.0）

const宣言の右辺には他のconstを参照する算術式を書けます（`H_TOTAL = H_ACTIVE + H_FP + H_SYNC + H_BP` 等）。整数宣言へのfloat式（`CLK_FREQ * 0.02` 等）は `$rtoi()` で明示変換して出力され、ゼロ方向切り捨て（Cmの縮小変換と同じ意味論）になります（従来は生のreal式のままでVerilatorのREALCVT警告が出ていました）:

```cm
const uint CLK_FREQ = 210000000 / 4;
const uint DEBOUNCE_LIMIT = CLK_FREQ * 0.02;
```

```systemverilog
localparam logic [31:0] CLK_FREQ = 32'd52500000;
localparam logic [31:0] DEBOUNCE_LIMIT = $rtoi((CLK_FREQ * 0.020000));
```

### 関数本体の定数float式の整数畳み込み（v0.17.0）

同じ「整数定数×浮動小数リテラル」の式を関数本体に書いた場合は、値が全て定数のfloat演算チェーンがコンパイル時に整数定数へ畳まれ、SV004にはなりません（従来はconst宣言でだけ展開でき、関数本体では拒否される非対称がありました）:

```cm
const uint LIMIT = 100;

async void t(posedge clk) {
    uint s = LIMIT * 0.5;   // → scaled <= 32'd50; に畳まれる
    scaled = s;
}
```

実行時値が絡むfloat式は従来どおり `error[SV004]` で拒否されます。double→uintの暗黙縮小警告が出るため、警告を消すには `(LIMIT * 0.5) as uint` と明示します。

---

## SV属性

| 属性 | 効果 | 例 |
|------|------|----|
| `#[input]` | 入力ポート | `#[input] posedge clk;` |
| `#[output]` | 出力ポート | `#[output] utiny led = 0xFF;` |
| `#[inout]` | 双方向ポート | `#[inout] ushort bus;` |
| `#[sv::bram]` | `(* ram_style = "block" *)` | `#[sv::bram] utiny mem[1024];` |
| `#[sv::lutram]` | `(* ram_style = "distributed" *)` | `#[sv::lutram] utiny lut[16];` |
| `#[sv::clock_domain("name")]` | `async func`のクロック指定 | `#[sv::clock_domain("fast")]` |
| `#[sv::pipeline]` | パイプラインヒント | |
| `#[sv::share]` | リソース共有ヒント | |
| `#[sv::pin("XX")]` | ピン割り当て (XDC/CST) | `#[sv::pin("H11")]` |
| `#[sv::iostandard("YY")]` | IO電圧規格 | `#[sv::iostandard("LVCMOS33")]` |

---

<!-- nav -->
← 前: [コンパイラ編 - SystemVerilogバックエンド](index.html) ｜ [目次](index.html) ｜ 次: [SVバックエンド - プロセスと代入](processes.html) →
