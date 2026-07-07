# SVバックエンド - 実機I/O（ピン制約・トライステート・CDC）

v0.16.0で追加された、実基板への配線と非同期入力を扱う機能群。

## ピン制約とプロジェクトスクリプトの生成（--emit-constraints）

物理ピン割り当てをポート宣言に `#[sv::pin]` 属性として同居させ、
Gowin向けの `.cst`（ピン制約）と `.tcl`（プロジェクトスクリプト）を生成できます。
ポート名変更時の手動同期エラーを構造的に防ぎます。

```cm
//! platform: sv
//! sv: device: GW5AST-LV138PG484AC1/I0 C
//! sv: option: use_ready_as_gpio

#[input]
#[sv::pin("T10")]
posedge clk;

#[output]
#[sv::pin("U12", io_type: "LVCMOS33", drive: 8)]
bool led_ready = false;
```

```bash
cm compile --target=sv blink.cm -o blink.sv --emit-constraints
# → blink.sv / blink.cst / blink_build.tcl
gw_sh blink_build.tcl   # 合成〜ビットストリーム生成まで一括実行
```

生成される `blink.cst`:

```
IO_LOC  "led_ready" U12;
IO_PORT "led_ready" IO_TYPE=LVCMOS33 DRIVE=8;
```

- `#[sv::pin]` の第1引数は物理ピン（必須）。以降は `key: value` 形式で、
  `io_type` / `drive` / `pull` / `slew` は正式属性名（IO_TYPE等）へ写像、
  未知のキーは大文字化してそのまま転記されます（ツール固有属性に対応）
- `//! sv: device:` が無い場合は `.cst` のみ生成されます
- `--emit-constraints` 指定時、`#[sv::pin]` の無いポートは警告されます
  （割り当て漏れの検出）
- 生成はオプトインです。既存の手書き .cst/.tcl 運用はそのまま継続できます

## トライステート（#[sv::tri]）

I2C等の双方向オープンドレインバスは、出力イネーブルと出力値のペアを
`#[sv::tri]` 属性で宣言します。oe=1で駆動、oe=0でハイインピーダンス（'z）です。

```cm
#[inout]
#[sv::tri(oe: "sda_oe", out: "sda_out")]
bool sda;

bool sda_oe = false;   // 出力イネーブル
bool sda_out = false;  // 駆動値
```

```systemverilog
inout tri sda;
assign sda = sda_oe ? sda_out : 1'bz;
```

- ポートは複数ドライバ可能なnet型 `tri` で宣言されます
- 読み取りは通常の値として扱えます（バス上のプルアップ前提）
- sda自体への直接代入は行わず、oe/outを通して制御してください

## クロックドメイン交差の同期（#[sv::sync]）

ボタン等の非同期入力は、そのまま使うとメタステーブルの原因になります。
`#[sv::sync]` で2FF同期段を宣言的に生成できます。

```cm
#[input] posedge clk;
#[input] bool async_btn;

#[sv::sync(clk: "clk", src: "async_btn", stages: 2)]
bool btn_sync;
```

```systemverilog
(* async_reg = "true" *) logic btn_sync_meta1;
(* async_reg = "true" *) logic btn_sync;

always @(posedge clk) begin
    btn_sync_meta1 <= async_btn;
    btn_sync       <= btn_sync_meta1;
end
```

- `stages` は省略時2（メタ段1 + 出力段1）。3以上も指定可能です
- 生成レジスタには合成ツール向けの `(* async_reg = "true" *)` 属性が付与されます
- 同期した信号（上記の `btn_sync`）を通常のロジックから参照してください

---

<!-- nav -->
← 前: [SVバックエンド - モジュール階層の保持](hierarchy.html) ｜ [目次](index.html) ｜ 次: [SVバックエンド - 状態初期化とシミュレーション](state-sim.html) →
