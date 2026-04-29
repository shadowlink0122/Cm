# SystemVerilog バックエンド設計書

> 対象: Cm v0.15.0
> ステータス: **IMPLEMENTED** (Phase 1) — rev.3 (2026-03-09更新)

## 1. 概要

Cmコンパイラに**SystemVerilog (SV) バックエンド**を追加し、Cmのソースコードからシンセサイズ可能な RTL コードを生成する。C/C++ベースのHLS (High-Level Synthesis) ツールと同等の変換を、Cmの型システムとasync構文を活用して実現する。

### 設計思想

- `utiny` (8bit), `ushort` (16bit), `uint` (32bit), `ulong` (64bit) → **ビット幅が型で自然に決まる**
- `async func` → **`always_ff @(posedge clk)` への自然なマッピング**
- `struct` → **`module` (ポートリスト) への自然なマッピング**
- `enum` → **FSM (有限状態マシン) への変換**
- `++` / `--` → MIR展開済み (`var = var + 1`) → `var <= var + 1` として出力

---

## 2. 型システム

### 2.1 既存型マッピング

| Cm型 | SystemVerilog型 | ビット幅 |
|------|----------------|---------|
| `bool` | `logic` | 1 |
| `utiny` / `tiny` | `logic [7:0]` / `logic signed [7:0]` | 8 |
| `ushort` / `short` | `logic [15:0]` / `logic signed [15:0]` | 16 |
| `uint` / `int` | `logic [31:0]` / `logic signed [31:0]` | 32 |
| `ulong` / `long` | `logic [63:0]` / `logic signed [63:0]` | 64 |

### 2.2 新規: `bit<N>` 型

任意ビット幅のハードウェアレジスタ/ワイヤを表現するための新しい型を導入する。

```cm
// 任意ビット幅型（配列サフィックス形式）
bit[24] addr = 24'h0;     // 24ビットアドレス
bit[3] rgb = 3'b101;      // 3ビットRGB
bit[128] data = 128'h0;   // 128ビット幅データ
```

#### SV出力

```systemverilog
logic [23:0] addr = 24'h0;
logic [2:0]  rgb  = 3'b101;
logic [127:0] data = 128'h0;
```

#### リテラル記法

| Cm記法 | SV記法 | 意味 |
|--------|--------|------|
| `8'hFF` | `8'hFF` | 8ビット16進 |
| `4'b1010` | `4'b1010` | 4ビット2進 |
| `16'd1024` | `16'd1024` | 16ビット10進 |
| `32'h0` | `32'h0` | 32ビットゼロ |

> **実装**: レキサーに `N'[hbdo]value` パターンを追加。`bit<N>` はジェネリクス構文を再利用 (`TypeKind::BitVector`)。

### 2.3 SVターゲットで使用不可の型

| 型 | 理由 | コンパイルエラーメッセージ |
|----|------|----------------------|
| ポインタ (`*`) | ハードウェアにアドレス空間なし | `Pointer types are not supported in SV target` |
| 文字列 (`string`) | 可変長データは合成不可 | `String types are not synthesizable` |
| 浮動小数点 (`float`/`double`) | Phase 2 | `Floating-point requires IP core (Phase 2)` |
| ヒープ (Vector等) | 動的メモリなし | `Dynamic allocation not available in hardware` |

---

## 3. モジュールとポート

### 3.1 `struct` → `module` 変換

```cm
#[sv::module]
struct UartTx {
    // === ポート宣言 ===
    #[input]  clk:     bool,           // クロック
    #[input]  rst_n:   bool,           // アクティブローリセット
    #[input]  tx_data: utiny,          // 送信データ (8bit)
    #[input]  tx_start: bool,          // 送信開始トリガ
    #[output] tx_pin:  bool,           // UART TX 出力ピン
    #[output] tx_busy: bool,           // 送信中フラグ
}
```

生成される SV:

```systemverilog
module UartTx (
    input  logic        clk,
    input  logic        rst_n,
    input  logic [7:0]  tx_data,
    input  logic        tx_start,
    output logic        tx_pin,
    output logic        tx_busy
);
    // ...
endmodule
```

### 3.2 ピン割当 (Constraints)

物理ピンへのマッピングは**XDC/SDCファイル**として別途出力する。

```cm
#[sv::module]
#[sv::constraints("fpga_pin_map.xdc")]
struct Top {
    #[input]  #[sv::pin("E3")]   clk_100mhz: bool,  // Nexys A7 100MHz
    #[input]  #[sv::pin("C12")]  rst_n: bool,        // CPU RESET button
    #[output] #[sv::pin("H5")]   led0: bool,         // LED[0]
    #[output] #[sv::pin("D4")]   uart_tx: bool,      // USB-UART TX
    #[input]  #[sv::pin("C4")]   uart_rx: bool,      // USB-UART RX
}
```

生成される XDC:

```tcl
## Clock
set_property -dict { PACKAGE_PIN E3  IOSTANDARD LVCMOS33 } [get_ports {clk_100mhz}]
create_clock -add -name sys_clk_pin -period 10.00 [get_ports {clk_100mhz}]

## Reset
set_property -dict { PACKAGE_PIN C12 IOSTANDARD LVCMOS33 } [get_ports {rst_n}]

## LEDs
set_property -dict { PACKAGE_PIN H5  IOSTANDARD LVCMOS33 } [get_ports {led0}]

## UART
set_property -dict { PACKAGE_PIN D4  IOSTANDARD LVCMOS33 } [get_ports {uart_tx}]
set_property -dict { PACKAGE_PIN C4  IOSTANDARD LVCMOS33 } [get_ports {uart_rx}]
```

> IOSTANDARD はデフォルト `LVCMOS33`。`#[sv::iostandard("LVDS")]` で変更可能。

### 3.3 モジュールインスタンス化

struct内のstruct → サブモジュールインスタンス化:

```cm
#[sv::module]
struct Top {
    #[input] clk: bool,
    #[input] rst: bool,
    #[output] led: utiny,
    
    // サブモジュール (非ポート)
    counter: Counter,
    uart: UartTx,
}

impl Top {
    func connect(self) {
        // ポート接続
        self.counter.clk = self.clk;
        self.counter.rst = self.rst;
        self.led = self.counter.count as utiny;
        
        self.uart.clk = self.clk;
        self.uart.rst_n = !self.rst;
    }
}
```

生成される SV:

```systemverilog
module Top (
    input  logic       clk,
    input  logic       rst,
    output logic [7:0] led
);

Counter counter_inst (
    .clk   (clk),
    .rst   (rst),
    .count ()        // → led に接続
);

UartTx uart_inst (
    .clk   (clk),
    .rst_n (~rst)
);

assign led = counter_inst.count[7:0];

endmodule
```

---

## 4. `async func` → `always` ブロック変換

### 4.1 変換ルール

| Cmの関数種別 | SVブロック | 代入演算子 | 用途 |
|-------------|----------|-----------|------|
| `async func` | `always_ff @(posedge clk)` | `<=` (ノンブロッキング) | 順序回路（FF） |
| `func` (戻り値あり) | `always_comb` | `=` (ブロッキング) | 組み合わせ回路 |

### 4.2 `++`/`--` の展開

`self.count++` はMIR loweringで `self.count = self.count + 1` に展開される（`expr_ops.cpp` L710-763）。
SVバックエンドはこのMIRを入力とするため、追加の変換は不要:

```
Cm:   self.count++;
MIR:  _5 = Add(_3, Const(1))    // _3 = count, _5 = new_value
      Assign(count, _5)
SV:   count <= count + 32'd1;    // async func内
```

### 4.3 基本例: カウンタ

```cm
#[sv::module]
struct Counter {
    #[input]  clk: bool,
    #[input]  rst: bool,
    #[input]  enable: bool,
    #[output] count: uint,
}

impl Counter {
    async func tick(self) {
        if self.rst {
            self.count = 0;
        } else if self.enable {
            self.count++;
        }
    }

    func is_max(self) -> bool {
        return self.count == 32'hFFFFFFFF;
    }
}
```

生成される SV:

```systemverilog
module Counter (
    input  logic        clk,
    input  logic        rst,
    input  logic        enable,
    output logic [31:0] count
);

always_ff @(posedge clk) begin
    if (rst) begin
        count <= 32'd0;
    end else if (enable) begin
        count <= count + 32'd1;
    end
end

logic is_max;
always_comb begin
    is_max = (count == 32'hFFFFFFFF);
end

endmodule
```

### 4.4 `await` → FSMステート境界

`async func` 内の `await` は「次のクロックエッジまで待機」を意味し、FSMのステート遷移になる。

```cm
impl SpiMaster {
    async func transfer(self) {
        let mut i: uint = 0;
        while i < 8 {
            self.mosi = (self.data_in >> (7 - i)) & 1;
            self.sclk = true;
            await;              // ← ステート境界: S_BIT_HIGH → S_BIT_LOW
            self.sclk = false;
            await;              // ← ステート境界: S_BIT_LOW → S_BIT_HIGH or S_DONE
            i++;
        }
        self.done = true;
    }
}
```

---

## 5. 演算子マッピング

### 算術・ビット演算

| Cm | SV | 備考 |
|----|-----|------|
| `a + b` | `a + b` | |
| `a - b` | `a - b` | |
| `a * b` | `a * b` | 合成ツールが乗算器推論 |
| `a / b` | `a / b` | 合成困難（警告出力） |
| `a % b` | `a % b` | 合成困難（警告出力） |
| `a & b` | `a & b` | ビットAND |
| `a \| b` | `a \| b` | ビットOR |
| `a ^ b` | `a ^ b` | ビットXOR |
| `a << n` | `a << n` | 左シフト |
| `a >> n` | `a >> n` / `a >>> n` | 符号なし/符号付き右シフト |
| `!a` (bool) | `~a` | ビット反転 |

### 比較演算 → if/case条件

| Cm | SV |
|----|-----|
| `a == b` | `a == b` |
| `a != b` | `a != b` |
| `a < b` | `a < b` |
| `a > b` | `a > b` |
| `a <= b` | `a <= b` |
| `a >= b` | `a >= b` |

### キャスト → ビット幅変換

| Cm | SV |
|----|-----|
| `val as utiny` | `val[7:0]` (トランケーション) |
| `val as uint` (拡張) | `{24'b0, val}` (ゼロ拡張) |

---

## 6. アトリビュート体系

プラットフォームディレクティブのプレフィックスは **`sv::` と `verilog::` の両方を使用可能**。コンパイラ内部ではエイリアスとして同一処理する。

```cm
// 以下は等価:
#[sv::module]       struct Counter { ... }
#[verilog::module]  struct Counter { ... }
```

| アトリビュート | エイリアス | 対象 | 効果 |
|--------------|----------|------|------|
| `#[sv::module]` | `#[verilog::module]` | `struct` | SVの`module`として生成 |
| `#[input]` | — | フィールド | 入力ポート |
| `#[output]` | — | フィールド | 出力ポート |
| `#[inout]` | — | フィールド | 双方向ポート |
| `#[sv::clock]` | `#[verilog::clock]` | `bool`フィールド | クロック信号 |
| `#[sv::reset]` | `#[verilog::reset]` | `bool`フィールド | リセット信号 |
| `#[sv::reset_n]` | `#[verilog::reset_n]` | `bool`フィールド | アクティブローリセット |
| `#[sv::wire]` | `#[verilog::wire]` | ローカル変数 | ワイヤ宣言 |
| `#[sv::pin("XX")]` | `#[verilog::pin("XX")]` | ポートフィールド | 物理ピン割当 |
| `#[sv::iostandard("XX")]` | `#[verilog::iostandard("XX")]` | ポートフィールド | I/O規格指定 |
| `#[sv::param(N)]` | `#[verilog::param(N)]` | const | `parameter N = ...` |
| `#[sv::constraints("file")]` | `#[verilog::constraints("file")]` | struct(module) | XDCファイルパス |

---

## 7. SystemVerilog機能カバレッジとロードマップ

### Phase 1: 基盤 (v0.15.0) ✅ 実装済み

| SV機能 | Cmからの生成方法 | ステータス |
|--------|---------------|--------|
| `module` / `endmodule` | `struct` + `async func` / `func` | ✅ 実装済み |
| `input` / `output` | ポート宣言 | ✅ 実装済み |
| `logic [N:0]` | 整数型 (`utiny`/`ushort`/`uint`/`ulong`) | ✅ 実装済み |
| `always_ff` | `async func` | ✅ 実装済み |
| `always_comb` | `func` (pure) | ✅ 実装済み |
| `if` / `else` | `if` / `else` | ✅ 実装済み |
| `assign` | `func` 内の代入 | ✅ 実装済み |
| リテラル (`N'[hbd]val`) | `N'hXX` 記法 | ✅ 実装済み |
| テストベンチ自動生成 | `_tb.sv` 自動出力 | ✅ 実装済み |
| プラットフォームディレクティブ | `//! platform: sv` / `--target=sv` | ✅ 実装済み |

### Phase 2: 制御フロー・FSM (v0.15.x)

| SV機能 | Cmからの生成方法 |
|--------|---------------|
| `case` / `endcase` | `match` |
| `typedef enum` | `enum` |
| FSMステートマシン | `await` in `async func` |
| `for` / `while` (展開) | `for` / `while` |
| モジュールインスタンス化 | struct内struct |
| `parameter` | `#[sv::param]` const |

> **Phase 2到達時点**: ゲーム機用8-bit CPU (6502風) が構築可能

### Phase 3: メモリ・配列インスタンス化 (v0.16.0)

| SV機能 | Cm構文 | 用途 |
|--------|-------|------|
| BRAM推論 | `#[sv::bram] mem: utiny[32768]` | VideoRAM, 命令ROM |
| 分散RAM | `#[sv::lutram] mem: utiny[64]` | レジスタファイル |
| `generate` / `genvar` | `cores: ComputeCore[N]` | 配列インスタンス化 |
| テストベンチ自動生成 | `#[sv::testbench]` | 自動検証 |
| XDC/SDC 制約ファイル | `#[sv::pin("XX")]` | FPGAピン割当 |

```cm
// Phase 3: BRAM 推論
#[sv::module]
struct VideoRAM {
    #[input]  clk: bool,
    #[input]  addr: bit[15],
    #[input]  write_data: utiny,
    #[input]  write_enable: bool,
    #[output] read_data: utiny,

    #[sv::bram]
    mem: utiny[32768],  // → (* ram_style = "block" *) logic [7:0] mem [0:32767]
}

// Phase 3: 配列インスタンス化
#[sv::module]
struct MultiCoreProcessor<const N: uint = 4> {
    cores: ComputeCore[N],  // → generate for (...) ComputeCore core_i(...)
}
```

> **Phase 3到達時点**: 16-bit ゲーム機 (SFC風) / RISC-V RV32I CPU が構築可能

### Phase 4: マルチクロック・バスインターフェース (v0.16.x)

| SV機能 | Cm構文 | 用途 |
|--------|-------|------|
| 複数クロックドメイン | `#[sv::clock_domain(mem_clk)]` | GPU/SoC |
| CDC同期プリミティブ | ビルトイン `SyncFF<T>`, `AsyncFIFO<T>` | クロック間データ転送 |
| `interface` | `#[sv::interface] interface AXI4 {...}` | バスプロトコル定義 |
| 外部IPブラックボックス | `#[sv::external_ip("xilinx_mig")]` | DDR/PCIe IP接続 |

```cm
// Phase 4: 複数クロックドメイン
#[sv::module]
struct SoC {
    #[input] #[sv::clock] core_clk: bool,   // 100MHz
    #[input] #[sv::clock] mem_clk: bool,    // 200MHz

    cpu: CPU,
    dram: DRAMController,
}

impl SoC {
    #[sv::clock_domain(core_clk)]
    async func cpu_tick(self) { /* core_clk で動作 */ }

    #[sv::clock_domain(mem_clk)]
    async func mem_tick(self) { /* mem_clk で動作 */ }
}

// Phase 4: AXIバスインターフェース
#[sv::interface]
interface AXI4Lite {
    awaddr:  bit[32],
    awvalid: bool,
    awready: bool,
    wdata:   bit[32],
    wvalid:  bool,
    wready:  bool,
    bresp:   bit[2],
    bvalid:  bool,
    bready:  bool,
    // ... Read channels
}
```

> **Phase 4到達時点**: 小規模GPU (16コア) / SoC が構築可能

### Phase 5: 高度な合成最適化 (v0.17.0+)

| SV機能 | Cm構文 | 用途 |
|--------|-------|------|
| パイプライン自動挿入 | `#[sv::pipeline(stages=3)]` | 高周波数化 |
| リソース共有 | `#[sv::share]` | 乗算器/除算器の共有 |
| ストリームインターフェース | `#[sv::stream]` | データフロー |
| FIFOバッファ | ビルトイン `FIFO<T, DEPTH>` | ステージ間バッファ |

```cm
// Phase 5: パイプライン自動挿入
impl ALU {
    #[sv::pipeline(stages = 3)]
    func multiply(self) -> uint {
        return self.a * self.b;  // 3段パイプラインに自動分割
    }
}
```

> **Phase 5到達時点**: 大規模GPGPU (数百コア) が構築可能

---

### ユースケース別 到達可能Phase

| プロジェクト | 必要Phase | 主要依存機能 |
|------------|:---------:|------------|
| LED点滅 / 7セグ表示 | **Phase 1** | module, always_ff |
| UART通信モジュール | **Phase 2** | FSM (await), case |
| 8-bit ゲーム機CPU (6502風) | **Phase 2** | FSM, enum, match |
| SPI/I2C コントローラ | **Phase 2** | FSM |
| 16-bit ゲーム機 (SFC風) | **Phase 3** | BRAM (VideoRAM), generate |
| RISC-V RV32I CPU | **Phase 3** | BRAM (命令/データ), generate |
| カスタムSoC | **Phase 4** | 複数クロック, AXI, IP統合 |
| 小規模GPU (16コア) | **Phase 4** | generate, 共有メモリ, CDC |
| 大規模GPGPU (数百コア) | **Phase 5** | パイプライン, ストリーム, AXI |

---

## 8. マルチファイル出力と import/export マッピング

### 8.1 方針

SystemVerilog では各 `module` を独立した `.sv` ファイルに格納し、合成ツールがファイル一覧を受け取って参照を解決する。CmのSVバックエンドはこの慣習に従い、**1モジュール = 1ファイル** を原則とする。

```
Cm ソース                         SV 出力
─────────                         ────────
top.cm                     →      Top.sv
├── import ./cpu/core;     →      Core.sv
├── import ./gpu/shader;   →      Shader.sv
└── import ./mem/ram;      →      BlockRAM.sv
```

### 8.2 Cm `import` → SV モジュール参照

Cmの `import` はプリプロセッサがソースを結合する仕組み。SVバックエンドではこれを**モジュールインスタンス化の依存関係**として解釈する。

```cm
// top.cm
import ./cpu/core;        // Core モジュールを使用
import ./peripheral/uart; // UartTx モジュールを使用

#[sv::module]
struct Top {
    #[input] clk: bool,
    #[input] rst: bool,
    #[output] tx: bool,

    cpu: Core,       // import した struct をサブモジュールとして配置
    uart: UartTx,
}
```

```cm
// cpu/core.cm
export Core;  // 外部から参照可能にする

#[sv::module]
struct Core {
    #[input]  clk: bool,
    #[input]  rst: bool,
    #[output] mem_addr: bit[16],
    #[output] mem_data: utiny,
}
```

**生成ファイル**:

```
output/
├── Top.sv         # module Top (... Core core_inst (...), UartTx uart_inst (...) ...)
├── Core.sv        # module Core (...)
├── UartTx.sv      # module UartTx (...)
└── filelist.f     # Verilator/Vivado 用ファイルリスト
```

### 8.3 `export` → SV公開ルール

| Cm構文 | SV効果 |
|--------|-------|
| `export StructName;` | 別ファイルに `module StructName` を出力、他モジュールからインスタンス化可能 |
| `export func_name;` | SVでは不要（モジュール内に含まれる） |
| `export const FOO;` | `parameter FOO = ...` をモジュール外に公開 |
| export なし | モジュール内部のローカルロジックとして展開（ファイル分割しない） |

### 8.4 ファイルリスト生成

合成ツールやVerilatorは複数ファイルの依存順を指定する必要がある。
SV バックエンドは **`filelist.f`** を自動生成する。

```
// filelist.f (依存順 = リーフモジュールから)
Core.sv
UartTx.sv
BlockRAM.sv
Top.sv
```

```bash
# Verilator: ファイルリストで検証
verilator --lint-only -Wall --timing -f output/filelist.f

# Vivado: ファイルリストで合成
vivado -mode batch -source synth.tcl  # read_verilog [glob output/*.sv]
```

### 8.5 出力モード

```bash
# 単一ファイル出力（デフォルト: 全モジュールを1ファイルに）
cm compile --target=sv top.cm -o top.sv

# マルチファイル出力（ディレクトリ指定: 1モジュール1ファイル）
cm compile --target=sv top.cm -o output/
# → output/Top.sv, output/Core.sv, output/UartTx.sv, output/filelist.f

# ファイルリストのみ出力
cm compile --target=sv --filelist top.cm -o output/filelist.f
```

### 8.6 import / 依存解決フロー

```
1. Cmプリプロセッサが import を解決（既存処理）
     ↓
2. 各ソースファイルから #[sv::module] struct を収集
     ↓
3. struct 内のフィールド型を走査し、依存グラフを構築
   - フィールド型が他の #[sv::module] struct → サブモジュール依存
     ↓
4. トポロジカルソートで出力順を決定
     ↓
5. 各モジュールを個別 .sv ファイルに出力 (マルチファイルモード)
   または全モジュールを1ファイルに結合 (単一ファイルモード)
     ↓
6. filelist.f を依存順で生成
```

### 8.7 循環依存の検出

SVモジュールの循環依存は合成不可能。コンパイル時にエラーを出す。

```
error[SV001]: Circular module dependency detected
  --> top.cm:5
  |
  | Core → ALU → Core
  |
  = help: Break the cycle by extracting shared logic into a separate module
```

---

## 9. SV出力の検証パイプライン

### 9.1 検証の考え方

生成されたSVが**文字列として期待通り**であることと、**実際にビルドが通る（合成可能な有効なSV）** であることは別問題。
CmのSVバックエンドテストは以下の3段階で検証する:

```
Stage 1: Cm → SV 生成         ← コンパイラが正しくSVを出力するか
Stage 2: SV → ビルド検証       ← 生成されたSVが有効なSystemVerilogか ★重要★
Stage 3: SV → シミュレーション  ← 生成された回路が正しく動作するか
```

### 9.2 Stage 1: SV出力生成テスト (CI必須)

```bash
# Cm → SV コンパイル
cm compile --target=sv tests/sv/basic/counter.cm -o /tmp/counter.sv
echo $?  # 0 = コンパイル成功
```

**成功判定**: `cm compile --target=sv` が exit code 0 で `.sv` ファイルを出力する。

### 9.3 Stage 2: SVビルド検証 (CI必須) ★核心★

生成された `.sv` が文法的に正しく、合成可能であることを外部ツールで検証する。

#### 検証ツール

| ツール | 用途 | インストール |
|-------|------|------------|
| **Verilator** (推奨) | lint + 合成チェック | `brew install verilator` / `apt install verilator` |
| **iverilog** (代替) | 構文チェック | `brew install icarus-verilog` / `apt install iverilog` |

#### Verilator lint モード

```bash
# 構文 + 合成可能性チェック（シミュレーションなし）
verilator --lint-only -Wall --timing counter.sv
# exit code 0 = 有効なSV
# exit code ≠ 0 = 構文エラーまたは合成不可
```

#### iverilog コンパイルモード

```bash
# SystemVerilog 2012 として構文チェック
iverilog -g2012 -o /dev/null counter.sv
# exit code 0 = 構文OK
```

#### unified_test_runner.sh への統合

```bash
sv)
    local sv_file="$TEMP_DIR/sv_${test_name}.sv"
    rm -f "$sv_file"

    local test_dir="$(dirname "$test_file")"
    local test_basename="$(basename "$test_file")"

    # Step 1: Cm → SV コンパイル
    (cd "$test_dir" && run_with_timeout "$CM_EXECUTABLE" compile \
        --target=sv "$test_basename" -o "$sv_file" > "$output_file" 2>&1) || exit_code=$?

    if [ $exit_code -eq 0 ] && [ -f "$sv_file" ]; then
        # Step 2: SVビルド検証 (Verilator or iverilog)
        if command -v verilator >/dev/null 2>&1; then
            verilator --lint-only -Wall --timing "$sv_file" >> "$output_file" 2>&1
            exit_code=$?
            if [ $exit_code -ne 0 ]; then
                echo "VERILATOR_LINT_FAIL" >> "$output_file"
            fi
        elif command -v iverilog >/dev/null 2>&1; then
            iverilog -g2012 -o /dev/null "$sv_file" >> "$output_file" 2>&1
            exit_code=$?
            if [ $exit_code -ne 0 ]; then
                echo "IVERILOG_COMPILE_FAIL" >> "$output_file"
            fi
        else
            echo -e "${YELLOW}[WARN]${NC} verilator/iverilog not found, skip build verification"
        fi

        # expectファイルとの比較用にSV内容を出力
        if [ $exit_code -eq 0 ]; then
            echo "COMPILE_OK" > "$output_file"
        fi
    fi
    ;;
```

#### .expect ファイルの形式

```
COMPILE_OK
```

SVバックエンドのテストは **ビルド成功(`COMPILE_OK`)を期待出力**とする。
これはUEFI/baremetalテストと同じパターン。

特定のSVコード内容を検証したい場合は `.expect.sv` ファイルで別途diff比較も可能。

### 9.4 Stage 3: シミュレーション検証 (CI optional / 手動)

テストベンチ付きの回路動作シミュレーション。Phase 2以降で対応。

```bash
# Verilator C++シミュレーション
verilator --cc counter.sv --exe counter_tb.cpp --timing
make -C obj_dir -f Vcounter.mk
./obj_dir/Vcounter    # シミュレーション実行

# iverilog シミュレーション
iverilog -g2012 -o sim counter.sv counter_tb.sv
vvp sim              # 波形出力
```

### 9.5 テストファイル構成

```
tests/sv/
├── basic/                      # 基本回路テスト (20件)
│   ├── counter.cm / counter.expect
│   ├── adder.cm / adder.expect       # SIM_OK + TEST行
│   ├── mux.cm / mux.expect           # SIM_OK + TEST行
│   ├── sv_width_literal.cm           # N'hXX リテラル
│   └── ...
├── control/                    # 制御フロー
│   ├── compare.cm / compare.expect
│   ├── nested_if.cm / nested_if.expect
│   └── ...
└── advanced/                   # 高度なパターン
    ├── led_blinker.cm / led_blinker.expect
    ├── posedge_counter.cm / posedge_counter.expect
    └── negedge_reset.cm / negedge_reset.expect
```

> **注**: `tests/programs/sv/` は廃止。全テストは `tests/sv/` に統合済み。

### 9.6 Makefile統合

```makefile
# Cm ルートMakefileに追加 (既存パターンに合わせて)
tsvp0: ; OPT_LEVEL=0 ./tests/unified_test_runner.sh -b sv -p
tsvp1: ; OPT_LEVEL=1 ./tests/unified_test_runner.sh -b sv -p
tsvp2: ; OPT_LEVEL=2 ./tests/unified_test_runner.sh -b sv -p
tsvp3: ; OPT_LEVEL=3 ./tests/unified_test_runner.sh -b sv -p
tsvp:  tsvp3  # デフォルトはO3
```

### 9.7 CIパイプライン

```yaml
# .github/workflows/sv-tests.yml
sv-backend:
  runs-on: ubuntu-latest
  steps:
    - uses: actions/checkout@v4
    - name: Install Verilator
      run: sudo apt-get install -y verilator
    - name: Build Cm
      run: make install
    - name: SV Backend Tests (compile + verilator lint)
      run: make tsvp
```

### 9.8 CI環境のツール要件

| ツール | 必須度 | ない場合の挙動 |
|-------|-------|-------------|
| `cm` | **必須** | テスト不可 |
| `verilator` | **推奨** | Stage 2スキップ（警告出力） |
| `iverilog` | 代替 | verilator不在時に使用 |

> **方針**: CIでは `verilator` を必ずインストールし、Stage 2を**必須**とする。
> ローカル開発では verilator/iverilog がなくてもテスト自体はスキップ（WARN）で継続。

---

## 10. コンパイルパイプライン

```
Cm ソース (.cm)
    ↓
Lexer → Parser → TypeChecker (既存)
    ↓
HIR → MIR (既存変換)
  ※ ++/-- は MIR loweringで var = var ± 1 に展開済み
    ↓
【新規】SV解析パス
  - #[sv::module] struct → SVModule抽出
  - async func → always_ff フラグ
  - func → always_comb フラグ
  - await → FSMステート境界 (Phase 2)
  - 型検証 (ポインタ/string/float → エラー)
    ↓
【新規】MIR → SV コード生成
  - BufferedCodeGenerator 基盤を再利用
  - module/port/always ブロック出力
  - bit<N> リテラル変換 (N'hXX)
    ↓
SystemVerilog (.sv) 出力
    ↓ (Phase 3)
XDC/SDC (.xdc) 出力 (ピン割当)
```

### ターゲット指定

```bash
cm compile --target=sv counter.cm -o counter.sv
cm compile --target=sv --constraints top.cm     # XDCも同時生成
```
