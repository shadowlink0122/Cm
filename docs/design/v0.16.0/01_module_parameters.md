# v0.16.0 実装設計 1: モジュールパラメータと階層Stage 3

優先度: 1（v0.16.0の中核）
関連: [roadmap.md](roadmap.html) A1/A2

> **✅ 2026-07-07 実装済み（属性構文に洗練）**: 宣言はモジュールレベル
> `<const ...>` 案ではなく、既存属性系に統一した
> `#[sv::parameter] const uint WIDTH = 8;` とした（言語コア変更なし）。
> P1（#(parameter)ヘッダ）・P2（[WIDTH-1:0]記号幅。checkerが
> size_param_nameを解決後も保持するよう変更）・P3（階層でのextern struct
> への写しとstructリテラル上書き→ #(.WIDTH(16))）まで完了。
> P4（ベンダプリミティブ実例）とパラメータ幅メモリ（bit[W][D]）・
> パラメータ依存ループ展開は未対応（ロードマップA5/A6扱い）。
> 検証: sv/hierarchy/param_top（シミュレーションで幅16の実効を確認）。

## 目標

パラメタライズドIPをCmで記述し、`#(parameter WIDTH = 8)` 付きSVモジュールとして
出力・インスタンス化できるようにする。

```cm
//! platform: sv
// fifo.cm — ジェネリックパラメータがSVパラメータになる
<const WIDTH: uint = 8, const DEPTH: uint = 16>
#[input]  bit[WIDTH] din;
#[output] bit[WIDTH] dout = 0;
```

```systemverilog
module fifo #(
    parameter WIDTH = 8,
    parameter DEPTH = 16
) (
    input  logic [WIDTH-1:0] din,
    output logic [WIDTH-1:0] dout
);
```

インスタンス化（階層Stage 3）:

```cm
//! sv: hierarchy
import ./fifo;

fifo<WIDTH: 16> rx_fifo = fifo { din: rx_data, dout: rx_out };
// → fifo #(.WIDTH(16)) rx_fifo (.din(rx_data), .dout(rx_out));
```

## 設計方針

1. **構文**: モジュールレベルの `<const NAME: type = default>` 宣言
   （ジェネリック構造体の構文と統一。SVターゲットではモジュールパラメータへ写像）
2. **MIR**: `MirProgram::modules`（既存の未使用フィールド）を有効化し、
   モジュール単位のMIRを保持する。パラメータは `MirModule::parameters`
   （名前・型・デフォルト値）として運ぶ
3. **codegen**: ポート幅・localparam・配列サイズ中の
   パラメータ参照はテキストではなく式ツリー（SVExpr Atom）として保持し、
   `WIDTH-1:0` のような区間式を生成する
4. **型検査**: パラメータは const 整数のみ（v0.16.0時点）。
   幅計算はパラメータを記号のまま伝播し、具体化はSV側（合成ツール）に委ねる
5. **階層Stage 3**: import先のモジュールMIRを親のMirProgramに取り込み
   （現在の「自プロセス再帰起動+テキスト連結」を置換）、
   インスタンス宣言の型引数を `#(.PARAM(value))` に写像する

## 段階分割

| Phase | 内容 | 検証 |
|---|---|---|
| P1 | パラメータ宣言のパース・MIR保持・`#(parameter)`出力（単一モジュール） | sv_codegen_test + lint |
| P2 | ポート/配列幅でのパラメータ参照（`bit[WIDTH]`） | パラメタライズドFIFOのlint |
| P3 | Stage 3: 複数モジュールMIR + パラメータ付きインスタンス | iverilogシミュレーション |
| P4 | ベンダープリミティブ実例（rPLL等のextern struct + パラメータ） | 実機/チュートリアル |

## 既知の論点

- パラメータに依存する定数ループ展開は不可（展開回数が記号）→ genvar generate が
  必要になるケースは v0.16 では明示エラー+ FSM推奨のガイダンス
- Cm側でのシミュレーション（JIT実行）はデフォルト値で具体化して行う
