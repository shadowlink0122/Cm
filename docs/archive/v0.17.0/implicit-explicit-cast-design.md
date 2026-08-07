---
title: 暗黙変換と明示キャストの設計整理（Z5）
parent: v0.17.0 Design
---

# 暗黙変換と明示キャストの設計整理（Z5）

## 概要

「拡大変換は暗黙・縮小/意味変化は明示`as`」という設計意図（チュートリアルvariables.mdは`double→int`を不可と明記）に対し、現実装の`types_compatible`は全数値型を相互互換として受理しており、縮小変換まで暗黙で通る。
さらに暗黙変換の「受理」と「変換命令の挿入」が分離しているため、受理されたのに変換が挿入されず、ビット再解釈のゴミ値やバックエンド分裂になる組み合わせが存在する。
stdlibには本来不要な`as`が多数あり、その一部は壊れた暗黙昇格（Y4）の回避策として書かれている。設計と実装と利用側の三者を揃えるリファクタリングが必要である。

## 現状マトリクス（調査時点・let初期化文脈の実測）

| 変換 | 受理 | 実行時の値 | 評価 |
|---|---|---|---|
| int→long / short→int / int→double / int→float / float→double | 暗黙OK | 正しい | 設計どおり（拡大） |
| int literal→short/double（適合値） | 暗黙OK | 正しい | 設計どおり（リテラル適合） |
| long→int | 暗黙OK | ビット切り詰め（5000000001→705032705） | **設計違反**（縮小が無診断） |
| **double→int** | 暗黙OK | **ゴミ値・バックエンド分裂**（2.9→jit:-1342177280 / native:858993459 / js:2.9素通し） | **設計違反＋変換命令欠落**（fptosi未挿入のビット再解釈） |
| double→float | 暗黙OK | 丸め（正しい） | 設計違反（縮小が無診断、値は正しい） |
| int→short | 暗黙OK | ビット切り詰め（70000→4464） | 設計違反（縮小が無診断） |
| int↔uint | 暗黙OK | ビット再解釈 | 要仕様決定（符号解釈変化） |
| int→char / char→int / int↔bool / char→string | `as`必須 | — | 設計どおり（意味変化の境界は機能している） |

追加調査で、**関数引数のdouble→intはLLVMモジュール検証エラー（コンパイル失敗）**、return文脈もjsが2.9素通しになることを確認した（let/代入/引数/returnの4文脈すべてで変換命令が欠落）。

## 実装記録（2026-08-05）

### 1. 変換の意味論を1箇所に定義（`classify_numeric_conversion`）

`src/internal/types/checking/utils/conversion.cpp` を新設し、数値変換を「Identity／Widening（拡大・暗黙可）／Narrowing（縮小・要as）／SignChange（符号解釈変化・要as）」に分類する`classify_numeric_conversion(target, source)`を型検査へ追加した。
拡大=同符号の幅拡大・符号なし→広い符号付き・整数→浮動小数・float→double。縮小=幅縮小・浮動小数→整数・double→float。符号変化=符号付き→符号なし・同幅の符号違い。
仕様はCANONICAL_SPEC.md 10.3に変換表として明文化し、チュートリアルvariables.md（日英）を実装に一致させた。

### 2. 縮小/符号変化の段階的診断（警告→--strictエラー）

受理サイト（let初期化・代入・複合代入・return）で上記分類のNarrowing/SignChangeを警告し、`check/lint --strict`ではエラーへ昇格する（H6と同じ段階導入運用）。
宛先に適合するリテラル（`short s = 5;`・`uint u = 7;`・`float f = 2.5;`・`ulong u = 0xFFFFFFFFFFFFFFFF;`）は診断しない（単項マイナス1段を剥がして範囲判定。2^63以上の素のリテラルはulongの意図が明確なため適合扱い、明示的負値のみ診断）。
**符号なし整数→符号付き整数（uint/usize→int）は現段階では診断しない**と仕様決定した: `len()`/`cap()`/`sizeof`等がuint/usizeを返し「`int n = arr.len()`」が言語全体の既存イディオムであるため（試行時にテストスイートだけで20ファイル超が警告）。2^31超の縮小リスクは--strictでの診断化を将来課題とする。
floatオペランド×浮動小数リテラルの二項演算（`return v / 2.0;`）はdouble昇格でなくリテラル側をfloatへ適合させるようinfer_binaryを拡張し、戻り値での縮小警告の誤検出を防いだ（演算もユーザー意図どおりfloat幅になる）。

### 3. 暗黙変換の変換命令挿入（受理と挿入の一致）

MIR loweringの`coerce_to_float_context`（B2: 整数→浮動小数宛先のsitofp挿入）を`coerce_numeric_context`へ一般化し、**浮動小数→整数宛先のfptosi/fptoui相当Cast**も挿入するようにした。
適用サイトは既存のcoerce適用点そのまま（let初期化・代入・複合代入・関数引数・デフォルト引数・return・構造体フィールド初期化）で、これによりdouble→intのゴミ値（let/代入）・LLVM検証エラー（引数）・js素通し（全文脈）が解消し、全バックエンドがfptosi切り捨てで一致する。
整数同士の幅違いは従来どおりコード生成の幅合わせ（2の補数ラップ）が機能しているため変換Castは挿入しない。

### 4. stdlibの回避策`as`削減

Y4修正済みのため、`std::json`の`(digit as double)`2箇所と`std::io::console::input`の`(c - 48) as long`を素の混合演算へ戻した（値検証済み）。
`unwrap_or(0 as long)`等のジェネリック型引数を固定するための`as`・ポインタ/型消去の`as`は正当なため残した。

### 5. テスト

- `tests/common/casting/numeric_conversion_contexts.cm`: 引数/let/代入/return/構造体フィールドのfloat↔int変換値・拡大変換値・uint→int読み出し・明示asの縮小値・複合代入を7実行経路（jit・native O0/O2・wasm O0/O2・js・ts）で一致検証。
- `tests/i18n/z5_narrowing.cm` + expects 3ケース: checkでの縮小/符号変化警告（en）・--strictエラー昇格（en/ja）・適合リテラルとuint→int読みが無診断であることの negative check。
- `tests/common/basic/int_width_wrap.cm`: `tiny t2 = t + 1`（演算結果int）の縮小格納を明示`as tiny`へ更新。

## 将来課題

- 関数引数・構造体フィールド・push要素など残り文脈への縮小/符号変化診断の拡大（変換命令は挿入済み、診断のみ未適用）。
- 符号なし→符号付き整数（uint/usize→int）の--strictでの診断化（len/sizeofイディオムの扱いとセットで再検討）。
- 整数縮小への明示Cast挿入の統一（現在はコード生成の幅合わせ依存。値は正しいが経路の一元化が残る）。

## 検出経緯

レイヤー別レビュー追補（暗黙/明示キャスト設計調査）で作成。実測は `.tmp/bughunt4/cast_probe.cm` / `cast_rt.cm`。
