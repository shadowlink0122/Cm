# SV-N4: generate / genvar・パラメータ幅配列・パラメータ依存ループ展開

**分類:** 新機能（構造的パラメトリック生成）
**優先度:** Medium
**ステータス:** 未実装（v0.17.0 SVギャップ調査で検出。ロードマップA5/A6に対応）

## 現状（実測: cm 2026-08-08ビルド）

- `generate`/`genvar`/for-generate/if-generate は**一切生成されない**（`validation.cpp`に予約語登録のみ・codegenに出力なし）。
- 定数ループの静的展開（`tests/sv/control/const_loop_unroll.cm`）が部分的な代替で、コンパイル時定数の範囲ループは while を残さず展開される。
- ただし**パラメータ依存**のループ展開（A5）と**パラメータ幅のメモリ配列**（A6、`bit[WIDTH][DEPTH]`）は未対応（`docs/tutorials/ja/compiler/sv/hierarchy.md:156`に将来対応と明記）。

現状はモジュールパラメータ（`#[sv::parameter]`→`module #(parameter WIDTH=8)`）は宣言・伝搬できるが、そのパラメータで**本体を構造的にスケール**（N個のインスタンス・N段のパイプライン・WIDTH幅の配列）させる手段がない。

## 提案

1. **generate-for / genvar**: コンパイル時に境界が定数（またはモジュールパラメータ）で決まるループを、SVの`generate for (genvar i=0; i<N; i++) begin ... end`として出力する。中でモジュールインスタンス・assign・always・信号宣言を反復生成できるようにする。
   ```systemverilog
   genvar i;
   generate
     for (i = 0; i < N; i = i + 1) begin : gen_stage
       assign out[i] = in[i] ^ key[i];
     end
   endgenerate
   ```
2. **generate-if**: パラメータ値に応じた条件付きハードウェア生成（`generate if (WIDTH > 8) ... else ... endgenerate`）。
3. **パラメータ幅配列（A6）**: `bit[WIDTH]` 要素・`[DEPTH]`段の配列で、WIDTH/DEPTHがモジュールパラメータのものを `logic [WIDTH-1:0] mem [0:DEPTH-1]` として出力する。
4. **パラメータ依存ループ展開（A5）**: 境界がパラメータのループを、展開でなく generate-for または while+パラメータ境界で出力する。

## 実装方針

- Cm側の表現: 既存の定数ループ展開経路（`const_loop_unroll`）を、境界がモジュールパラメータ（`#[sv::parameter]`定数）の場合に「展開」でなく「generate-for出力」へ分岐する。ループ変数を`genvar`宣言する。
- インスタンス反復は SV-N5（インスタンス配列）と連携（generate-for内でのインスタンス化）。
- 配列型解決で要素幅・段数がパラメータシンボルのとき、リテラル幅でなくパラメータ式（`[WIDTH-1:0]`/`[0:DEPTH-1]`）を出力する（現状はリテラル畳み込み前提）。
- generate内で許可する構文（assign/always/インスタンス/信号宣言）を限定し、非合成構文が入ったらSV007で停止。

## テスト計画

`tests/sv/hierarchy/`・`tests/sv/control/` へ: パラメータNのgenerate-forによるビット並列XOR・N段シフトレジスタ、generate-ifによる条件付きロジック、パラメータ幅BRAM（`bit[WIDTH][DEPTH]`）が正しいSVを出力しiverilogで値検証（複数のパラメータ値でインスタンス化）。既存の定数ループ展開テストは非パラメータ経路として維持。
