# svバックエンド 不足機能の実装案

作成日: 2026-07-04
対象バージョン: v0.15.1
言語: 日本語（[English](sv_backend_missing_features_en.html)）

CmCPU（Tang Console 138K向けCPU開発）での実運用と2026-07-04の調査で判明した、svバックエンドに不足している機能と実装案をまとめる。各項目は「CPU開発に必要な順」に並べている。

実装済みの修正（優先順位括弧・レジスタ初期値・キャスト・算術シフト・enum幅・配列ポート・符号付き定数・whileループ再構成・initialブロック）については `docs/releases/v0.15.1.md` と `docs/archive/013_refactoring_sv_backend_and_cpp.md` を参照。

---

## 1. モジュール階層の保持（最重要）

**現状**: 1コンパイル = 1モジュール。`import` は全シンボルをフラット化して単一モジュールに展開する。インスタンス化できるのは `extern struct`（外部プリミティブ）のみ。CmCPUの `hdmi_text_top` は約6,300行の単一モジュールに展開されており、CPUをALU・デコーダ・レジスタファイルに分割しても合成結果の階層が失われる。

**実装案**:
1. `module X { ... }` 相当の単位（現状はファイル）ごとに `SVModule` を生成し、`modules_` ベクタに複数モジュールを保持する（データ構造は既に対応済み）
2. Cmモジュールのインスタンス化構文を導入する。既存の `extern struct` インスタンス化と同じ生成経路（named port connection）を再利用できる:
   ```cm
   import ./alu;           // aluモジュールを階層のまま利用
   Alu alu_inst = Alu { .a = op_a, .b = op_b, .result = alu_out };
   ```
3. import解決時に「フラット化」か「階層化」かを選択できるようにする（後方互換のためデフォルトはフラット化、`//! sv: hierarchy` ディレクティブで階層化）

**難易度**: 高（analyzeMIRの単一 `default_mod` 前提の解消が必要）

## 2. モジュールパラメータ（`module #(parameter ...)`）

**現状**: `const` は `localparam` のみ。生成モジュール自体をパラメータ化できない。`#[sv::param]` はv0.15.1で廃止宣言されたがコードとテストに残存しており、仕様と実装が食い違っている。

**実装案**: `export const` を `parameter`（上書き可能）として出力するオプション属性 `#[sv::param]` を正式仕様として復活させるか、完全に削除して `localparam` に一本化する。階層化（案1）とセットで `#(.WIDTH(8))` のパラメータオーバーライドを生成する。

**難易度**: 中（案1に依存）

## 3. メモリ初期化（`$readmemh` / 配列初期値）

> **✅ 2026-07-05 全実装済み**:
> - 案1（2026-07-04）: 配列リテラル初期値の `initial` ブロック出力。
>   `utiny[4] rom = [10, 20, 30, 40];` が `initial begin rom[0] = 10; ... end` として出力される
>   （BRAM/LutRAM属性付き配列にも対応）。回帰テスト: `tests/sv/memory/array_init`
> - 案2（2026-07-05）: `#[sv::memfile("font.hex")]` 属性で
>   `initial $readmemh("font.hex", mem);` を出力（初期値なし配列にも使用可）。
>   回帰テスト: `tests/sv/memory/readmemh`、ユニットテスト `MemfileReadmemh` 他
> - 案3（2026-07-05）: `--emit-memfile` オプションで配列リテラル初期値を
>   `.hex` ファイル（生成SVと同じディレクトリ）として書き出す。
>   ユニットテスト `MemfileEmitHexFile`

**現状**: 配列（BRAM/LutRAM）の初期値は出力されない。命令ROM・フォントROMは巨大なlookup関数（case文）として記述するしかなく、CmCPUの `font_rom.cm` は2,174行のcase文になっている。

**実装案**:
1. 配列リテラル初期値を `initial begin mem[0] = ...; end` として出力（小規模向け）
2. `#[sv::memfile("font.hex")]` 属性で `initial $readmemh("font.hex", mem);` を出力（大規模向け）
3. コンパイル時にconst配列の内容を `.hex` ファイルとして出力する `--emit-memfile` オプション

**難易度**: 低〜中（属性1つと initial 出力の追加）

## 4. string型の関数境界での幅固定（24bit）の解消

> **✅ 2026-07-04 一部実装済み**: 非constのstringに3文字を超える初期値を与えた場合、
> サイレントな切り詰めではなく **コンパイルエラー（`error[SV005]`）** になるようにした
> （実装案2のエラー化）。回帰テスト: `tests/sv/errors/string_truncation`。
> 長さ情報の型システム化（案1）は未実装。

**現状**: `mapType`/`getBitWidth` が `String → logic [23:0]`（3文字）固定。const グローバルは実長で出力されるが、関数引数・戻り値・非const変数は3文字を超えると切り詰められる。

**実装案**:
1. 型システムに文字列長を持たせる（`string<N>`）か、HIR型の `array_size` を流用して定数伝播で長さを決定する
2. 長さが決定できない string の関数境界使用は **コンパイルエラー**にする（現状はサイレントに壊れるため、エラー化だけでも価値がある）

**難易度**: 中（フロントエンドの型情報拡張）

## 5. `while (true)` + `break` 型ループ（無条件ヘッダ）

> **✅ 2026-07-04 解決確認済み**: フロントエンドは `while (true)` を
> `switchInt(const true)` 付きの条件ヘッダに降下するため、既存のループ再構成が
> そのまま適用され `while (_t) begin ... break; ... end`（`_t = 1'b1`）として
> 正しく出力される。回帰テスト: `tests/sv/control/while_true`。
> ヘッダが純粋な `Goto` になるケースは現状のMIRでは生成されない。

## 6. generate / genvar 相当の構文

**現状**: ハードウェア構造の繰り返し生成（例: Nビット分のモジュールインスタンス）は手書き展開が必要。

**実装案**: const範囲の `for` をコンパイル時に展開する（Cmはコンパイル時定数畳み込みを持つため、MIRレベルでの静的展開が自然）。SVの `generate` を出力するのではなく、Cm側で展開してから通常経路で出力する方が実装が単純。

**難易度**: 中

## 7. アサーション（SVA）

> **✅ 2026-07-05 即時アサーション実装済み**: 標準ライブラリに `std::debug::assert` を追加。
> `import std::debug::assert;` して `assert(cond, "msg")`（2引数）で使用する。
> - SV: 即時アサーション `assert (cond) else $error("assertion failed: msg");` を出力
>   （シミュレーションで検証され、合成では無視される）
> - 実行系 (JIT/LLVM/WASM): 失敗時 `assertion failed: msg` を出力して exit(1)
> - JS: `Error` を throw
> 回帰テスト: `tests/sv/simulation/assert_immediate`、`tests/common/basic/assert_pass`、
> `tests/common/errors/assert_fail`、ユニットテスト `ImmediateAssertion`。
> `assert property`（時相プロパティ検証）は未実装。

**現状**: 即時アサーションのみ。プロパティ検証（SVA）は未対応。

**実装案（残り）**: `assert property (@(posedge clk) expr);` 形式の時相アサーション対応。

**難易度**: 中（時相式の構文設計が必要）

## 8. トライステート（`'z`）とCDC同期化プリミティブ

**現状**: `inout` ポート方向のみ対応。Z値リテラル・ハイインピーダンス制御は不可。クロックドメイン間の同期化（2FF synchronizer）は手書き。

**実装案**:
- `4'bz` 形式のZ値リテラルをSVスタイルリテラルの拡張として許可
- `#[sv::sync(2)]` 属性でクロックドメインをまたぐ信号に自動で2FF同期段を挿入

**難易度**: 中

## 9. lint_off の段階的削除

**現状**: 生成SVは `WIDTHTRUNC` / `WIDTHEXPAND` / `UNDRIVEN` / `UNUSED` を一括 lint_off しており、幅不一致系の欠陥を自ら隠している。今回の調査で発覚したバグ群（キャスト無視・符号付き定数など）の多くは lint_off がなければ早期に検出できた。

**実装案**: キャスト明示出力（実装済み）により WIDTHTRUNC/WIDTHEXPAND の抑止は原理的に不要になったため、テストスイートで lint_off を1つずつ外して警告を潰す。最終的に `--sv-strict-lint` をデフォルト化する。

**難易度**: 低〜中（地道な警告潰し）

## 10. sv codegen のユニットテスト整備

> **✅ 2026-07-04 実装済み**: `tests/unit/sv_codegen_test.cpp` を追加
> （Cmソース断片→生成SV文字列のゴールデンテスト9件: 符号付き定数・優先順位括弧・
> 縮小キャスト・算術シフト・enum幅・レジスタ初期値・whileループ再構成・
> 配列initial・配列ポート次元）。CMakeのコンポーネント別ソースリストを流用して
> `sv_codegen_test` ターゲットとして定義、`ctest -L unit` で実行される。

## 11. 式ツリーベースのSV出力への転換（長期）

**現状**: svバックエンドは「一度SVテキストを出力してから文字列操作で修正する」設計（一時変数インライン展開・else-if正規化・ternary変換・always種別推論がすべて文字列 `find`/`replace`）。優先順位括弧バグはこの設計の必然的な帰結だった。

**実装案**: MIRから式ツリー（またはSV用の小さなAST）を構築し、優先順位を持つプリティプリンタで一括出力する。ラッチ推論は「行内の `if (` 数を数える」テキストヒューリスティックではなく、MIRの代入完全性解析（どのパスでも全信号が代入されるか）に置き換える。

**難易度**: 高（バックエンドの再設計。ただし1〜10の実装を安全に積み上げる土台になる）

---

## 補足: 言語コア側の既知バグ（2026-07-04調査）

svバックエンド以外で、同日の調査により以下の重大バグが確認されている（詳細な再現コードは調査記録参照）。これらは別途対応が必要:

1. ~~**関数内からのグローバル変数への単純代入が無視される**~~ → **✅ 2026-07-04 修正済み**。
   根本原因は2つ: (a) `DeadCodeElimination::remove_dead_stores` がグローバル/static変数への
   ストアを「関数内で読まれていない」という理由で削除、(b) SCCPがグローバルの定数値を
   関数呼び出しをまたいで伝播。DCEでグローバル/staticを常に使用扱いにし、
   SCCPの `can_bind_constant` でグローバル/staticの束縛を禁止した。
   さらに2026-07-05、JSバックエンドがグローバル変数を関数ごとのローカル `let` として
   複製していた欠陥も修正（モジュールレベルの `__global_<name>` 宣言に一本化）。
   回帰テスト: `tests/common/basic/global_assign`
2. ~~**演算結果の格納値が型幅に切り詰められず、表示値と比較値が食い違う**~~ → **✅ 2026-07-05 修正済み**。
   SCCP/ConstantFoldingの定数畳み込みがint64のまま値を伝播していた。
   共有ヘルパー `const_eval.hpp` を新設し、(a) 畳み込み結果を結果型
   （`BinaryOpData.result_type`、無ければ幅の広い方へ昇格）の幅・符号に正規化、
   (b) 代入時に代入先ローカルの型幅へ正規化（狭い型への代入はラップ）するようにした。
   回帰テスト: `tests/common/basic/int_width_wrap`
3. ~~**unsignedセマンティクスの崩壊**~~ → **✅ 2026-07-05 修正済み**。
   (a) 定数畳み込み: 符号なし型の比較・除算・剰余・右シフトを符号なしで評価、
   (b) LLVM codegen: `convertBinaryOp` にオペランドHIR型を渡し
   udiv/urem/lshr/icmp u{lt,le,gt,ge} を選択、拡張もzext/sextを符号で切り替え、
   (c) キャストの拡張をソース型の符号で決定（`utiny 255 as int` が -1 になる欠陥を修正）、
   (d) JS codegen: 符号なし右シフトを `>>>`、uintの加減乗・左シフトを `>>> 0` で符号なし化。
   既存テスト `ulong_large_hex` の期待値はバグを焼き込んでいたため修正
   （`ulong >> 63` は論理シフトで 1 が正しい）。
   回帰テスト: `tests/common/basic/unsigned_semantics`
4. ~~**文字列補間内の式 `{a + b}` 等がゴミ値を出力**~~ → **✅ 2026-07-05 修正済み**。
   MIR loweringの補間プレースホルダ解析が変数名パターン専用で、
   演算子を含む式は未初期化テンポラリのまま渡されていた。
   演算子を含む内容は本物のフロントエンド（Lexer→Parser→HirLowering）で
   式としてパースし、通常の式loweringに掛けるフォールバックを追加した。
   回帰テスト: `tests/common/basic/interp_expression`
5. ~~**配列要素・構造体フィールドへの `++`/`--` が無効**~~ → **✅ 2026-07-04 修正済み**。
   `lower_unary` のinc/decが `HirVarRef` 以外を黙って `const 0` に置換していた。
   Index/Member/Deref（多次元・ポインタ自動deref含む）のprojection付きplaceを構築して
   read-modify-write を生成するようにした（後置の旧値返却も対応）。
   回帰テスト: `tests/common/basic/incdec_place`
6. ~~**整数ゼロ除算がトラップせずゴミ値**~~ → **✅ 2026-07-05 修正済み**。
   LLVM codegenの整数div/remにゼロチェック分岐を挿入し、除数0で
   `integer division by zero` を出力して exit(1) するようにした
   （非ゼロ定数除数はチェックを省略）。JSバックエンドも同名エラーをthrow。
   WASMランタイムに `exit`（proc_exit委譲）を追加。
   回帰テスト: `tests/common/errors/div_by_zero`
7. 構造体は関数へ参照渡し・配列は値渡しという**引数渡し規約の不整合**（設計判断の明文化が必要）
