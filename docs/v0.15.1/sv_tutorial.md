# Cm SystemVerilog チュートリアル

Cm コンパイラの SV バックエンドを使用して、FPGA 向けの SystemVerilog コードを生成するための包括的なガイドです。

---

## 目次

1. [はじめに](#1-はじめに)
2. [最初の回路: LED 点滅](#2-最初の回路-led-点滅)
3. [プラットフォームディレクティブ](#3-プラットフォームディレクティブ)
4. [型システム](#4-型システム)
5. [ポート宣言](#5-ポート宣言)
6. [ロジックブロック](#6-ロジックブロック)
7. [演算子](#7-演算子)
8. [定数リテラルとビット幅](#8-定数リテラルとビット幅)
9. [定数と localparam](#9-定数と-localparam)
10. [制御構文](#10-制御構文)
11. [連接と複製](#11-連接と複製)
12. [列挙型 (FSM)](#12-列挙型-fsm)
13. [SV 属性](#13-sv-属性)
14. [暗黙的変換](#14-暗黙的変換)
15. [コンパイルと検証](#15-コンパイルと検証)
16. [全体例: カウンタ付き LED 点滅](#16-全体例-カウンタ付き-led-点滅)
17. [付録: トークン・キーワード一覧](#17-付録-トークンキーワード一覧)

---

## 1. はじめに

Cm の SV バックエンドは、Cm の既存構文を活用して **合成可能な SystemVerilog** を生成します。
ソフトウェア開発者にとって馴染み深い Cm の構文で FPGA 回路を記述でき、
コンパイラが適切な SV 構文（`always_ff`, `always_comb`, `<=` 代入等）に自動変換します。

### 設計哲学

- **Cm の構文を最大限活用**: 新しいキーワードは最小限にし、既存の `if/else`, `switch`, `enum` 等をそのまま使用
- **暗黙的な SV マッピング**: `=` 代入は文脈に応じて `<=` (ノンブロッキング) と `=` (ブロッキング) に自動変換
- **型安全なハードウェア記述**: 非合成型（`float`, `string`, ポインタ）はコンパイルエラー
- **1 ファイル = 1 モジュール**: ファイル名がモジュール名になる

---

## 2. 最初の回路: LED 点滅

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

生成される SV:
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

> **ポイント**: Cm の `=` が自動的に SV の `<=` (ノンブロッキング代入) に変換されています。
> `!led` も SV の `~led` に変換されています。

---

## 3. プラットフォームディレクティブ

SV バックエンドを使用するには、ファイル先頭に **必ず** 以下のディレクティブを記述します:

```cm
//! platform: sv
```

このディレクティブにより:
- `posedge`, `negedge`, `wire`, `reg` 等の SV 固有キーワードが有効化
- 非合成型（`float`, `string`, ポインタ）に対するバリデーションが有効化
- `always`, `assign`, `initial` 等の SV 構文が使用可能

---

## 4. 型システム

### 4.1 基本型と SV マッピング

| Cm 型 | SV 出力 | ビット幅 | 用途 |
|-------|---------|---------|------|
| `bool` | `logic` | 1 | フラグ、制御信号 |
| `utiny` | `logic [7:0]` | 8 | 小さなカウンタ、状態 |
| `ushort` | `logic [15:0]` | 16 | アドレス、中間値 |
| `uint` | `logic [31:0]` | 32 | カウンタ、データ |
| `ulong` | `logic [63:0]` | 64 | タイムスタンプ、大規模データ |
| `tiny` | `logic signed [7:0]` | 8 | 符号付き小数値 |
| `short` | `logic signed [15:0]` | 16 | 符号付き中間値 |
| `int` | `logic signed [31:0]` | 32 | 符号付きデータ |
| `long` | `logic signed [63:0]` | 64 | 符号付き大規模データ |

### 4.2 SV 固有型

| Cm 型 | 用途 | SV 出力 |
|-------|------|---------|
| `posedge` | クロック立ち上がりエッジ信号 | `logic` (1-bit) |
| `negedge` | クロック/リセット立ち下がりエッジ信号 | `logic` (1-bit) |
| `wire<T>` | ワイヤ修飾（組み合わせ出力） | `T` のマッピングに準拠 |
| `reg<T>` | レジスタ修飾（順序回路出力） | `T` のマッピングに準拠 |

### 4.3 カスタムビット幅

任意のビット幅を `bit[N]` で指定:

```cm
#[output] bit[4] nibble;       // → output logic [3:0] nibble
#[output] bit[12] address;     // → output logic [11:0] address
bit[26] counter;               // → logic [25:0] counter
```

### 4.4 非合成型 (コンパイルエラー)

以下の型は SV バックエンドで **コンパイルエラー** になります:

- `float`, `double` — 浮動小数点
- `string`, `cstring` — 文字列
- `*T` (ポインタ), `&T` (参照)

---

## 5. ポート宣言

ポートは属性付きグローバル変数で宣言します:

```cm
// 入力ポート
#[input]  posedge clk;              // → input logic clk
#[input]  bool rst = false;         // → input logic rst
#[input]  utiny data_in;            // → input logic [7:0] data_in

// 出力ポート
#[output] bool led = false;         // → output logic led
#[output] utiny led_array = 0xFF;   // → output logic [7:0] led_array
#[output] uint data_out;            // → output logic [31:0] data_out

// 双方向ポート
#[inout]  ushort bus;               // → inout logic [15:0] bus

// パラメータ（外部から上書き可能）
#[sv::param] uint WIDTH = 8;        // → parameter WIDTH = 32'd8;
```

> **初期値**: ポートの初期値（`= false`, `= 0xFF` 等）はポート宣言には反映されず、
> 内部ロジックのリセット値として使用されます。

---

## 6. ロジックブロック

### 6.1 順序回路 (always_ff)

#### パターン A: `always` + エッジパラメータ （推奨）

```cm
always void counter(posedge clk) {
    count = count + 1;
}
// → always_ff @(posedge clk) begin
//        count <= count + 32'd1;
//    end
```

#### パターン B: 非同期リセット付き（複数エッジ）

```cm
always void process(posedge clk, negedge rst_n) {
    if (rst_n == false) {
        count = 0;
    } else {
        count = count + 1;
    }
}
// → always_ff @(posedge clk or negedge rst_n) begin
//        if (rst_n == 1'b0) begin
//            count <= 32'd0;
//        end else begin
//            count <= count + 32'd1;
//        end
//    end
```

#### パターン C: `void f(posedge clk)` （後方互換）

```cm
void blink(posedge clk) {
    led = !led;
}
// → always_ff @(posedge clk) begin
//        led <= ~led;
//    end
```

#### パターン D: `async func` （後方互換）

```cm
async func tick() {
    counter = counter + 1;
}
// → always_ff @(posedge clk) begin
//        counter <= counter + 32'd1;
//    end
```

> **注意**: `async func` は暗黙的に `clk` 変数を参照します。
> `clk` が未宣言の場合、自動的に `input logic clk` が追加されます。

### 6.2 組み合わせ回路 (always_comb)

エッジパラメータなしの関数は `always_comb` に変換されます:

```cm
always void decode() {
    out = 0;          // デフォルト値（ラッチ防止）
    if (sel) {
        out = a;
    } else {
        out = b;
    }
}
// → always_comb begin
//        out = 32'd0;
//        if (sel) begin out = a; end
//        else begin out = b; end
//    end
```

トリガなし `void f()` / `func f()` も `always_comb` に変換されます（後方互換）:

```cm
void update() {
    signal = (counter > 100);
}
// → always_comb begin
//        signal = (counter > 32'd100);
//    end
```

### 6.3 代入の自動変換ルール

| ブロック種別 | Cm での記述 | SV 出力 |
|------------|-----------|---------|
| `always_ff` (順序回路) | `x = expr;` | `x <= expr;` (ノンブロッキング) |
| `always_comb` (組み合わせ) | `x = expr;` | `x = expr;` (ブロッキング) |

Cm では常に `=` で記述し、コンパイラが文脈に応じて適切な代入方式を選択します。

---

## 7. 演算子

### 7.1 算術演算子

| Cm | SV | 例 |
|----|----|----|
| `+` | `+` | `counter + 1` → `counter + 32'd1` |
| `-` | `-` | `a - b` |
| `*` | `*` | `a * b` |
| `/` | `/` | `a / b` |
| `%` | `%` | `a % 10` |

### 7.2 ビット演算子

| Cm | SV | 例 |
|----|----|----|
| `&` | `&` | `a & 0xFF` |
| `\|` | `\|` | `a \| b` |
| `^` | `^` | `a ^ b` |
| `~` | `~` | `~a` |
| `<<` | `<<` | `a << 2` |
| `>>` | `>>` | `a >> 1` |

### 7.3 比較/論理演算子

| Cm | SV | 備考 |
|----|----|----|
| `==` | `==` | |
| `!=` | `!=` | |
| `<` | `<` | |
| `<=` | `<=` | 比較演算子（代入の `<=` とは異なる） |
| `>` | `>` | |
| `>=` | `>=` | |
| `&&` | `&&` | |
| `\|\|` | `\|\|` | |
| `!` | `~` | **暗黙変換**: 論理否定がビット反転に統合 |

### 7.4 暗黙的な演算子変換

> **重要**: Cm の `!` (論理否定) は SV では `~` (ビット反転) にマッピングされます。
> SV の `!` は 1 ビット論理否定ですが、現在のバックエンドは多ビット信号にも安全な `~` に統一しています。

---

## 8. 定数リテラルとビット幅

Cm の定数リテラルは、文脈のビット幅に合わせて **自動的にビット幅付きリテラル** に変換されます:

| Cm リテラル | 文脈の型 | SV 出力 |
|------------|---------|---------|
| `true` | `bool` | `1'b1` |
| `false` | `bool` | `1'b0` |
| `42` | `uint` (32-bit) | `32'd42` |
| `42` | `utiny` (8-bit) | `8'd42` |
| `42` | `int` (符号付き32-bit) | `32'sd42` |
| `-5` | `int` | `-32'sd5` |

### SV スタイルのリテラル

Cm は SV スタイルのビット幅指定リテラルもそのまま使用可能:

```cm
utiny mask = 8'b10101010;     // → 8'b10101010
ushort addr = 16'hFF00;       // → 16'hFF00
```

### 数値区切り文字

大きな数値にはアンダースコア `_` が使えます:

```cm
const uint CLK_FREQ = 50_000_000;   // → localparam CLK_FREQ = 32'd50000000;
```

---

## 9. 定数と localparam

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

外部モジュールからオーバーライド可能なパラメータ:

```cm
#[sv::param] const uint WIDTH = 8;
```
```systemverilog
parameter WIDTH = 32'd8;
```

---

## 10. 制御構文

### 10.1 if / else if / else

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
    // idle
end
```

### 10.2 switch → case

```cm
switch (state) {
    case 0: {
        next_state = 1;
    }
    case 1: {
        next_state = 2;
    }
    default: {
        next_state = 0;
    }
}
```
```systemverilog
case (state)
    32'd0: begin
        next_state <= 32'd1;
    end
    32'd1: begin
        next_state <= 32'd2;
    end
    default: begin
        next_state <= 32'd0;
    end
endcase
```

---

## 11. 連接と複製

### 連接 (Concatenation)

```cm
result = {a, b};          // → result = {a, b};
wide = {a, b, c};         // → wide = {a, b, c};
```

### 複製 (Replication)

```cm
replicated = {3{a}};      // → replicated = {3{a}};
```

### ビルトイン関数

連接の `{...}` 構文がブロック `{...}` と曖昧になる場合、ビルトイン関数を使用:

```cm
result = concat(a, b);       // → result = {a, b};
wide = replicate(nibble, 3); // → wide = {3{nibble}};
```

---

## 12. 列挙型 (FSM)

Cm の `enum` は SV の `typedef enum logic` に変換されます。
ビット幅はバリアント数から自動計算:

```cm
enum State {
    IDLE,
    RUN,
    DONE,
    ERROR
}
```
```systemverilog
typedef enum logic [1:0] {
    IDLE  = 2'd0,
    RUN   = 2'd1,
    DONE  = 2'd2,
    ERROR = 2'd3
} State;
```

FSM での使用例:

```cm
//! platform: sv

enum State { IDLE, RUN, DONE }

#[input]  posedge clk;
#[input]  bool rst = false;
#[output] uint count = 0;

always void process(posedge clk) {
    if (rst) {
        count = 0;
    } else {
        switch (state) {
            case State::IDLE: { state = State::RUN; }
            case State::RUN:  { count = count + 1; }
            default: {}
        }
    }
}
```

---

## 13. SV 属性

### ポート属性

| 属性 | 効果 | 例 |
|------|------|----|
| `#[input]` | 入力ポート | `#[input] posedge clk;` |
| `#[output]` | 出力ポート | `#[output] utiny led = 0xFF;` |
| `#[inout]` | 双方向ポート | `#[inout] ushort bus;` |

### パラメータ属性

| 属性 | 効果 | 例 |
|------|------|----|
| `#[sv::param]` | `parameter` 宣言 | `#[sv::param] uint WIDTH = 8;` |

### メモリ属性

| 属性 | 効果 | 例 |
|------|------|----|
| `#[sv::bram]` | `(* ram_style = "block" *)` | `#[sv::bram] utiny mem[1024];` |
| `#[sv::lutram]` | `(* ram_style = "distributed" *)` | `#[sv::lutram] utiny lut[16];` |

### 合成ヒント

| 属性 | 効果 |
|------|------|
| `#[sv::pipeline]` | パイプラインヒントコメント生成 |
| `#[sv::share]` | リソース共有ヒントコメント生成 |

### クロック/タイミング

| 属性 | 効果 | 例 |
|------|------|----|
| `#[sv::clock_domain("name")]` | `async func` のクロックを指定 | `#[sv::clock_domain("fast")]` |

### 物理配置 (XDC/CST 生成)

| 属性 | 効果 | 例 |
|------|------|----|
| `#[sv::pin("A1")]` | ピン割り当て | `#[sv::pin("H11")] #[input] posedge clk;` |
| `#[sv::iostandard("LVCMOS33")]` | IO 電圧規格 | `#[sv::iostandard("LVCMOS18")]` |

---

## 14. 暗黙的変換

Cm の SV バックエンドは、開発者が意識せずとも正しい SV コードを生成するために
多数の暗黙的変換を行います。

### 14.1 代入方式の自動決定

| Cm コード | 文脈 | SV 出力 |
|----------|------|---------|
| `x = expr;` | `always_ff` 内 | `x <= expr;` (ノンブロッキング) |
| `x = expr;` | `always_comb` 内 | `x = expr;` (ブロッキング) |

### 14.2 論理否定の変換

| Cm コード | SV 出力 | 理由 |
|----------|---------|------|
| `!flag` | `~flag` | 多ビット信号に安全な `~` に統一 |
| `~data` | `~data` | そのまま |

### 14.3 リテラルのビット幅付与

| Cm コード | 代入先の型 | SV 出力 |
|----------|-----------|---------|
| `counter = 0;` | `uint` (32-bit) | `counter <= 32'd0;` |
| `flag = true;` | `bool` (1-bit) | `flag <= 1'b1;` |
| `val = 42;` | `utiny` (8-bit) | `val <= 8'd42;` |

### 14.4 クロック/リセットの自動追加

| 条件 | 動作 |
|------|------|
| `async func` 存在 & `clk` 未宣言 | `input logic clk` を自動追加 |
| `async func` 存在 & `rst` 未宣言 | `input logic rst` を `clk` の後に自動追加 |

### 14.5 MIR 一時変数のインライン展開

MIR で生成される `_tXXXX` 一時変数は、SV 出力時に元の式にインライン展開されます:

```
// MIR: _t1000 = counter + 1; result = _t1000;
// SV:  result <= counter + 32'd1;  (一時変数が消える)
```

### 14.6 `self.` プレフィックスの除去

```
// MIR: self.counter → SV: counter
```

### 14.7 `else if` の正規化

ネストした `else { if ... }` パターンは SV の `else if` に正規化されます:

```systemverilog
// ネストせず、フラットな else if チェーンを生成
if (cond1) begin
    ...
end else if (cond2) begin
    ...
end else begin
    ...
end
```

### 14.8 冗長な三項演算子の除去

`cond ? x : x` のような冗長な三項演算子は単純な代入 `x` に最適化されます。

---

## 15. コンパイルと検証

### コンパイル

```bash
# SV コード生成
cm compile --target=sv blink.cm -o blink.sv

# テストベンチも同時生成
cm compile --target=sv blink.cm -o blink.sv --testbench
```

### Verilator でのシミュレーション

```bash
# Verilator でコンパイル + シミュレーション
verilator --sv --lint-only blink.sv      # 構文チェック
verilator --sv --cc blink.sv --exe       # シミュレーション
```

### Icarus Verilog での検証

```bash
iverilog -g2012 -o blink_sim blink.sv blink_tb.sv
vvp blink_sim
```

### FPGA ビルド (Gowin EDA)

```bash
# Cm → SV → Gowin EDA → ビットストリーム
cm compile --target=sv blink.cm -o blink.sv
gw_sh gowin_build.tcl
```

---

## 16. 全体例: カウンタ付き LED 点滅

```cm
//! platform: sv

// === ポート定義 ===
#[input]  posedge clk;
#[input]  negedge rst_n;
#[output] bool led = false;

// === 定数 ===
const uint CLK_FREQ = 27_000_000;    // 27MHz (Tang Console)
const uint CNT_MAX = CLK_FREQ / 2 - 1;

// === 内部レジスタ ===
uint counter = 0;

// === 順序回路: 非同期リセット付き ===
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

生成される SV:
```systemverilog
`timescale 1ns / 1ps

module blink (
    input logic clk,
    input logic rst_n,
    output logic led
);
    localparam CLK_FREQ = 32'd27000000;
    localparam CNT_MAX = CLK_FREQ / 2 - 32'd1;

    logic [31:0] counter;

    always_ff @(posedge clk or negedge rst_n) begin
        if (rst_n == 1'b0) begin
            counter <= 32'd0;
            led <= 1'b0;
        end else begin
            if (counter == CNT_MAX) begin
                counter <= 32'd0;
                led <= ~led;
            end else begin
                counter <= counter + 32'd1;
            end
        end
    end
endmodule
```

---

## 17. 付録: トークン・キーワード一覧

### SV 固有トークン

| トークン | キーワード | TypeKind | 用途 |
|---------|---------|----------|------|
| `KwPosedge` | `posedge` | `Posedge` | 立ち上がりエッジ |
| `KwNegedge` | `negedge` | `Negedge` | 立ち下がりエッジ |
| `KwWire` | `wire` | `Wire` | ワイヤ修飾型 |
| `KwReg` | `reg` | `Reg` | レジスタ修飾型 |
| `KwAlways` | `always` | — | ロジックブロック修飾子 |
| `KwAssign` | `assign` | — | 連続代入文 |
| `KwInitial` | `initial` | — | シミュレーション初期化 |
| `KwBit` | `bit` | `Bit` | 任意ビット幅型 |

### 既存トークンの SV での意味

| トークン | 通常 (LLVM) の意味 | SV での意味 |
|---------|-------------------|------------|
| `async` | JS 非同期関数 | `always_ff` ブロック生成 (後方互換) |
| `func` | 関数宣言 | `always_comb` ブロック生成 |
| `void` | 戻り値なし関数 | ブロック生成 (ff/comb) |
| `=` | 変数代入 | ff 内: `<=`, comb 内: `=` |
| `!` | 論理否定 | `~` (ビット反転に統合) |
| `const` | 定数宣言 | `localparam` |
| `switch/case` | パターンマッチ | `case/endcase` |
| `enum` | 列挙型 | `typedef enum logic` |

### SV 予約語 (モジュール名回避)

以下の名前はモジュール名として使用できません:

```
output, input, inout, module, wire, reg, logic, begin, end,
if, else, for, while, case, default, assign, always, initial,
posedge, negedge, task, function, parameter, integer, real, time, event
```
