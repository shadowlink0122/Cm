# v0.16.0 実装設計 3: トライステート（'z）とCDC同期化プリミティブ

優先度: 3
関連: [roadmap.md](roadmap.html) A3/B1（旧 missing_features 項目8）

> **✅ 2026-07-07 実装済み（属性方式に洗練）**: 言語コア（型修飾 `tri` /
> `z` リテラル）の追加を避け、既存の属性系だけで表現する方式で実装した。
> `#[sv::tri(oe: "...", out: "...")]` がトライステート駆動
> （`inout tri` + `assign ... : 1'bz`）を、
> `#[sv::sync(clk: "...", src: "...", stages: N)]` がCDC同期段を生成する。
> 実行系バックエンドはこれらの属性を無視する（SV専用機能）。
> 使い方はチュートリアル「実機I/O」を参照。以下は当初設計案（記録用）。

## 3.1 トライステート

### 目標

I2C等の双方向オープンドレインバスを記述可能にする。

```cm
#[inout]
#[sv::pin("A11", io_type: "LVCMOS33", pull: "UP")]
tri bool sda;          // tri修飾: 'z を取り得る

bool sda_oe = false;   // 出力イネーブル
bool sda_out = false;

void drive() {
    // trueならドライブ、falseならハイインピーダンス
    sda = sda_oe ? sda_out : z;   // 'z リテラル
}
```

```systemverilog
inout tri sda;
assign sda = sda_oe ? sda_out : 1'bz;
```

### 設計方針

- **`z` リテラル**: SVターゲット専用のキーワード定数。`tri` 修飾された
  信号への代入でのみ使用可（型検査で強制）。他バックエンドでは
  コンパイルエラー（非合成型と同じ扱い）
- `tri` 信号への代入は継続代入（`assign`）へ写像し、複数ドライバは
  エラー（Cm側で単一の駆動関数に制限）
- 読み取りは通常の値として扱う（'z 判定構文は提供しない。
  実バスではプルアップ前提の H/L 読みになるため）

## 3.2 CDC同期化プリミティブ

### 目標

クロックドメイン交差の2FF同期を宣言的に生成し、
生成子を通さない素通し交差を警告できる基盤を作る。

```cm
#[input] posedge clk_sys;
#[input] bool async_btn;

// clk_sys ドメインへ2段FFで同期（段数は引数、既定2）
#[sv::sync(clk: clk_sys, stages: 2)]
bool btn_sync = false;
```

```systemverilog
logic btn_sync_meta;
always @(posedge clk_sys) begin
    btn_sync_meta <= async_btn;
    btn_sync      <= btn_sync_meta;
end
```

### 設計方針

- `#[sv::sync]` は「入力信号を指定ドメインで同期した信号」を宣言する属性。
  ソース信号は初期化式で指定（`bool btn_sync = async_btn;` 形式も検討）
- メタステーブル段は `_meta` サフィックスで自動命名し、
  `(* async_reg = "true" *)` 属性（Vivado）/ Gowin相当を付与
- ドメイン推論・交差警告（アナライザ）は次段階。まず生成子を提供する

## テスト

- tri: lint（Verilator）+ iverilogでのオープンドレイン双方向シミュレーション
- sync: 2FF構造のゴールデンテスト + マルチクロックシミュレーション
