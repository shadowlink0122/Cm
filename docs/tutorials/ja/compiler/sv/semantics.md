---
title: SVバックエンド - 意味論保証
parent: Tutorials
nav_order: 17
---

[English](../../../en/compiler/sv/semantics.html)

# SVバックエンド - 意味論保証

Cm の SV バックエンドは「Cm で書いたロジックが、生成された SystemVerilog でも同じ意味で動く」ことを保証するように設計されています。このページでは、v0.15.1（2026-07-04 更新）で強化された意味論の対応関係と、押さえておくべき変換規則を解説します。

基本的な使い方は [SystemVerilogバックエンド](index.html) を参照してください。

---

## 1. 演算子の優先順位は Cm ソースの構造どおり

SystemVerilog では `==` が `&` より優先されるため、括弧なしの `a & 256 == 0` は
`a & (256 == 0)` と解釈されます。Cm コンパイラは式の構造を保持し、必要な括弧を必ず出力します。

```cm
if ((r_qm & 256) == 0) { ... }
```

```systemverilog
// 生成されるSV: 括弧が保持される
if (((r_qm & 32'd256) == 32'd0)) begin ... end
```

書いたとおりの評価順序になるため、TMDS エンコーダのようなビット演算の多いロジックも安心して記述できます。

## 2. 符号付き演算は Cm / LLVM と同一意味論

### 算術右シフト

Cm の `>>` は符号付き型では算術シフトです（LLVM バックエンドの `ashr` と同じ）。
SV の `>>` は常に論理シフトのため、符号付きオペランドには `>>>` が出力されます。

```cm
int s = -8;
int r = s >> 2;   // -2（算術シフト）
```

```systemverilog
shifted <= s >>> 32'sd2;  // 算術シフト
```

### 符号付き定数

SV では比較の片方が unsigned だと **比較全体が unsigned** になります。
Cm は定数を型に従って出力するため、`s < 0` のような負数判定が正しく動作します。

```cm
if (s < 0) { neg = 1; }   // int s
```

```systemverilog
if ((s < 32'sd0)) begin ... end  // 'sd = 符号付き10進
```

## 3. `as` キャストはサイズキャストとして出力

式の途中の縮小キャストは、SV のサイズキャスト `N'(expr)` として明示的に出力されます。
符号が変わる場合は `$signed()` / `$unsigned()` も併用されます。

```cm
wide = ((a + 300) as utiny) + 1000;  // 8bitに切り詰めてから加算
```

```systemverilog
wide <= 8'((a + 32'd300)) + 32'd1000;  // a=0 なら 44 + 1000 = 1044
```

## 4. 変数の初期値は電源投入時初期値になる

モジュールレベル変数の宣言初期値は、SV のレジスタ宣言初期値として出力されます。
FPGA 合成では初期値として扱われ、シミュレーションでは X 伝播を防ぎます。

```cm
uint state = 0;
uint counter = 42;
```

```systemverilog
logic [31:0] state = 32'd0;
logic [31:0] counter = 32'd42;
```

これにより、生成された SV は iverilog / Verilator で **そのままシミュレーション可能**です。

## 5. enum は明示タグ値から幅を計算

```cm
enum Status {
    IDLE = 0,
    ERROR = 100
}
```

```systemverilog
typedef enum logic [6:0] {  // 100を表現できる7bit幅
    IDLE = 7'd0,
    ERROR = 7'd100
} Status;
```

## 6. 配列型ポートはアンパックド次元を保持

```cm
#[output] uint[4] data;
```

```systemverilog
output logic [31:0] data [0:3]  // 次元が保持される
```

---

## 変換規則早見表

| Cm | 生成SV | 備考 |
|----|--------|------|
| `bool` | `logic` | |
| `int` / `uint` | `logic signed [31:0]` / `logic [31:0]` | tiny/short/longも同様の幅で対応 |
| `s >> n`（符号付き） | `s >>> n` | 算術シフト |
| `x as utiny`（式中） | `8'(x)` | サイズキャスト |
| `int as uint` 等の符号変更 | `$unsigned(...)` / `$signed(...)` | |
| 符号付き定数 | `32'sd5` 等 | unsigned比較化を防止 |
| `uint x = 42;` | `logic [31:0] x = 32'd42;` | 電源投入時初期値 |
| `uint[N]` ポート | `logic [31:0] name [0:N-1]` | |
| `async void f(posedge clk)` | `always @(posedge clk)` | |
| `string` 定数 + インデックス | packedベクトル + パートセレクト | `TITLE[(L-1-i)*8 +: 8]` |

## テストによる保証

これらの意味論は `tests/sv/` のシミュレーション付き回帰テスト
（iverilog + vvp による値検証）で継続的に確認されています:

- `basic/precedence_mask` — 優先順位の括弧保持
- `basic/cast_truncate` — 式中の縮小キャスト
- `control/signed_shift` / `control/signed_const_cmp` — 符号付きシフト・比較
- `control/for_loop` / `control/loop_break` / `control/nested_loop` — whileループ再構成・break・ネスト
- `advanced/enum_explicit` / `advanced/reg_init` — enum明示値・初期値
- `memory/array_port` — 配列ポート

---

<!-- nav -->
← 前: [SVバックエンド - 状態初期化とシミュレーション](state-sim.html) ｜ [目次](index.html) ｜ 次: [内部構造編](../../internals/index.html) →
