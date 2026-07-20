---
title: SVバックエンド - データ構造
parent: Tutorials
nav_order: 15
---

[English](../../../en/compiler/sv/data.html)

# SVバックエンド - データ構造

[SystemVerilogバックエンド](index.html) の詳細ページです。連接・複製、enum、配列、文字列の扱いを解説します。

---

## 連接と複製

### 基本構文

```cm
result = {a, b};         // → {a, b}
replicated = {3{a}};     // → {3{a}}
```

### 型推論

連接と複製は `bit[N]` 型に対してビット幅を自動計算します:

```cm
#[input]  bit[4] a = 0;
#[input]  bit[4] b = 0;
#[output] bit[8] result = 0;      // {a, b} → 4+4=8ビット
#[output] bit[12] replicated = 0; // {3{a}} → 4*3=12ビット

always_comb void compute() {
    result = {a, b};
    replicated = {3{a}};
}
```

### ビルトイン関数

`{...}` がブロックと曖昧な場合、明示的な関数を使用できます:

```cm
result = concat(a, b);       // → {a, b}
wide = replicate(nibble, 3); // → {3{nibble}}
```

---

## 列挙型 (FSM)

Cmの `enum` はSVの `typedef enum logic` に変換されます。ビット幅は**最大タグ値**から自動計算されます（明示的なタグ値に対応）:

```cm
enum State { IDLE, RUN, DONE, ERROR }
// → typedef enum logic [1:0] { IDLE = 2'd0, RUN = 2'd1, DONE = 2'd2, ERROR = 2'd3 } State;

enum Status { OK = 0, NOT_FOUND = 404, SERVER_ERROR = 503 }
// → typedef enum logic [9:0] { OK = 10'd0, NOT_FOUND = 10'd404, SERVER_ERROR = 10'd503 } Status;
```

> **旧バージョンの注意:** 以前はメンバー数から幅を計算していたため、
> `ERROR = 100` が `1'd100` のような不正リテラルになる問題がありました（修正済み）。

### enum + switch (FSM)

```cm
State current = State::IDLE;

void fsm(posedge clk) {
    switch (current) {
        case(State::IDLE) { current = State::RUN; }
        case(State::RUN) { current = State::DONE; }
        else { current = State::IDLE; }
    }
}
```

---

## 配列とメモリ

### 内部配列（レジスタ/RAM）

```cm
utiny buffer[16];                    // → logic [7:0] buffer [0:15];
#[sv::bram] utiny mem[1024];         // → (* ram_style = "block" *) logic [7:0] mem [0:1023];
#[sv::lutram] utiny lut[16];         // → (* ram_style = "distributed" *) logic [7:0] lut [0:15];
```

### 配列型ポート

```cm
#[output] uint[4] data;   // → output logic [31:0] data [0:3]
```

> 配列の**初期値**は initial ブロックとして出力され、`#[sv::memfile]` /
> `--emit-memfile` による `$readmemh` にも対応しています（v0.15.1）。
> 詳細は[メモリ初期化](memory.html)を参照してください。

---

## 文字列

### const文字列（推奨）

const の string はパックドベクトル定数（`localparam`）になり、インデックスアクセスはパートセレクトに変換されます:

```cm
export const string TITLE = "HELLO CM";

utiny ch = TITLE[i] as utiny;
// → localparam logic [63:0] TITLE = "HELLO CM";
//   ch = TITLE[(7 - i) * 8 +: 8];   // 先頭文字がMSB側
```

### 制限

- **非const の string 変数・関数引数・戻り値は `logic [23:0]`（3文字分）固定**です。3文字を超える文字列を渡すと切り詰められます。const 定数以外での string 使用は避けてください（[v0.16.0ロードマップ](../../../../archive/v0.16.0/roadmap.html)で長さの型化を検討中）。

## ビットスライス（v0.16.0）

`bit[N]`・整数値の部分ビットを、SVと同じ降順・両端含みの範囲で読み書きできます:

```cm
bit[16] word = 0xABCD;
bit[8] hi = word[15:8];      // 0xAB（定数範囲）
word[11:4] = 0xFF;           // 部分代入（read-modify-write）

uint i = 1;
bit[4] nib = word[i*4 +: 4]; // 可変基点+定数幅（インデックスドパートセレクト）
```

- 範囲・幅は**整数リテラル**で指定します（v0.16.0時点の制限）。基点（`+:` の左）は任意の整数式が使えます
- 全バックエンド共通のシフト+マスクに脱糖されるため、実行系（JIT/native/WASM/JS）でも同じ結果になります
- 幅は最大64ビット、結果型は `bit[w]`（整数との相互代入可）


---

<!-- nav -->
← 前: [SVバックエンド - 制御構文とループ](control-flow.html) ｜ [目次](index.html) ｜ 次: [SVバックエンド - メモリ初期化（ROM/RAM）](memory.html) →

---

## interfaceとimplメソッド

`interface` / `impl` で定義した構造体メソッドはSVの `function automatic` として合成されます。
メソッドの `self` はコンパイラが自動的に構造体の値渡しへ変換するため、ポインタ非対応のSVでもそのまま使えます。

```cm
interface Summable {
    int total();
}

struct Pair {
    int x;
    int y;
}

impl Pair for Summable {
    int total() {
        return self.x + self.y;
    }
}

void compute() {
    Pair p;
    p.x = a;
    p.y = b;
    total = p.total();  // Pair__total(p) というSV functionの呼び出しになる
}
```

制約（明確な診断エラーになります）:

- `self` のフィールドへ書き込むメソッドは未対応（`error[SV010]`。値渡しでは呼び出し元へ反映されないため）
- interface型変数経由の動的ディスパッチは未対応（`error[SV011]`。呼び出し先が静的に決まる具体型経由の呼び出しを使う）
- `self` のポインタ値をメソッド呼び出し以外へ持ち出す使い方は未対応（`error[SV012]`）
