# Cm ⇔ SystemVerilog マッピング対応表

Cmの構文要素がSVバックエンドでどのように変換されるかの完全な対応表。

---

## 1. 関数 → ブロック マッピング

| Cm構文 | `is_async` | トリガパラメータ | SV出力 | 代入方式 |
|-------|------------|----------------|--------|---------|
| `void f(posedge clk) {...}` | N/A | `posedge clk` | `always_ff @(posedge clk)` | `<=` |
| `void f(negedge rst) {...}` | N/A | `negedge rst` | `always_ff @(negedge rst)` | `<=` |
| `async func f() {...}` | `true` | なし | `always_ff @(posedge clk)` | `<=` |
| `void f() {...}` | `false` | なし | `always_comb` | `=` |
| `func f() {...}` | `false` | なし | `always_comb` | `=` |

> [!IMPORTANT]
> **`async` キーワードの二重意味**: `async` は元々 JavaScript バックエンド用の非同期関数マーカー。
> SV バックエンドではこれを `always_ff` 生成のトリガとして流用している。
> MIR の `is_async` フラグが両バックエンドで異なる意味を持つ。

---

## 2. 変数宣言マッピング

| Cm宣言 | 属性 | SV出力 |
|-------|------|--------|
| `#[input] posedge clk;` | `input` | `input logic clk` (ポート) |
| `#[input] bool rst = false;` | `input` | `input logic rst` (ポート) |
| `#[output] utiny led = 0xFF;` | `output` | `output logic [7:0] led` (ポート) |
| `#[inout] uint data;` | `inout` | `inout logic [31:0] data` (ポート) |
| `#[sv::param] uint WIDTH = 8;` | `sv::param` | `parameter WIDTH = 32'd8;` |
| `uint counter = 0;` | なし | `logic [31:0] counter;` (内部レジスタ) |

---

## 3. SV構文のうちCmに対応がないもの

以下のSV構文は、現在のCmバックエンドでは**生成されない**:

### 3.1 生成されないSVブロック

| SV構文 | 説明 | 現状 |
|--------|------|------|
| `function ... endfunction` | 組み合わせロジック関数 | Cm `func` → `always_comb` に変換 |
| `task ... endtask` | 手続き的タスク | 未サポート |
| `initial begin ... end` | シミュレーション初期化 | テストベンチのみ |
| `generate ... endgenerate` | パラメトリック生成 | 未サポート |
| `always @(*)` | 旧来の組み合わせ | `always_comb` を使用 |
| `always @(posedge ... or negedge ...)` | 非同期リセット | 未サポート |
| `assign wire = expr;` | 連続代入 | 未サポート |

### 3.2 生成されないSVデータ型

| SV構文 | 説明 | 現状 |
|--------|------|------|
| `integer` | 32-bit符号付き (旧) | `logic signed [31:0]` を使用 |
| `real` | 浮動小数点 | 非合成 → エラー |
| `bit` | 2-state (0/1のみ) | `logic` (4-state) を使用 |
| `byte` | 8-bit符号付き | `logic signed [7:0]` を使用 |
| `shortint` | 16-bit符号付き | `logic signed [15:0]` を使用 |
| `longint` | 64-bit符号付き | `logic signed [63:0]` を使用 |
| `struct packed {...}` | パックド構造体 | 未サポート |
| `enum {...}` | 列挙型 | 未サポート |
| `typedef` | 型エイリアス | 未サポート |

### 3.3 生成されないSV演算子/構文

| SV構文 | 説明 | 現状 |
|--------|------|------|
| `{a, b}` | 連接 (concatenation) | 未サポート |
| `{N{expr}}` | 複製 (replication) | 未サポート |
| `a ? b : c` | 三項演算子 | MIRのSwitchIntで分岐化 |
| `$clog2(N)` | システム関数 | 未サポート |
| `for (;;)` | forループ | 未サポート (静的展開のみ) |
| `localparam` | ローカルパラメータ | `parameter` のみ |

---

## 4. Cmキーワードの SV バックエンドでの意味変化

| Cmキーワード | 通常(LLVM)の意味 | SVバックエンドの意味 |
|-------------|-----------------|-------------------|
| `async` | JS非同期関数 | `always_ff` ブロック生成 |
| `func` | 関数宣言 (戻り値推論) | `always_comb` ブロック生成 |
| `void` | 戻り値なし関数 | ブロック生成 (ff/comb) |
| `=` | 変数代入 | ff内: `<=`, comb内: `=` |
| `!` | 論理否定 | `~` (ビット反転に統合) |
| `struct` | 構造体定義 | **未サポート** |
| `enum` | 列挙型定義 | **未サポート** |
| `for` | ループ | **未サポート** (将来: generate for?) |
| `match` | パターンマッチ | `case` 文に変換 |

---

## 5. 暗黙の動作

| 動作 | 条件 | 説明 |
|------|------|------|
| `clk` ポート自動追加 | `async func` 存在 & `clk` 未宣言 | `input logic clk` を先頭に追加 |
| `rst` ポート自動追加 | `async func` 存在 & `rst` 未宣言 | `input logic rst` を `clk` の後に追加 |
| 一時変数インライン展開 | `_tXXXX` 変数 | MIRテンポラリを式に展開 |
| `self.` プレフィックス除去 | `self.xxx` | SVでは `xxx` に短縮 |
