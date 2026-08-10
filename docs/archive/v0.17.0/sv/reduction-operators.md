# SV-N2: リダクション演算子（`&x` / `|x` / `^x` / `~&` / `~|` / `~^`）

**分類:** 新機能（Cm構文＋SV出力）
**優先度:** High
**ステータス:** 実装済み（案A＝組み込み関数を採用）

## 現状

SystemVerilogのリダクション演算子（ベクタ全ビットを1ビットへ畳み込む単項演算）を**Cmで表現する構文がない**。コード生成の単項演算はNeg/Not/BitNotのみ（`codegen.cpp:1028-1036`）。パリティ・全ビットAND/OR等のRTL頻出イディオムが書けず、ユーザーは手動でループやビット毎の式に展開するしかない。

対象演算子:
| SV | 意味 |
|----|------|
| `&x` | 全ビットAND（全ビット1で1） |
| `\|x` | 全ビットOR（1ビットでも1で1） |
| `^x` | 全ビットXOR（パリティ） |
| `~&x` / `~\|x` / `~^x` | それぞれの否定（NAND/NOR/XNORリダクション） |

## 提案

Cmにリダクションを表現する構文を追加し、SVへ native リダクション演算子として出力する。二項演算子（`&`/`|`/`^`）と衝突しない単項プレフィックス構文が必要。案:

- **案A（組み込み関数）**: `reduce_and(x)` / `reduce_or(x)` / `reduce_xor(x)` / `reduce_nand(x)` / `reduce_nor(x)` / `reduce_xnor(x)`。構文追加不要でパーサ変更が最小。非SVバックエンドでも意味を定義できる（ループ畳み込み）。
- **案B（単項演算子構文）**: SVと同形の `&x`/`|x`/`^x` を単項として受理。ただしCmの`&`はアドレス取得と衝突するため、SVターゲット限定の字句/文法分岐が要る。曖昧性が高い。

**推奨は案A**（組み込み関数）。SVターゲットでは native リダクション演算子（`&x`等）へ写像し、他バックエンドではビット幅ぶんのループ/畳み込みで同値を計算する（移植性のため）。戻り値は1ビット（`bool`/`bit[1]`）。

```cm
// 案A
bit[8] flags = ...;
bool all_set = reduce_and(flags);   // SV: assign all_set = &flags;
bool parity  = reduce_xor(flags);   // SV: assign parity  = ^flags;
```

## 実装方針

1. 組み込み関数`reduce_and/or/xor/nand/nor/xnor`を型検査へ登録（引数は整数/`bit[N]`、戻り値`bool`）。ランタイムビルトインレジストリ（runtime-builtin-registry）へ追加し、非SVバックエンドの実装（ループ畳み込み）を用意。
2. SVコード生成の単項演算出力（`codegen.cpp:1028-1036`）へリダクション種別を追加し、`&`/`|`/`^`/`~&`/`~|`/`~^` を前置出力（括弧は`expr_tree`の優先順位規則に従う）。
3. VSCode拡張の組み込みメソッド/関数レジストリ（`builtins.ts`）にも追記する。

## テスト計画

`tests/sv/basic/` へ: 6種のリダクションが native SV演算子を出力し、iverilogシミュレーションで既知入力に対し正しい1ビット結果を返すことを検証（例: `reduce_and(8'hFF)=1`・`reduce_xor(8'h07)=1`）。非SVバックエンド（jit）でも同値を返す移植性テストを`tests/common/`へ。

## 実装記録（案A＝組み込み関数を採用）

案A（組み込み関数）を実装した。二項演算子（`&`/`|`/`^`）との衝突を避け、非SVバックエンドでも意味を定義できる方針を優先した。

- **型検査**（`types/checking/call/function.cpp`）: `reduce_and/or/xor/nand/nor/xnor` を組み込み自由関数として登録した。引数は1個・整数型または `bit[N]` 型（単一 `bit` 含む）に限定し、非整数は診断 `TcReductionArgMustBeBits`（「引数は整数型または bit[N] 型である必要があります」）で停止する。戻り値は `bool`。
- **HIR lowering**（`hir/lowering/expr.cpp` の `lower_reduction`）: ビルトインをターゲットで分岐させた。`HirLowering::set_sv_target`（`build.cpp` が `--target` またはソースの `//! platform: sv` から設定）が真のとき、native 出力用に `__builtin_reduce_*` 呼び出しを残す。非SVでは被演算子の型幅 `W` を求め、`reduce_and`=`(x & mask) == mask`・`reduce_or`=`(x & mask) != 0`・`reduce_nand`/`reduce_nor` はその否定形、`reduce_xor`/`reduce_xnor` は `XOR_{k<W} ((x>>k) & 1)` のパリティ畳み込みへ脱糖する（LLVM/JSが既に扱える純粋な整数演算のみを生成し、バックエンド追加改修は不要）。パリティは被演算子を幅ぶん評価するため、`reduce_xor`/`reduce_xnor` の被演算子には副作用のない式を渡す前提とした。
- **SVコード生成**（`codegen/sv/emit_control.cpp`）: Call ターミネータの `__builtin_reduce_*` を native 前置単項リダクション演算子（`&x`/`\|x`/`^x`/`~&x`/`~\|x`/`~^x`）へ写像した（連接・複製の既存分岐と同じノンブロッキング/ブロッキング代入判定を共用）。
- **VSCode拡張**: 文法の組み込み関数一覧（`grammar/terms.ts`）と補完・ホバーのビルトインレジストリ（`navigation/builtins.ts`）へ6関数を追記し、`syntaxes/cm.tmLanguage.json` を再生成した。
- **テスト**: `tests/sv/basic/operators/reduction.cm`（native SV演算子出力＋iverilogシミュレーションの真理値検証、2入力ベクタ）・`tests/common/basic/numeric/reduction.cm`（interpreter/llvm/js 一致の移植性検証・幅の効きを含む19行）・`tests/common/errors/numeric/reduction_arg.cm`（非整数引数の診断）。
- 範囲外（案Aの推奨どおり見送り）: 案B（`&x` 等の単項演算子構文）はCmの `&`（アドレス取得）と衝突するため採用しない。SV出力ポートは `bool` 型で受ける（`bit` ポートへは代入型不一致になるため）。
