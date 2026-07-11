# v0.16.0 実装設計 2: 物理制約ファイル生成（#[sv::pin] / --emit-constraints）

優先度: 2関連: [roadmap.md](roadmap.html) §2（tcl/cst統合・ハイブリッド案）

## 目標

ピン割り当て・デバイス情報をCmソースに単一情報源として持ち、Gowin向け `.cst`（物理制約）と `.tcl`（プロジェクトスクリプト）を生成する。既存の手書きファイル運用は変更なしで継続可能（オプトイン）。

## 構文

```cm
//! platform: sv
//! sv: device: GW5AST-LV138PG484AC1/I0 C
//! sv: option: use_ready_as_gpio
//! sv: option: use_done_as_gpio

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
```

生成される `blink.cst`:

```
IO_LOC  "led_ready" U12;
IO_PORT "led_ready" IO_TYPE=LVCMOS33 DRIVE=8;
```

生成される `blink_build.tcl`: device宣言・`add_file <abs>/blink.sv`・`add_file <abs>/blink.cst`・`set_option -top_module blink`・optionディレクティブ群・`run all`。

## 設計判断

- **ピンは定数ではなく属性**: 値として演算する需要がなく、ポート宣言との物理的同居がリネーム時の同期エラーを構造的に防ぐため
- `#[sv::pin]` の引数: 第1引数=ピン位置（必須）、以降は `key: value` の名前付き引数（io_type / drive / pull / slew 等。未知のキーは`KEY=VALUE` としてそのまま IO_PORT に転記し、ツール固有属性に開かれた設計とする）
- `--emit-constraints` 指定時に `#[sv::pin]` の無い入出力ポートがあれば警告（割り当て漏れの検出）
- deviceディレクティブが無い場合、.tcl は生成せず .cst のみ生成

## 実装ポイント

- 属性のパース: 既存の `#[sv::memfile("...")]` パーサを名前付き引数対応に拡張
- MIR: `MirGlobalVar::attributes` に構造化して保持（現在は文字列リスト）
- codegen/sv に `emitConstraints()` を追加（emitMemfileIfRequested と同型の分離実装）
- ボードファイル（`//! sv: board:` によるピン対応表の再利用）は第2段階とし、本設計では単一ファイル完結のみ実装

## テスト

- ユニット: 生成 .cst / .tcl のゴールデンテスト（sv_codegen_test方式）
- 統合: CmCPUのblinkを `--emit-constraints` 生成物でビルドし、手書き .cst と同一の合成結果になることを確認
