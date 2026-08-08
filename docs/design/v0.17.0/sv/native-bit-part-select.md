# SV-N1: native ビット選択・部分選択の出力（`[hi:lo]` / `[+:]` / `[-:]` ・部分代入）

**分類:** idiom改善（合成結果は等価だが可読性・ツール互換）＋ 一部新機能（`[-:]`）
**優先度:** High
**ステータス:** 未実装（v0.17.0 SVギャップ調査で検出）

## 現状（実測: cm 2026-08-08ビルド、プローブ `.tmp/svgap/`）

Cmはビットスライス・部分選択を**shift+maskへ降下**し、SVの native part-select 構文（`[hi:lo]`・`[base +: width]`・`[base -: width]`）を出力しない。合成結果の論理は等価だが、生成SVが冗長で読みにくく、一部の合成/lintツールは native part-select の方を最適に扱う。

| Cm | 生成される現状SV | 望ましいnative SV |
|----|------------------|-------------------|
| `hi = din[7:4]`（定数範囲・読み） | `hi = din >> 32'sd4 & 32'sd15;` | `hi = din[7:4];` |
| `nib = word[i +: 4]`（int基点・読み） | `nib = word >> i & 32'sd15;` | `nib = word[i +: 4];` |
| `word[7:4] = v`（部分代入） | `word = word & -8'sd241 \| (v & 32'sd15) << 32'sd4;` | `word[7:4] = v;` |
| `word[i -: 4]`（下降インデックス） | — | `word[i -: 4]`（現状は生成不能） |

補足: 定数範囲ビットスライス読みは `tests/sv/basic/bitslice.cm` でSIM値検証されている（shift+mask経由で値は正しい）。可変基点`+:`はbit型基点だと「the base of a part-select must be an integer type」で拒否される（int基点なら受理されshift+maskへ）。

## 提案

コード生成（`src/internal/codegen/sv/` の式lowering・emit_control.cpp / codegen.cpp）で、対象がSVレジスタ/信号（`bit[N]`・整数幅の`logic[N:0]`）へのビット範囲アクセスのとき、shift+maskでなく native part-select を出力する:

- **定数範囲読み**: `x[hi:lo]`（`hi`/`lo`がコンパイル時定数）
- **インデックス付き部分選択読み**: 基点が変数・幅が定数なら `x[base +: width]`。下降方向 `x[base -: width]` も受理・出力（現状は未対応）
- **部分代入（書き）**: 左辺がビット範囲のとき、read-modify-maskでなく `x[hi:lo] <= v;` / `x[base +: w] <= v;`（ノンブロッキング/ブロッキングは既存の代入方式判定に従う）
- 基点の型は`bit[N]`も整数として許容する（現状のbit基点拒否を解消）

定数畳み込みが効く単一ビット選択`x[i]`は既に native（`arr[i]`）で出ているため、範囲版を同じ経路へ寄せる。合成等価性はVerilator/iverilogで既存bitsliceテストと値一致を確認する。

## 実装方針

1. ビット範囲アクセスのMIR/HIR表現を、shift+mask降下の前段で「part-selectノード」として保持する（現状は算術式へ早期降下している疑い。lowering箇所を特定して分岐）。
2. SV式ツリー（`expr_tree.cpp` の`SVExpr`）へ part-select ノード種別を追加し、`[hi:lo]`/`[+:]`/`[-:]` を出力する。
3. 部分代入は代入lowering（codegen.cpp:1281-1309付近）で左辺がビット範囲なら native 左辺part-selectを出力する。
4. 非SVバックエンド（native/jit/wasm/js）は従来のshift+mask/整数演算を維持（part-selectはSV専用出力）。

## テスト計画

`tests/sv/basic/` へ: 定数範囲読み・可変基点`+:`読み・`-:`読み・部分代入の各々が native part-select を出力し（生成SVのgrep）、かつiverilogシミュレーションで値がshift+mask版と一致することを確認。bit基点`+:`が受理されることの回帰。`.expect`はSIM_OK＋値行。
