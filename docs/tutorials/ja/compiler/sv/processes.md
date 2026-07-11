---
title: SVバックエンド - プロセスと代入
parent: Tutorials
nav_order: 13
---

[English](../../../en/compiler/sv/processes.html)

# SVバックエンド - プロセスと代入

[SystemVerilogバックエンド](index.html) の詳細ページです。alwaysブロックの生成規則と、代入・暗黙的変換のルールを解説します。

---

## ロジックブロック

### 順序回路 (always_ff)

#### パターンA: `always` + エッジパラメータ （推奨）

```cm
always void counter_tick(posedge clk) {
    count = count + 1;
}
// → always @(posedge clk) begin
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
// → always @(posedge clk or negedge rst_n) begin ...
```

#### パターンC: `void f(posedge clk)` （後方互換）

```cm
void blink(posedge clk) {
    led = !led;
}
// → always @(posedge clk) begin led <= ~led; end
```

#### パターンD: `async func` （後方互換）

```cm
async func tick() {
    counter = counter + 1;
}
// → always @(posedge clk) begin counter <= counter + 32'd1; end
```

> **注意:** `async func` は暗黙的に `clk` 変数を参照します。
> `clk` が未宣言の場合、自動的に `input logic clk` が追加されます。

### 組み合わせ回路 (always_comb)

エッジパラメータなしのvoid関数:

```cm
always void decode() {
    out = 0;
    if (sel) { out = a; }
    else { out = b; }
}
// → always_comb begin ... end
```

後方互換: `void f()` / `func f()` も `always_comb` に変換されます。

### function

引数あり（edgeパラメータなし）かつ **非void（戻り値あり）** の関数は、自動的に SV `function automatic` に変換されます:

```cm
uint max_val(uint x, uint y) {
    if (x > y) { return x; }
    return y;
}
// → function automatic logic [31:0] max_val(...); ... endfunction
```

---

## 代入の自動変換ルール

| ブロック種別 | Cmでの記述 | SV出力 |
|------------|----------|--------|
| `always_ff` (順序回路) | `x = expr;` | `x <= expr;` (ノンブロッキング) |
| `always_comb` (組み合わせ) | `x = expr;` | `x = expr;` (ブロッキング) |

Cmでは常に `=` で記述し、コンパイラが文脈に応じて適切な代入方式を選択します。

---

## 暗黙的変換

SVバックエンドは、正しいSVコードを自動生成するために多数の暗黙的変換を行います。

### 論理否定の変換

| Cm | SV | 理由 |
|----|----|----|
| `!flag` | `~flag` | 多ビット信号に安全な `~` に統一（`!` は型検査でbool限定） |

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

MIRの `_tXXXX` 一時変数は元の式にインライン展開されます。展開時には**演算子の優先順位を考慮した括弧付与**が行われます:

```
MIR:  _t1000 = a & 256; _t1001 = _t1000 == 0;
SV:   if (((a & 32'd256) == 32'd0))   // 括弧が保持される
```

> whileループの条件のように複数回代入される一時変数は展開されず、
> レジスタとして残ります（[制御構文とループ](control-flow.html)参照）。

### その他

- **`self.` プレフィックスの除去**: `self.counter` → `counter`
- **`else if` の正規化**: ネストした `else { if ... }` を `else if` にフラット化
- **冗長な三項演算子の除去**: `cond ? x : x` → `x`

---

<!-- nav -->
← 前: [SVバックエンド - 型とポート](types.html) ｜ [目次](index.html) ｜ 次: [SVバックエンド - 制御構文とループ](control-flow.html) →
