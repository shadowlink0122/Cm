# v0.16.0 調査報告 8: SVコード生成の精査（実回路ビルド結果の監査）

実施日: 2026-07-11対象: CmCPUリポジトリの実回路（blink / button_counter / uart_hello / uart_button / hdmi_colorbar / hdmi_text）を現行コンパイラ（v0.16.0, feature/v0.16.0 f6011f2）でSV生成し、ソースとの意味等価性・余分なコード生成・バグの温床を精査した。機械検査として Verilator 5.046 `--lint-only -Wall` を併用した。

## 結論サマリー

- 生成SVの**回路としての意味は既存回路の全ファイルでソースと等価**（TMDSエンコーダのelse-if平坦化、colorbar_genの補集合条件展開も等価性を確認）
- 修正すべき不具合が4件: トップモジュール名（A-1）、一時変数のスコープ漏れ（A-2）、**SV予約語の識別子衝突**（A-4）、**async関数の自動clk/rstポート注入による重複宣言**（A-5）。A-4/A-5は新規CPU/GPUサンプルの作成中に実際に踏んだ再現性のあるバグ
- 「意図していないバグが生まれる部分」として最重要なのは**NBA意味論とソースの逐次的見た目の乖離**（A-3）と**全面的な幅警告抑制**（B-1）

## A. 不具合・要修正

### A-1. `module` 宣言名がトップモジュール名に反映されない【バグ】

`src/hdmi/main.cm` は `module hdmi_colorbar;` を宣言しているが、生成SVは`module main (` となる（ファイル名 `main.cm` 由来）。他の回路は宣言名とファイル名が偶然一致しているため露見していなかった。Verilator も `%Warning-DECLFILENAME: Filename 'hdmi_colorbar' does not match MODULE name: 'main'` を報告。合成プロジェクトのトップモジュール名指定と食い違う実害があるため、`module NAME;` 宣言をSVモジュール名に使うべき。

### A-2. MIR一時変数のモジュールスコープ漏れとblocking代入【要改善】

`&&`/`||` の短絡loweringや関数呼び出し結果が `_t1014` のような一時変数として**モジュールスコープに宣言され、シーケンシャルalways内でblocking代入**される。

```systemverilog
// button_counter.sv — always @(posedge clk) 内
_t1014 = (btn_prev) ? ~btn_state : 1'b0;   // blocking（NBAと混在）
if (_t1014) begin ...
```

- Verilator `-Wall` で **BLKSEQ 44件**（hdmi_text）、**VARHIDDEN 5件**（`_t1003`/`_t1004` がモジュールスコープと `function automatic` 内で重複宣言、シャドーイング頼みで壊れやすい）
- 代入後使用のため現状の意味は正しいが、(1) 一時変数はalwaysブロック内ローカル（`begin : name logic t; ... end`）か式インライン化にする、(2) 一時変数名の採番をスコープ間で衝突させない、の2点を推奨

### A-3. NBA意味論とソースの逐次的見た目の乖離【仕様明文化 or 警告を提案】

posedge関数内のモジュール状態変数への代入はすべてNBA（`<=`）に変換されるため、**「代入した直後の参照」は今サイクルの新値ではなく前サイクル値を読む**。

```cm
// button_counter.cm — ソフトウェア的解釈では恒偽の条件
btn_prev = btn_state;
if (btn_prev && !btn_state) {  // 逐次なら btn_state && !btn_state == false
    presses = presses + 1;      // NBAでは前サイクルのbtn_prevが読まれ、正しく動く
}
```

ハードウェア記述としては正しい定番イディオムだが、Cmのソフトウェア風構文では逐次実行に見えるため、逆パターン（新値が読まれる前提のコード）を書くと**シミュレーションは通るのに意図と違う回路**になり得る。対応案:
1. 言語仕様・チュートリアルに「posedge関数内の状態変数は前サイクル値が読める、代入は次サイクル反映（NBA）」を明文化する
2. checkerで「posedge関数内で代入した状態変数をその後で参照」に注意喚起の警告を出す（ローカル変数は blocking なので対象外）

### A-4. SV予約語と衝突する識別子がエスケープされない【バグ】

Cmソースで `program` という変数名（SVの予約語）を使うと、そのまま`logic [31:0] program [0:7];` と出力され **iverilogが構文エラー**になる（`Syntax in assignment statement l-value`）。CPUサンプル作成時に遭遇。SV予約語（`program`/`class`/`bind`/`priority` 等）は codegen でリネームかエスケープ識別子（`\program `）にするべき。checkerでの早期警告も有効。

### A-5. asyncクロック関数＋内部クロックで自動ポートが重複宣言を生む【バグ】

`async void f(posedge clk)` のクロックが `#[input]` 宣言されておらずモジュール内部信号（`bool clk = false;` + OSC駆動）の場合、**`input logic clk, rst` ポートが自動注入され、内部の `logic clk;` 宣言と重複して不正なSVになる**（Verilator: `Duplicate declaration of signal: 'clk'`）。ソースに存在しない `rst` ポートも一緒に注入される。`-D TEST` ビルドではクロックが `#[input]` になるため顕在化せず、合成ビルドでのみ発生する点が厄介。非asyncの `void f(posedge clk)` は正常。クロック解決を「input → 内部信号 → 見つからなければエラー」に統一すべき。

## B. バグの温床（設計上の留意点）

### B-1. ファイル全面の `lint_off WIDTHTRUNC / WIDTHEXPAND`

生成SVの冒頭で幅警告を全面抑制しているため、**幅間違いの実バグもSV側では検出されない**。`int`（signed 32bit）中心の型付けで `presses（8bit） + 32'sd1` のような切り詰めが常態化しており、意図的な設計（幅検査は言語側checkerの責務）だとしても、抑制範囲を「コンパイラが幅安全を確認した式」に限定するか、`--sv-strict-width` のようなオプトインで抑制なし出力を選べるようにしたい。

### B-2. `always_ff` / `always_comb` の不使用

現行は Gowin EDA 互換のため `always_ff @` を `always @` に置換して出力する（codegen.cpp:471, 意図的）。副作用として多重ドライバやラッチ推論に対するSVレベルの防御が効かない。ターゲット指定（例: `//! sv: vendor=gowin`）がない場合は `always_ff`/`always_comb` を保持する切り替えを提案。

### B-3. 符号付きリテラルと符号無し変数の混在

`uint` 変数への `32'sd1` 代入・比較が多数生成される。SVの規則（片方unsignedなら式全体unsigned）で現状は正しいが、コンテキストの符号に合わせたリテラル生成にすると読みやすく、符号関連lintにも強くなる。

## C. 余分なコード生成（品質・可読性）

1. **非TESTビルドでも空のテストベンチ `*_tb.sv` を常時生成**（`#[test]` が全て除外され `#100; $finish;` だけのシェルが残る）。`-D TEST` 無しではTB生成をスキップすべき
2. **定数畳み込み後に未使用となる localparam を出力**（blink: `CLK_FREQ`、hdmi: `H_BP` 等 — Verilator UNUSEDPARAM 計9件）
3. **未使用信号の残留**（hdmi_text: `pll_lock` 等 UNUSEDSIGNAL 計13件。`#ifdef` 分岐で使われなくなった信号が宣言だけ残るケース）
4. **else-ifチェーンの補集合条件展開**: `if (hc < W) ... else if (hc < 2W) ...` が`if (hc < W) {...} if (hc >= W) { if (hc < 2W) {...} } ...` の独立if列に展開され比較器が重複（colorbar_gen 8段、uart_button msg_idx 12段）。合成では共有されるが可読性・保守性が低い。MIR→構造化ifの再構成改善候補
5. **PROCASSINIT（-Wall）**: 宣言初期値＋プロシージャ代入はFPGAの意図的パターンなので容認（計66件）。生成側で限定的に lint_off を付与してもよい
6. cosmetic: assign式の冗長な多重括弧、ネストしたelse-ifの `end else if` のインデント崩れ（hdmi_colorbar.sv:213 付近）
7. **`#[test]` から内部レジスタを参照すると iverilog の束縛エラー**（`Unable to bind wire/reg/memory 'halted'`）になる。テストベンチが参照できるのはポートのみだが、エラーがiverilog段階まで遅延しメッセージも不親切。checkerで「テスト関数はポートのみ参照可」を早期診断すべき

## D. 正しさを確認できた項目

- レジスタ宣言初期値の出力（`logic [31:0] counter = 32'd0;`）と`#[test]` コードの合成時除外
- 定数畳み込み（`CLK_FREQ / 2 - 1` → `32'd26249999`）
- `#[sv::param]` → パラメータ付きインスタンス（PLL/OSC）、OSER10 D0..D9 / TLVDS_OBUF の結線
- `#[sv::sync] stages=2` → `(* async_reg = "true" *)` 2段FF
- `#[sv::ram]` → `(* ram_style = "block" *)` と `initial $readmemh`（相対パス）
- Cm関数 → `function automatic` 化、utiny/ushort の 8/16bit幅反映
- TMDSエンコーダのDCバランス処理: ソースの入れ子if/elseと生成SVのelse-if平坦化が意味等価であることを分岐網羅で確認
- NBAのlast-write-wins（同一ブロック内の `stable_count` 二重代入等）がソースの逐次上書きと一致

## E. 監査時の注意事項

`CmCPU/build/` 配下に**旧コンパイラの生成物が残存**していた（`build/blink/blink.sv` 6/2生成: 初期値なし・always_ff使用・絶対パスの`$dumpfile` — いずれも現行では解消済み）。監査・比較の際は必ず再生成すること。Makefileの出力パスが平置き（`build/blink.sv`）とサブディレクトリ（`build/blink/`）で揺れていた時期があるため、stale生成物の削除を推奨。

## 対応優先度（提案）

| 項目 | 優先度 | 種別 | 状況 |
|---|---|---|---|
| A-4 SV予約語の識別子衝突 | 高 | バグ修正 | **対応済み**（2026-07-11: 明確なエラーで停止） |
| A-5 async自動clk/rstポートの重複宣言 | 高 | バグ修正 | **対応済み**（2026-07-11: エッジパラメータ解決時は注入しない） |
| A-1 moduleトップ名 | 高 | バグ修正 | **対応済み**（2026-07-11: `module NAME;` を優先） |
| A-3 NBA意味論の明文化/警告 | 高 | ドキュメント+警告 | **対応済み**（2026-07-11: チュートリアル明文化 + `--sv-warn-nba`。既存コーパスでも正当なイディオムとして多用されるためオプトイン） |
| C-7 テストのポート外参照 | 中 | 機能改善 | **対応済み**（2026-07-11: 内部信号の読み取りは `dut.` 階層参照として対応。入力ポート以外への代入は明確なエラー） |
| A-2 一時変数のスコープ/blocking | 中 | codegen改善 | **対応済み**（2026-07-11: 関数ローカル・一時変数を名前付きブロック内宣言へ移動。モジュールスコープ汚染とVARHIDDENを解消。blocking代入自体は局所値のため正当として維持） |
| B-1 幅lint抑制の限定化 | 中 | codegen改善 | **対応済み**（既存の `--sv-strict-lint` で抑止なし出力が可能。チュートリアルに明記。既定は型チェッカが幅安全を担保する設計のため据え置き） |
| C-1 空TB生成の抑止 | 中 | codegen改善 | **対応済み**（2026-07-11: `#[test]` も `//! test:` も無い場合はTBを生成しない） |
| C-2 未使用localparam | 低 | codegen改善 | **対応済み**（2026-07-11: 使用箇所の無いlocalparamは固定点反復で除去） |
| C-3 未使用信号 | 低 | codegen改善 | 未対応（インスタンス接続のみの信号を誤削除するリスクがあり、参照/駆動の区別を持つデータフロー解析が必要） |
| C-4 else-if再構成 | 低 | — | **再分類**（調査の結果、CmCPUのソース自体が独立if+補集合条件で記述されており、生成SVは忠実な変換だった。else-ifチェーンは正しく保持される（tmds_encodeで確認済み）ため、コード生成の欠陥ではない。改善はソース側の記述で可能） |
| B-2 always_ffのターゲット切替 | 低 | codegen改善 | **対応済み**（2026-07-11: `--sv-always-ff` で保持を選択可能。既定はGowin互換） |
