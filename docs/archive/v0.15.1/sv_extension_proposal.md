# SV バックエンド 構文拡張提案 (v0.15.1)

## 背景

現在の Cm SV バックエンドは、Cm の汎用構文（`async`, `func`, `void`）をSV の `always_ff` / `always_comb` にマッピングしている。しかし、SV には Cm に直接対応する構文がない機能が多数あり、また Cm のキーワードが SW/HW で異なる意味を持つ問題がある。

本ドキュメントでは、ユーザーの提案を含む構文拡張の候補を列挙する。

---

## 拡張1: `always_ff` マッピングの明示化

### 現状の問題

```cm
// 方法A: asyncキーワード流用 — JSバックエンドと意味が衝突
async func tick() { ... }           // → always_ff @(posedge clk)

// 方法B: posedgeパラメータ — 意味は明確だが構文が特殊
void blink(posedge clk) { ... }     // → always_ff @(posedge clk)
```

`async` は JS の非同期と意味が衝突し、SV ユーザーには直感的でない。

### 提案: `async void ff(...)` 構文

```cm
// 提案: async と void を組み合わせた明示的な構文
async void ff() { ... }             // → always_ff @(posedge clk)
async void ff(posedge clk) { ... }  // → always_ff @(posedge clk)
async void ff(negedge rst) { ... }  // → always_ff @(negedge rst)
```

#### メリット
- `async` = 順序回路 (クロック同期) を明示
- `void ff()` = 「flip-flop ブロック」と自然に読める
- 既存の `async func` との後方互換性を維持可能

#### 検討事項
- `ff` は関数名か予約語か？ → **関数名** として扱い、命名規則で意味付与
- `async void` と `async func` の共存ルールが必要

---

## 拡張2: `function` / `task` の SV ネイティブ対応

### 現状の問題

```cm
func select() { ... }  // → always_comb — SV の function とは異なる
```

SV の `function` は **純粋な組み合わせ論理関数** で、モジュール内で呼び出し可能な再利用可能なロジック。Cm の `func` はこれとは異なり `always_comb` ブロック全体を生成する。

### 提案

| 新Cm構文 | SV出力 | 用途 |
|---------|--------|------|
| `#[sv::function] func f(uint a, uint b) -> uint {...}` | `function ... endfunction` | 再利用可能な組み合わせロジック |
| `#[sv::task] void f() {...}` | `task ... endtask` | 手続き的ロジック |

あるいは:
```cm
// SV function を直接記述
sv function uint mux(uint a, uint b, bool sel) {
    return sel ? a : b;
}
```

---

## 拡張3: `assign` (連続代入) のサポート

### 現状
`#[sv::assign]` 属性を使用した連続代入がサポートされています。

```cm
// 属性で明示
#[sv::assign]
bool led = (counter > 25000000);
// → wire led;
// → assign led = (counter > 25000000);
```

### 制約
- 初期値は定数式のみ（実行時計算は未対応）
- wire宣言と assign 文がセットで生成される

### 将来の拡張候補
```cm
// 方法: wire型 + 初期値で推論（未実装）
#[output] wire<bool> led = (counter > 25000000);
// → assign led = (counter > 25000000);
```

---

## 拡張4: `generate for` / パラメトリック生成

### 現状
ループの SV 出力は未サポート。

### 提案
```cm
// 定数ループ → generate for
#[sv::generate]
for (uint i = 0; i < WIDTH; i++) {
    assign out[i] = in[WIDTH - 1 - i];
}
// → genvar i;
// → generate for (i = 0; i < WIDTH; i = i + 1) begin
// →     assign out[i] = in[WIDTH - 1 - i];
// → end endgenerate
```

---

## 拡張5: 連接 / ビットスライス演算子

### 現状
✅ **実装済み**: SV の `{a, b}` (連接) と `{N{expr}}` (複製) は対応済み。ビットスライス `a[3:0]` は未サポート。

### 提案 (ビットスライスのみ)
```cm
// ビットスライス: 配列添字の拡張
utiny low = data[7:0];          // 方法A: 範囲添字
utiny low = data.bits(7, 0);    // 方法B: メソッド
```

---

## 拡張6: 非同期リセット対応

### 現状
✅ **実装済み**: `always_ff @(posedge clk or negedge rst_n)` は対応済み。

### 使用例
```cm
// 複数エッジの指定
void process(posedge clk, negedge rst_n) {
    if (!rst_n) {
        counter = 0;
    } else {
        counter = counter + 1;
    }
}
// → always_ff @(posedge clk or negedge rst_n) begin
//       if (!rst_n) begin
//           counter <= 0;
//       end else begin
//           counter <= counter + 1;
//       end
//   end
```

---

## 拡張7: `localparam` のサポート

### 現状
✅ **実装済み**: `const` 変数は `localparam` として出力される。

### 使用例
```cm
// const → localparam
const uint CLK_FREQ = 50_000_000;
// → localparam logic [31:0] CLK_FREQ = 32'd50000000;

// #[sv::param] 付き → parameter (外部から変更可能)
#[sv::param] const uint WIDTH = 8;
// → parameter WIDTH = 32'd8;
```

---

## 拡張8: `struct packed` / `enum` のサポート

### 現状
✅ **実装済み**: Cm の `struct` / `enum` は SV バックエンドで対応済み。

### 使用例
```cm
//! platform: sv

// パックド構造体
#[sv::packed]
struct AXIAddr {
    uint addr;
    utiny len;
    utiny size;
    utiny burst;
}
// → typedef struct packed {
//       logic [31:0] addr;
//       logic [7:0] len;
//       logic [7:0] size;
//       logic [7:0] burst;
//   } AXIAddr;

// 列挙型 (FSM状態)
enum State {
    IDLE,
    READ,
    WRITE,
    DONE
}
// → typedef enum logic [1:0] {
//       IDLE = 2'd0, READ = 2'd1, WRITE = 2'd2, DONE = 2'd3
//   } State;
```

---

## 優先度まとめ

| 優先度 | 拡張 | 理由 |
|-------|------|------|
| **P0** | 拡張1: always_ff明示化 | 既存 `async` の意味衝突を解消 |
| **P0** | 拡張6: 非同期リセット | 実用的なFPGA設計に必須 |
| **P0** | 拡張7: localparam | `const` → `localparam` は自然 |
| ~~**P1**~~ | ~~拡張3: assign~~ | ✅ **実装済み** (`#[sv::assign]` 属性) |
| **P1** | 拡張5: 連接/スライス | ビット操作はHDLの基本 |
| **P2** | 拡張2: function/task | 再利用ロジックの定義 |
| **P2** | 拡張8: struct/enum | FSM設計パターンに必要 |
| **P3** | 拡張4: generate | パラメトリック設計 |
