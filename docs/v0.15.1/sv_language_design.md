# Cm SV バックエンド 言語デザイン v0.15.1

> **設計原則**: Cmの既存構文を最大限活かし、SV固有の概念のみ新トークンで追加する。

---

## 新規トークン (追加)

| トークン | キーワード | 用途 |
|---------|---------|------|
| `KwAlways` | `always` | SV ロジックブロック修飾子 |
| `KwAssign` | `assign` | 連続代入文 |
| `KwInitial` | `initial` | シミュレーション初期化ブロック |
| `KwBit` | `bit` | 任意ビット幅型 `bit<N>` |

※ 既存の `KwPosedge`, `KwNegedge`, `KwWire`, `KwReg` はそのまま維持。

---

## 1. コンパイルモデル

```
cm compile --target=sv input.cm -o output.sv
```

**1ファイル = 1モジュール** の原則:

| 項目 | Cm (LLVM) | Cm (SV) |
|------|-----------|---------|
| `import` の動作 | 再帰的にフラット化 → 1バイナリ | **モジュール参照のみ** → 別ファイル |
| 出力 | 1つの実行ファイル | **1つの .sv ファイル** |
| リンク | コンパイラが行う | **Gowin EDA / Yosys** が行う |

```cm
//! platform: sv
import Gowin_OSC;     // ← Gowin_OSCモジュールの「存在」を知る（コンパイルはしない）
import UART_TX;       // ← UART_TXモジュールの「存在」を知る（コンパイルはしない）
```

ファイル名からモジュール名を自動推定。`//! platform: sv` 指定必須。

---

## 2. ポート宣言 (変更なし)

```cm
#[input]   posedge clk;
#[input]   negedge rst_n;
#[input]   bool enable = false;
#[output]  utiny led = 0xFF;
#[output]  uint data_out;
#[inout]   ushort bus;
```

既存の `#[input]`/`#[output]`/`#[inout]` 属性をそのまま使用。

---

## 3. ロジックブロック

### 3.1 always_ff (順序回路)

`always` + エッジパラメータ → `always_ff @(...)` を生成。

```cm
// 基本: posedge clk
always void counter(posedge clk) {
    count = count + 1;
}
// → always_ff @(posedge clk) begin
//        count <= count + 32'd1;
//    end

// 非同期リセット: 複数エッジ
always void process(posedge clk, negedge rst_n) {
    if (!rst_n) {
        count = 0;
    } else {
        count = count + 1;
    }
}
// → always_ff @(posedge clk or negedge rst_n) begin
//        if (!rst_n) begin
//            count <= 32'd0;
//        end else begin
//            count <= count + 32'd1;
//        end
//    end
```

**代入ルール**: `always` ブロック内の `=` は自動的に `<=` (ノンブロッキング) にマッピング。

### 3.2 always_comb (組み合わせ回路)

`always` + エッジパラメータなし → `always_comb` を生成。

```cm
always void decode() {
    out = 0;      // デフォルト値（ラッチ防止）
    if (sel) {
        out = a;
    } else {
        out = b;
    }
}
// → always_comb begin
//        out = 32'd0;
//        if (sel) begin
//            out = a;
//        end else begin
//            out = b;
//        end
//    end
```

**代入ルール**: エッジなし `always` ブロック内の `=` はブロッキング代入 (`=`) のまま。

### 3.3 後方互換

```cm
// 旧構文A: async → always_ff @(posedge clk) として引き続き動作
async void tick() {
    count = count + 1;
}

// 旧構文B: posedgeパラメータ → always_ff として引き続き動作
void blink(posedge clk) {
    led = !led;
}

// 旧構文C: トリガなし void → always_comb として引き続き動作
void update() {
    signal = (counter > 100);
}
```

---

## 4. 連続代入 (assign)

```cm
// assign文: wire的な組み合わせ出力
assign bool led = (counter > 25000000);
// → assign led = (counter > 25000000);

assign utiny mux_out = sel ? a : b;
// → assign mux_out = sel ? a : b;
```

`assign` 変数は自動的にポートリストまたはwire宣言に反映。

---

## 5. 定数パラメータ

```cm
// const → localparam (モジュール内ローカル定数)
const uint CLK_FREQ = 50_000_000;
const uint CNT_MAX = CLK_FREQ / 2 - 1;
// → localparam CLK_FREQ = 32'd50000000;
// → localparam CNT_MAX = CLK_FREQ / 2 - 32'd1;

// #[sv::param] + 非const → parameter (外部から上書き可能)
#[sv::param] uint WIDTH = 8;
// → parameter WIDTH = 32'd8;
```

---

## 6. 型システム

### 6.1 基本型 (変更なし)

| Cm型 | SV出力 | 幅 |
|------|--------|-----|
| `bool` | `logic` | 1 |
| `utiny` | `logic [7:0]` | 8 |
| `ushort` | `logic [15:0]` | 16 |
| `uint` | `logic [31:0]` | 32 |
| `ulong` | `logic [63:0]` | 64 |
| `tiny` | `logic signed [7:0]` | 8 |
| `short` | `logic signed [15:0]` | 16 |
| `int` | `logic signed [31:0]` | 32 |
| `long` | `logic signed [63:0]` | 64 |

### 6.2 SV固有型 (変更なし)

| Cm型 | SV用途 |
|------|--------|
| `posedge` | クロック立ち上がり |
| `negedge` | クロック/リセット立ち下がり |
| `wire<T>` | ワイヤ修飾 |
| `reg<T>` | レジスタ修飾 |

### 6.3 カスタムビット幅 (新規)

```cm
// 任意ビット幅: bit<N> 構文
#[output] bit<4> nibble;      // → output logic [3:0] nibble
#[output] bit<12> address;    // → output logic [11:0] address

bit<26> counter;              // → logic [25:0] counter
```

> [!NOTE]
> `bit<N>` は **新規型** として追加。SV の合成設計で頻出する任意ビット幅をサポート。

---

## 7. 演算子

### 7.1 既存演算子 (変更なし)

算術: `+` `-` `*` `/` `%`
ビット: `&` `|` `^` `~` `<<` `>>`
比較: `==` `!=` `<` `<=` `>` `>=`
論理: `&&` `||` `!`

### 7.2 新規演算子・ビルトイン

| Cm構文 | SV出力 | 用途 |
|-------|--------|------|
| `{a, b}` | `{a, b}` | 連接 (concatenation) |
| `{a, b, c}` | `{a, b, c}` | 多項連接 |
| `{N{expr}}` | `{N{expr}}` | 複製 (replication) |
| `x[7:0]` | `x[7:0]` | ビットスライス |
| `x[i]` | `x[i]` | ビット選択 |
| `!x` | `!x` | 論理否定 (1-bit) |
| `~x` | `~x` | ビット反転 |

> [!NOTE]
> **連接 `{a, b}`**: 式コンテキスト（代入RHS、関数引数等）では連接式、
> 制御構文の直後ではブロック `{...}` として、パーサーが意味論的に区別する。
> 代替として `concat(a, b)` ビルトイン関数も利用可能。
> **インクリメント**: `count++` は `count = count + 1` に展開される。

### 7.3 三項演算子

```cm
assign uint result = (sel) ? a : b;
// → assign result = (sel) ? a : b;
```

Cm の三項演算子 `?:` をそのまま SV の三項にマッピング。

---

## 8. 制御構文

### 8.1 if/else (変更なし)

```cm
if (condition) {
    // ...
} else if (other) {
    // ...
} else {
    // ...
}
```

### 8.2 switch → case

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
// → case (state)
//        32'd0: begin next_state <= 32'd1; end
//        32'd1: begin next_state <= 32'd2; end
//        default: begin next_state <= 32'd0; end
//    endcase
```

### 8.3 for ループ (新規: generate対応)

```cm
// 静的forループ → generate for
for (uint i = 0; i < WIDTH; i = i + 1) {
    assign out[i] = in[WIDTH - 1 - i];
}
// → genvar i;
// → generate for (i = 0; i < WIDTH; i = i + 1) begin : gen_reverse
//        assign out[i] = in[WIDTH - 1 - i];
//    end endgenerate
```

---

## 9. 構造化型

### 9.1 パックド構造体

```cm
#[sv::packed]
struct AXIAddr {
    uint addr;
    utiny len;
    utiny size;
    utiny burst;
}
// → typedef struct packed {
//        logic [31:0] addr;
//        logic [7:0] len;
//        logic [7:0] size;
//        logic [7:0] burst;
//    } AXIAddr;
```

### 9.2 FSM用列挙型

```cm
enum State {
    IDLE,
    READ,
    WRITE,
    DONE
}
// → typedef enum logic [1:0] {
//        IDLE  = 2'd0,
//        READ  = 2'd1,
//        WRITE = 2'd2,
//        DONE  = 2'd3
//    } State;
```

Cmの既存 `enum` 構文を SV の `typedef enum` にマッピング。
ビット幅はバリアント数から自動計算。

---

## 10. SV function / task

### 10.1 function (純粋組み合わせ関数)

```cm
// #[sv::function] 属性 → SV function
#[sv::function]
uint mux4(uint a, uint b, uint c, uint d, utiny sel) {
    switch (sel) {
        case 0: { return a; }
        case 1: { return b; }
        case 2: { return c; }
        default: { return d; }
    }
}
// → function automatic logic [31:0] mux4(
//        input logic [31:0] a, b, c, d,
//        input logic [7:0] sel
//    );
//        case (sel)
//            8'd0: mux4 = a;
//            ...
//        endcase
//    endfunction
```

### 10.2 task (手続き的ブロック)

```cm
#[sv::task]
void send_byte(utiny data) {
    tx_valid = true;
    tx_data = data;
}
// → task automatic send_byte(input logic [7:0] data);
//        tx_valid <= 1'b1;
//        tx_data <= data;
//    endtask
```

---

## 11. メモリ推論

```cm
#[sv::bram]
utiny memory[1024];                // → (* ram_style = "block" *) logic [7:0] memory [0:1023];

#[sv::lutram]
utiny lookup_table[16];            // → (* ram_style = "distributed" *) logic [7:0] lookup_table [0:15];
```

---

## 12. モジュールインスタンス化 (import/export)

```cm
// 外部モジュールのインポート
import Gowin_OSC;

// インスタンス化（名前付き接続）
Gowin_OSC osc_inst(
    .oscout = clk
);
// → Gowin_OSC osc_inst (
//        .oscout(clk)
//    );

// 複数モジュールのインポート
import UART_TX;
import UART_RX;

UART_TX tx_inst(.clk = clk, .data = tx_data, .tx = tx_pin);
UART_RX rx_inst(.clk = clk, .rx = rx_pin, .data = rx_data);
```

自分のモジュールを外部公開する場合:
```cm
//! platform: sv
export;  // このモジュールを他のCmファイルからimport可能にする

#[input]  posedge clk;
#[output] bool tx;
// ...
```

---

## 13. initial ブロック (シミュレーション専用)

```cm
initial {
    clk = false;
    rst = true;
    // 10ns後にリセット解除
    rst = false;
}
// → initial begin
//        clk = 1'b0;
//        rst = 1'b1;
//        #10 rst = 1'b0;
//    end
```

---

## 14. 定数リテラル (変更なし)

| Cm | SV出力 |
|----|--------|
| `true` / `false` | `1'b1` / `1'b0` |
| `42` | `32'd42` (コンテキスト依存) |
| `8'b10101010` | `8'b10101010` |
| `16'hFF00` | `16'hFF00` |

---

## 15. 属性一覧

| 属性 | SV効果 | カテゴリ |
|------|--------|---------|
| `#[input]` | 入力ポート | ポート |
| `#[output]` | 出力ポート | ポート |
| `#[inout]` | 双方向ポート | ポート |
| `#[sv::param]` | `parameter` | パラメータ |
| `#[sv::bram]` | `(* ram_style = "block" *)` | メモリ |
| `#[sv::lutram]` | `(* ram_style = "distributed" *)` | メモリ |
| `#[sv::clock_domain("name")]` | クロック指定 | タイミング |
| `#[sv::pipeline]` | パイプラインヒント | 合成 |
| `#[sv::share]` | リソース共有 | 合成 |
| `#[sv::packed]` | パックド構造体 | 型 |
| `#[sv::function]` | SV function生成 | ブロック |
| `#[sv::task]` | SV task生成 | ブロック |
| `#[sv::module]` | 外部モジュール宣言 | インスタンス |
| `#[sv::pin("XX")]` | ピン割当 | 物理 |
| `#[sv::iostandard("YY")]` | IO標準 | 物理 |

---

## 16. 完全な回路例

```cm
//! platform: sv

// ポート宣言
#[input]  posedge clk;
#[input]  negedge rst_n;
#[output] bool led;

// 定数
const uint CLK_FREQ = 50_000_000;
const uint CNT_MAX = CLK_FREQ / 2 - 1;

// 内部レジスタ
uint counter = 0;

// FSM状態
enum State { IDLE, RUN, DONE }
State state = State::IDLE;

// 順序回路（非同期リセット付き）
always void process(posedge clk, negedge rst_n) {
    if (!rst_n) {
        counter = 0;
        led = false;
        state = State::IDLE;
    } else {
        switch (state) {
            case State::IDLE: {
                state = State::RUN;
            }
            case State::RUN: {
                if (counter == CNT_MAX) {
                    counter = 0;
                    led = !led;
                } else {
                    counter = counter + 1;
                }
            }
            default: {}
        }
    }
}
```

出力SV:
```systemverilog
module example (
    input logic clk,
    input logic rst_n,
    output logic led
);
    localparam CLK_FREQ = 32'd50000000;
    localparam CNT_MAX = CLK_FREQ / 2 - 32'd1;

    logic [31:0] counter;

    typedef enum logic [1:0] {
        IDLE = 2'd0, RUN = 2'd1, DONE = 2'd2
    } State;
    State state;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            counter <= 32'd0;
            led <= 1'b0;
            state <= IDLE;
        end else begin
            case (state)
                IDLE: begin
                    state <= RUN;
                end
                RUN: begin
                    if (counter == CNT_MAX) begin
                        counter <= 32'd0;
                        led <= ~led;
                    end else begin
                        counter <= counter + 32'd1;
                    end
                end
                default: begin end
            endcase
        end
    end
endmodule
```

---

## トークン一覧 (最終)

### 既存トークン (SV バックエンドで使用)

| トークン | SV での意味 |
|---------|-----------|
| `KwAsync` | `always_ff` (後方互換) |
| `KwVoid` | ブロック戻り型 |
| `KwConst` | `localparam` |
| `KwStruct` | `struct packed` (+ 属性) |
| `KwEnum` | `typedef enum` |
| `KwSwitch`/`KwCase`/`KwDefault` | `case/endcase` |
| `KwFor` | `generate for` |
| `KwReturn` | `function` 戻り値 |
| `KwIf`/`KwElse` | `if/else` |
| `KwExtern` | 外部モジュール宣言 |
| `KwPosedge` | `posedge` 信号型 |
| `KwNegedge` | `negedge` 信号型 |
| `KwWire` | `wire` 修飾型 |
| `KwReg` | `reg` 修飾型 |

### 新規トークン

| トークン | キーワード | SV での意味 |
|---------|---------|-----------|
| `KwAlways` | `always` | ロジックブロック修飾子 |
| `KwAssign` | `assign` | 連続代入文 |
| `KwInitial` | `initial` | シミュレーション初期化 |
| `KwBit` | `bit` | 任意ビット幅型 `bit<N>` |

### ビルトイン関数 (SV モード)

| 関数 | SV出力 | 用途 |
|------|--------|------|
| `concat(a, b, ...)` | `{a, b, ...}` | ビット連接 |
| `replicate(expr, N)` | `{N{expr}}` | ビット複製 |
