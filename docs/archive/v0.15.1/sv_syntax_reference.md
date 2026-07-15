# SystemVerilog バックエンド 構文・トークン リファレンス

本ドキュメントは、Cmコンパイラの SV バックエンド (`codegen/sv/codegen.cpp`) が**出力する全SV構文** と、それに対応する **Cmトークン/型** を網羅的に列挙する。

---

## 1. モジュール構造体

### 出力される SV 構文

| SV構文 | 生成元 | 例 |
|--------|--------|-----|
| `module <name> (...)` | ソースファイル名 | `module blink (...)` |
| `endmodule` | 自動 | |
| `` `timescale 1ns / 1ps `` | ファイルヘッダ | |
| `input logic [N:0] <name>` | `#[input]` 属性 | `input logic clk` |
| `output logic [N:0] <name>` | `#[output]` 属性 | `output logic [7:0] led` |
| `inout logic [N:0] <name>` | `#[inout]` 属性 | `inout logic [15:0] data` |
| `localparam <type> <name> = <val>;` | `const` 宣言 | `localparam logic [31:0] WIDTH = 32'd8;` |

---

## 1.1 import/export (モジュール分割)

Cm の `import`/`export` キーワードを使って、SV ターゲットでもモジュール間で定数・関数を共有できます。

### 定数のエクスポート

```cm
// vga_timing.cm
export const uint H_ACTIVE = 640;
export const uint H_TOTAL  = 800;
```

### 関数のエクスポート

```cm
// alu_lib.cm
export uint add(uint a, uint b) {
    return a + b;
}
```

### インポート (全シンボル)

```cm
//! platform: sv
import vga_timing;
import alu_lib;
```

### 選択的インポート

```cm
//! platform: sv
import vga_timing::{H_ACTIVE, H_TOTAL};
import alu_lib::{add};
```

### SV バックエンドの自動処理

| 処理 | 内容 |
|------|------|
| localparam 重複排除 | namespace 内コピーと exported symbols コピーの重複を自動検出・除外 |
| namespace:: フラット化 | `alu_lib::add` → `add` (SV の function 名に `::` は使えない) |
| ローカル変数フィルタリング | function 内にインポートされたグローバル定数が混入するのを防止 |



## 2. 型マッピング

| Cm型 | TypeKind | SV出力 | ビット幅 |
|------|----------|--------|---------|
| `bool` | `Bool` | `logic` | 1 |
| `tiny` | `Tiny` | `logic signed [7:0]` | 8 |
| `utiny` | `UTiny` | `logic [7:0]` | 8 |
| `short` | `Short` | `logic signed [15:0]` | 16 |
| `ushort` | `UShort` | `logic [15:0]` | 16 |
| `int` | `Int` | `logic signed [31:0]` | 32 |
| `uint` | `UInt` | `logic [31:0]` | 32 |
| `long` | `Long` | `logic signed [63:0]` | 64 |
| `ulong` | `ULong` | `logic [63:0]` | 64 |
| `isize` | `ISize` | `logic signed [63:0]` | 64 |
| `usize` | `USize` | `logic [63:0]` | 64 |
| `posedge` | `Posedge` | `logic` (1-bit) | 1 |
| `negedge` | `Negedge` | `logic` (1-bit) | 1 |
| `wire<T>` | `Wire` | `mapType(T)` | T依存 |
| `reg<T>` | `Reg` | `mapType(T)` | T依存 |

### 非合成型 (SV00x エラー)

以下の型は SV バックエンドでコンパイルエラーとなる:
- `float`, `double`, `ufloat`, `udouble` — 浮動小数点
- `string`, `cstring` — 文字列
- `*T` (Pointer), `&T` (Reference) — ポインタ/参照

---

## 3. ロジックブロック生成

### 3.1 `always_ff` (順序回路)

| Cmパターン | SV出力 |
|-----------|--------|
| `void f(posedge clk) {...}` | `always_ff @(posedge clk) begin ... end` |
| `void f(negedge rst) {...}` | `always_ff @(negedge rst) begin ... end` |
| `async func f() {...}` | `always_ff @(posedge clk) begin ... end` |
| `#[sv::clock_domain("fast")] async func f() {...}` | `always_ff @(posedge fast) begin ... end` |

**代入**: ノンブロッキング `<=`

### 3.2 `always_comb` (組み合わせ回路)

| Cmパターン | SV出力 |
|-----------|--------|
| `void f() {...}` (トリガなし、非async) | `always_comb begin ... end` |
| `func f() {...}` | `always_comb begin ... end` |

**代入**: ブロッキング `=`

### 3.3 `assign` (連続代入)

現時点では `assign` 文は属性ベースで生成されない。将来のサポート候補。

---

## 4. 二項演算子マッピング

| Cm演算子 | MIR Op | SV出力 |
|---------|--------|--------|
| `+` | `Add` | `+` |
| `-` | `Sub` | `-` |
| `*` | `Mul` | `*` |
| `/` | `Div` | `/` |
| `%` | `Mod` | `%` |
| `&` | `BitAnd` | `&` |
| `\|` | `BitOr` | `\|` |
| `^` | `BitXor` | `^` |
| `<<` | `Shl` | `<<` |
| `>>` | `Shr` | `>>` |
| `==` | `Eq` | `==` |
| `!=` | `Ne` | `!=` |
| `<` | `Lt` | `<` |
| `<=` | `Le` | `<=` |
| `>` | `Gt` | `>` |
| `>=` | `Ge` | `>=` |
| `&&` | `And` | `&&` |
| `\|\|` | `Or` | `\|\|` |

---

## 5. 単項演算子マッピング

| Cm演算子 | MIR Op | SV出力 |
|---------|--------|--------|
| `-x` | `Neg` | `-x` |
| `!x` | `Not` | `~x` |
| `~x` | `BitNot` | `~x` |

> [!NOTE]
> Cmの `!` (論理否定) と `~` (ビット反転) は、SVでは両方 `~` にマッピングされる。
> SVの `!` は1ビット論理否定だが、現在のバックエンドは `~` に統一している。

---

## 6. 定数リテラル

| Cmリテラル | SV出力例 |
|-----------|---------|
| `true` | `1'b1` |
| `false` | `1'b0` |
| `42` (uint ctx) | `32'd42` |
| `42` (utiny ctx) | `8'd42` |
| `42` (signed int ctx) | `32'sd42` |
| `-5` | `-32'sd5` |
| `8'b10101010` | `8'b10101010` |
| `16'hFF00` | `16'hFF00` |

---

## 7. 制御構文

| Cm構文 | SV出力 |
|-------|--------|
| `if (cond) {...}` | `if (cond) begin ... end` |
| `if (cond) {...} else {...}` | `if (cond) begin ... end else begin ... end` |
| `if ... else if ...` | `if ... end else if ...` (正規化) |
| `switch (val) { case X: ... }` | `case (val) X: begin ... end endcase` |

---

## 8. 宣言構文

| SV出力 | 生成条件 |
|--------|---------|
| `logic [N:0] <name>;` | 内部レジスタ (属性なしグローバル変数 / 関数ローカル変数) |
| `(* ram_style = "block" *)` | `#[sv::bram]` 属性 |
| `(* ram_style = "distributed" *)` | `#[sv::lutram]` 属性 |

---

## 9. SV固有トークン (token.hpp)

| トークン | キーワード | TypeKind | 用途 |
|---------|---------|----------|------|
| `KwPosedge` | `posedge` | `Posedge` | 立ち上がりエッジクロック |
| `KwNegedge` | `negedge` | `Negedge` | 立ち下がりエッジクロック |
| `KwWire` | `wire` | `Wire` | ワイヤ修飾型 |
| `KwReg` | `reg` | `Reg` | レジスタ修飾型 |
| `KwAlways` | `always` | - | ロジックブロック修飾子（自動判別） |
| `KwAlwaysFF` | `always_ff` | - | 順序回路（明示指定） |
| `KwAlwaysComb` | `always_comb` | - | 組み合わせ回路（明示指定） |
| `KwAlwaysLatch` | `always_latch` | - | ラッチ（明示指定） |
| `KwAssign` | `assign` | - | 連続代入文 |
| `KwInitial` | `initial` | - | シミュレーション初期化 (未実装) |
| `KwBit` | `bit` | - | 任意ビット幅型 `bit[N]` |

---

## 10. SV属性 (Attribute)

| Cm属性 | SV効果 |
|-------|--------|
| `#[input]` | 入力ポート宣言 |
| `#[output]` | 出力ポート宣言 |
| `#[inout]` | 双方向ポート宣言 |
| `#[sv::param]` | parameter宣言 |
| `#[sv::bram]` | `(* ram_style = "block" *)` |
| `#[sv::lutram]` | `(* ram_style = "distributed" *)` |
| `#[sv::pipeline]` | 合成コメント出力 |
| `#[sv::share]` | リソース共有コメント |
| `#[sv::clock_domain("name")]` | async funcのクロック指定 |
| `#[sv::pin("XX")]` | XDCピン割当 |
| `#[sv::iostandard("YY")]` | XDC IO標準 |

---

## 11. SV予約語 (モジュール名回避)

```
output, input, inout, module, wire, reg, logic, begin, end,
if, else, for, while, case, default, assign, always, initial,
posedge, negedge, task, function, parameter, integer, real, time, event
```

---

## 12. テストベンチ自動生成

`generateTestbench()` が出力する構文:

| SV構文 | 用途 |
|-------|------|
| `module <name>_tb;` | テストベンチモジュール |
| `reg` | 入力信号宣言 |
| `wire` | 出力信号宣言 |
| `<module> uut(...)` | DUTインスタンス化 |
| `initial begin ... $finish; end` | テストシーケンス |
| `always #10 clk = ~clk;` | クロック生成 |
| `$dumpfile / $dumpvars` | 波形ダンプ |
| `$monitor` | 信号モニタリング |
