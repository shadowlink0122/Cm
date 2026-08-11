# SV-N4: generate / genvar・パラメータ幅配列・パラメータ依存ループ展開

**分類:** 新機能（構造的パラメトリック生成）
**優先度:** Medium
**ステータス:** 一部実装済み（A5=パラメータ境界ループのfor形出力・A6=パラメータ幅メモリ配列を実装。generate-for/ifは未実装）

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

## 実装記録（A5＝パラメータ境界ループの合成可能化・2026-08-11）

提案4（パラメータ依存ループ）を実装した。方針の「while+パラメータ境界」は合成ツールが受理しない（yosys実測: While loops are only allowed in constant functions）ため、for文出力を採った。

- **for形再構成**: SVコード生成のCFG走査へ事前解析`computeForLoops`を追加した。各自然ループについて「ラッチ末尾の`var = var ± 定数`（増分）」と「ループ外先行ブロック末尾の`var = 定数`（初期値）」の単純カウントパターンを検出し、`for (i = 0; i < N; i = i + 1) begin ... end`として出力する（初期値・増分文は元位置から抑止）。MIRでは値が単一定義テンポラリの多段連鎖（`t1=copy(i); t2=t1+1; i=copy(t2)`）になるため、Use(Copy(温))連鎖を定義rvalueまで辿って照合する。
- **適用条件**: ヘッダの残余文が全て単一定義テンポラリ（インライン展開されSV行を出さない）であること（for文は条件を自動再評価するため）。単一ラッチのみ。パターン外のループは従来どおりwhile出力（シミュレーション系ツールは受理する）。
- **検証**: `tests/sv/control/loop/param_bound.cm`（`#[sv::parameter] const uint N`境界のXOR畳み込み・シミュレーション値検証SIM_OK・yosys synth通過を実測）。既存ループテスト（break/for/while/nested/const_unroll）を含むSVスイート全数PASS。
- 残: generate-for/genvar（モジュールスコープの反復インスタンス・assign生成。SV-N5と連携）・generate-if・A6（パラメータ幅メモリ配列`bit[WIDTH][DEPTH]`の`logic [WIDTH-1:0] mem [0:DEPTH-1]`出力）。

## 実装記録（A6＝パラメータ幅メモリ配列・2026-08-11）

提案3（パラメータ幅配列）を実装した。`bit[WIDTH][DEPTH]`（WIDTH/DEPTHが`#[sv::parameter]`）が`logic [WIDTH-1:0] mem [0:DEPTH-1];`として出力され、添字読み書きが直接インデックス（`mem[waddr]`）になる。

- **宣言**: SVコード生成のアンパックド次元出力（getArraySuffix）がarray_size前提で記号深度を落としていたため、size_param_name登録済みの場合に`[0:DEPTH-1]`を記号のまま出力する分岐を追加した。
- **添字**: MIR loweringの固定長/スライス判別が`array_size.has_value()`のみで、記号サイズ配列がスライス扱いになりスライスランタイム呼び出し（cm_slice_get_subslice等＝SVで無意味）へ落ちていた。記号サイズ（size_param_name）はエラボレーション時に確定する静的サイズとして固定長側へ分類するよう、判別サイト6箇所（is_fixed_array_type・多重添字walk・要素内側スライス判定・代入左辺のスライス基点判定）を拡張した。
- **検証**: `tests/sv/memory/param_width_mem.cm`（書き込み→読み出しのシミュレーション値検証SIM_OK・verilator/yosys通過）。判別変更は全バックエンド共通のMIRだが記号サイズ配列はSV文脈でのみ出現し、interpreter/llvm/jsスイートの回帰なしを確認。
- 残: generate-for/genvar（モジュールスコープの反復インスタンス・assign生成。SV-N5と連携）・generate-if。
