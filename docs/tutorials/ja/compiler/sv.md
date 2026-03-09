---
title: SystemVerilogバックエンド
parent: Tutorials
nav_order: 11
---

[English](../../en/compiler/sv.html)

# コンパイラ編 - SystemVerilogバックエンド

**難易度:** 🟡 中級  
**所要時間:** 30分

CmからSystemVerilog (SV) を生成し、FPGA上でハードウェアとして動作させることができます。Tang Console（Gowin）、Xilinx、Intel等のFPGAに対応しています。

## 基本的な使い方

```bash
# SV生成
cm compile --target=sv program.cm -o output.sv

# テストベンチも自動生成される
# output_tb.sv が同時に生成
```

## ポート宣言

`#[input]` / `#[output]` アトリビュートで入出力ポートを宣言します。

```cm
//! platform: sv

#[input]  int a = 0;
#[input]  int b = 0;
#[output] int sum = 0;

void adder() {
    sum = a + b;
}
```

生成されるSV:

```systemverilog
module adder (
    input logic signed [31:0] a,
    input logic signed [31:0] b,
    output logic signed [31:0] sum
);

    // adder
    always_comb begin
        sum = a + b;
    end

endmodule
```

## 組み合わせ回路と順序回路

### 組み合わせ回路（always_comb）

通常の関数は組み合わせ回路（`always_comb`）として生成されます。

```cm
//! platform: sv

#[input]  int a = 0;
#[input]  int b = 0;
#[input]  int c = 0;
#[output] int out = 0;

void max3() {
    if (a > b) {
        if (a > c) { out = a; } else { out = c; }
    } else {
        if (b > c) { out = b; } else { out = c; }
    }
}
```

### 順序回路（always_ff）

`posedge` / `negedge` 型パラメータを使うと `always_ff` ブロックが生成されます。

```cm
//! platform: sv

#[output] uint counter = 0;
#[output] bool led = false;

void blink(posedge clk, bool rst) {
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

生成されるSV:

```systemverilog
always_ff @(posedge clk) begin
    if (rst) begin
        counter <= 32'd0;
        led <= 1'b0;
    end else begin
        if (counter == 32'd49999999) begin
            counter <= 32'd0;
            led <= !led;
        end else begin
            counter <= counter + 32'd1;
        end
    end
end
```

## SV固有型

| Cm型 | 説明 | 生成されるSV |
|------|------|------------|
| `posedge` | 立ち上がりエッジ | `always_ff @(posedge ...)` |
| `negedge` | 立ち下がりエッジ | `always_ff @(negedge ...)` |
| `wire` | ワイヤ | `wire` |
| `reg` | レジスタ | `reg` |

## SV幅付きリテラル

SystemVerilog形式の幅付きリテラルを直接記述でき、SV出力でもそのまま保持されます。

```cm
//! platform: sv

#[input]  utiny sel = 0;
#[output] utiny out = 0;

void literal_test() {
    if (sel == 0) {
        out = 3'b101;     // 2進数: 3ビット幅
    } else if (sel == 1) {
        out = 8'hFF;      // 16進数: 8ビット幅
    } else {
        out = 8'd170;     // 10進数: 8ビット幅
    }
}
```

| Cm記述 | SV出力 |
|--------|--------|
| `3'b101` | `3'b101` |
| `8'hFF` | `8'hFF` |
| `8'd170` | `8'd170` |

## Cm型とSV型の対応

| Cm型 | SVビット幅 | SV型 |
|------|-----------|------|
| `bool` | 1 | `logic` |
| `utiny` | 8 | `logic [7:0]` |
| `tiny` | 8 | `logic signed [7:0]` |
| `ushort` | 16 | `logic [15:0]` |
| `short` | 16 | `logic signed [15:0]` |
| `uint` | 32 | `logic [31:0]` |
| `int` | 32 | `logic signed [31:0]` |

## BRAM推論

配列はBlock RAM（BRAM）として推論されます。

```cm
//! platform: sv

int memory[256];
#[input]  utiny addr = 0;
#[input]  int   wdata = 0;
#[input]  bool  we = false;
#[output] int   rdata = 0;

void bram_access(posedge clk) {
    if (we) {
        memory[addr] = wdata;
    }
    rdata = memory[addr];
}
```

## テストベンチ自動生成

`cm compile --target=sv` を実行すると `_tb.sv` テストベンチも自動生成されます。iverilogで検証可能:

```bash
# コンパイルとテストベンチ生成
cm compile --target=sv program.cm -o output.sv

# シミュレーション実行
iverilog -g2012 -o sim output.sv output_tb.sv
vvp sim
```

## ターゲットFPGA

| ボード | チップ | ツール |
|--------|--------|--------|
| Tang Console | Gowin | Gowin EDA |
| Tang Nano 9K | Gowin GW1NR-9 | Gowin EDA |
| Arty A7 | Xilinx Artix-7 | Vivado |
| DE10-Lite | Intel MAX 10 | Quartus |

> **Note:** Gowin EDAでは Project → Configuration → Synthesis → Verilog Language でSystemVerilogを有効にしてください。

---

**前の章:** [WASMバックエンド](wasm.html)  
**次の章:** [フォーマッタ](formatter.html)

---

**最終更新:** 2026-03-09
