# SV-N1: native ビット選択・部分選択の出力（`[hi:lo]` / `[+:]` / `[-:]` ・部分代入）

**分類:** idiom改善（合成結果は等価だが可読性・ツール互換）＋ 一部新機能（`[-:]`）
**優先度:** High
**ステータス:** 実装済み（native出力＋`-:`新構文＋bit基点許容）

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

## 実装記録

提案の4項目（定数範囲読み・インデックスド読み`+:`/`-:`・部分代入・bit基点許容）を全て実装した。実装方針の「part-selectノード保持」はMIR/SVExprのノード追加でなく、HIR loweringのSVターゲット分岐でpart-select専用ビルトイン呼び出しへ落とし、SVコード生成のCallターミネータ写像で構文を出力する方式（SV-N2/N3と同じ機構）を採った。

- **構文（`-:`）**: レキサへ `MinusColon` トークンを追加し、パーサのインデックスドパートセレクト分岐を `+:`/`-:` 両対応にした（`ast::SliceExpr::part_select_down`）。`-1` 等の負数リテラルは `-` の直後が `:` でないため既存の字句解釈と衝突しない。
- **型検査**: `-:` は `+:` と同じ検査（基点=整数式・幅=1〜64の整数リテラル）を共有する。基点の型検査を拡張し、`bit`/`bit[N]` を整数として許容した（従来は `TypeTheBaseOfAPart` で拒否）。
- **HIR lowering**（`expr.cpp`）: 読みは `__builtin_sv_range_select(x, hi, lo)`・`__builtin_sv_part_select[_down](x, base, w)`、部分代入は `__builtin_sv_range_assign(x, hi, lo, v)`・`__builtin_sv_part_assign[_down](x, base, w, v)`（値を返さない代入文）へ落とす。非SVターゲットは従来のshift+mask脱糖を維持し、`-:` のシフト量は `base-(w-1)`。`#[test]`関数・initialブロックはSVテストベンチ生成がHIR式を直接消費するため、`hir_retained_context_` フラグで脱糖を抑止し従来のshift+mask式を保つ。
- **SVコード生成**（`emit_control.cpp`）: Callターミネータの各ビルトインを `dest = x[hi:lo];`・`dest = x[base +: w];`・`x[hi:lo] <= v;` 等へ写像する。基点が定数の場合はサイズ無し10進で出力（`32'sd7` でなく `7`）。部分代入のブロッキング/ノンブロッキングは対象信号のルートlocal（copy/ref逆引き）から通常代入と同じ規則（async/always_ffのグローバル・posedgeパラメータ関数のフォールバック込み）で判定する。
- **テスト**: `tests/sv/basic/bit-ops/partselect.cm`（定数範囲・`+:`・`-:`・部分代入のiverilogシミュレーション、2入力ベクタ）・`tests/regression/cases/sv/expr/partselect_native.cm`＋`SVCodegenTest.NativePartSelect`（native構文の出力とshift+mask非残存のgrep検証。bit[N]判定に型注釈が要るため型チェッカ経由の `compile_to_sv_checked` を新設）・`tests/common/basic/arrays-slices/partselect_down.cm`（`-:` の非SVバックエンド一致・`+:`との等価性・下降部分代入）。既存 `bitslice.cm`（SIM）・`bitslice.cm`（common）は無変更で通過し、意味論の不変を確認した。
- **範囲外**: SVExprへのpart-selectノード種別追加（提案の実装方針2）は、Callターミネータ写像で全ケースが賄えるため不要と判断した。読み結果は一時信号経由（`_t = x[7:4]; y <= _t;`）になるが合成等価で、SVの可読性目標（native構文の出力）は満たしている。
