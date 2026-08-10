# SystemVerilogバックエンド 機能ギャップ調査と新規実装項目（索引）

Cmの`--target=sv`（合成可能RTL＋テストベンチ自動生成）が、SystemVerilogの構文・機能をどこまで生成できるかを全面調査し、未対応の新規実装項目をまとめた。調査は3系統（コード生成`src/internal/codegen/sv/`の精読・`tests/sv/`とチュートリアルの実証範囲・`validation.cpp`とドキュメントの明示制限）を突き合わせ、主要ギャップは`cm compile --target=sv`で実機裏取りした。

対象はFPGA向け合成可能サブセットとテストベンチ生成。**非目標**（下記）は本調査の対象外とする。

## 凡例

- ✅ 対応（native SVを生成、テストまたはチュートリアルで実証）
- 🟡 機能は動くが native SV構文でない（shift+mask等へ降下＝合成結果は等価だが非idiomatic）
- 🔴 表現手段なし（新規実装項目）
- ⛔ 設計上の非目標（対象外）

## カバレッジ・マトリクス

### データ型
| 機能 | 状態 | 備考 |
|------|------|------|
| logic/signed/unsigned・整数幅写像・`bit[N]`・unpacked配列・多次元配列・typedef enum | ✅ | |
| packed struct | ✅ | 既定packed・`#[sv::unpacked]`でunpacked出力を選択可能（SV-N8で実装） |
| packed union | 🔴 | → [SV-N6](packed-union.md) |
| 2-state `bit`型・native `reg`宣言 | ⛔ | 現状維持を決定（logicがregを包含・合成同一） → [SV-N8](../../../archive/v0.17.0/sv/misc-synth-gaps.md) |

### 式・ビット操作
| 機能 | 状態 | 備考 |
|------|------|------|
| 連接`{}`・複製`{n{}}`・三項・算術右シフト`>>>`・`$signed`/`$unsigned`・サイズ付きリテラル | ✅ | |
| ビットスライス`x[hi:lo]`（読み） | ✅ | native `[hi:lo]` を出力 → [SV-N1](../../../archive/v0.17.0/sv/native-bit-part-select.md)（実装済み） |
| インデックス付き部分選択`x[i +: w]`（読み）・部分代入`x[hi:lo] = v` | ✅ | native `[+:]`・左辺part-select代入を出力 → [SV-N1](../../../archive/v0.17.0/sv/native-bit-part-select.md)（実装済み） |
| `x[i -: w]` | ✅ | 下降方向を新構文として追加（非SVはshift+mask脱糖） → [SV-N1](../../../archive/v0.17.0/sv/native-bit-part-select.md)（実装済み） |
| リダクション演算子`&x`/`\|x`/`^x`/`~&`/`~\|`/`~^` | ✅ | 組み込み関数 `reduce_and/or/xor/nand/nor/xnor` → [SV-N2](../../../archive/v0.17.0/sv/reduction-operators.md)（実装済み） |
| 型キャスト`type'(expr)` | ✅/🟡 | struct分は`Pair'(bits)`出力・enum分はenum同一性の正規化遅延待ち（分岐実装済み）。ストリーミング演算子は非対応 → [SV-N8](../../../archive/v0.17.0/sv/misc-synth-gaps.md) |

### 制御構文
| 機能 | 状態 | 備考 |
|------|------|------|
| if/else if/else・`unique case`・while再構成・break(disable方式)・don't-careパターン | ✅ | |
| `casez`（native） | ✅ | don't-care matchはnative `casez` を出力（互いに素→unique・重なり→priority自動選択） → [SV-N3](../../../archive/v0.17.0/sv/casez-casex-priority.md)（実装済み） |
| `priority`/`unique0` case修飾 | ✅ | `#[sv::priority]`/`#[sv::unique0]` 属性で切替 → [SV-N3](../../../archive/v0.17.0/sv/casez-casex-priority.md)（実装済み。casexは合成非推奨のため非出力） |

### モジュール構造
| 機能 | 状態 | 備考 |
|------|------|------|
| module/ポート(in/out/inout)・`#(parameter)`・localparam・名前付きポート接続・階層(export IO struct) | ✅ | |
| SV `function automatic`（戻り値あり関数） | ✅ | |
| generate/genvar/for-generate/if-generate | 🔴 | 定数ループ展開が部分代替 → [SV-N4](generate-genvar.md) |
| パラメータ幅メモリ配列`bit[WIDTH][DEPTH]`（ロードマップA6）・パラメータ依存ループ展開(A5) | 🔴 | → [SV-N4](generate-genvar.md) |
| モジュールインスタンス配列・位置ベースポート接続 | 🔴 | → [SV-N5](module-instance-arrays.md) |
| SV `task`（自動生成） | ⛔ | 見送りを決定（function automaticで代替・1関数=1プロセス設計を維持） → [SV-N8](../../../archive/v0.17.0/sv/misc-synth-gaps.md) |

### メモリ・属性・実機I/O
| 機能 | 状態 | 備考 |
|------|------|------|
| 内部配列/RAM・配列初期化・`$readmemh`・`#[sv::bram/lutram/memfile]`・`--emit-memfile` | ✅ | |
| ピン制約`#[sv::pin]`(.cst/.xdc/.tcl)・トライステート`#[sv::tri]`・CDC同期`#[sv::sync]` | ✅ | 正常系の統合テストは薄い（チュートリアルのみの項目あり） |
| `$readmemb` | ✅ | `#[sv::memfile(..., radix: bin)]`＋`--emit-memfile`の2進出力 → [SV-N8](../../../archive/v0.17.0/sv/misc-synth-gaps.md)（実装済み） |

### テストベンチ・検証
| 機能 | 状態 | 備考 |
|------|------|------|
| TB自動生成(`//! test:`)・`#[test]`刺激関数+`step()`・即時アサーション`assert(...) else $error`・`$display`/`$finish`/`$dumpvars`・クロック生成 | ✅ | （`//! test:`期待値の非検証は別途R15で対応中） |
| 並行アサーション`assert property`/`sequence`/`property` | 🔴 | → [SV-N7](concurrent-assertions-sva.md) |
| `$time`・`final`ブロック | ⛔ | 見送りを決定（$display＋$finishで代替） → [SV-N8](../../../archive/v0.17.0/sv/misc-synth-gaps.md) |

## 新規実装項目（優先度順）

| ID | 項目 | 優先度 | 分類 |
|----|------|--------|------|
| [SV-N1](../../../archive/v0.17.0/sv/native-bit-part-select.md) | native ビット選択・部分選択の出力（`[hi:lo]`/`[+:]`/`[-:]`・部分代入・実装済み） | High | idiom（合成結果は等価だが可読性・ツール互換） |
| [SV-N2](../../../archive/v0.17.0/sv/reduction-operators.md) | リダクション演算子（組み込み関数＋SV出力・実装済み） | High | 新機能 |
| [SV-N3](../../../archive/v0.17.0/sv/casez-casex-priority.md) | `casez`とpriority/unique0 case修飾（実装済み） | High | idiom＋新機能 |
| [SV-N4](generate-genvar.md) | generate/genvar・パラメータ幅配列・パラメータ依存ループ展開 | Medium | 新機能（A5/A6） |
| [SV-N5](module-instance-arrays.md) | モジュールインスタンス配列・位置ベースポート接続 | Medium | 新機能 |
| [SV-N6](packed-union.md) | packed union（ビット再解釈） | Low | 新機能 |
| [SV-N7](concurrent-assertions-sva.md) | 並行アサーション（SVA property/sequence） | Medium | 検証機能 |
| [SV-N8](../../../archive/v0.17.0/sv/misc-synth-gaps.md) | 小粒ギャップ集（$readmemb・type'(expr)・#[sv::unpacked]は実装、task/$time/final/reg・2-stateは判断記録で完了） | Low | 混在 |

## 設計上の非目標（⛔ 対象外・実装しない）

出典: `docs/tutorials/ja/compiler/sv/semantics.md`・`docs/design/backend_support_matrix.md`。Cmに構文が無く表現しない、または合成不能:

`force`/`release`・`specify`ブロック・UDP・信号強度(strength)・`fork`/`join`・イベント(event)・DPI-C・SV `interface`/`modport`（Cmのinterfaceとは別概念。階層＋構造体で代替）・遅延`#10`（TB生成内部でのみ使用）・`clocking block`・`package`（importはフラット化）・class（合成不能）・タグ付きunion（ペイロード付きenum）・クロージャ/ラムダ・動的配列/スライス・浮動小数・ポインタ。

型検査を通過した非対応構文がコード生成に到達した場合はSV007で明示エラーになる。

## 調査の裏取りメモ

主要ギャップは実機（cm 2026-08-08ビルド）で確認済み:
- `din[7:4]` → `hi = din >> 32'sd4 & 32'sd15;`（shift+mask、native `[7:4]`非出力）
- `word[i +: 4]`（int基点）→ `nib = word >> i & 32'sd15;`（同上。bit基点は「base of a part-select must be an integer type」で拒否）
- `word[7:4] = v` → `word = word & -8'sd241 | (v & 32'sd15) << 32'sd4;`（read-modify-write）
- `switch`（default/else付き）→ `unique case (...) ... default: ... endcase`（priorityは非出力）
- don't-care `0b1?00` → `(op & 32'd11) == 32'd8 ? ...`（native casez非出力）
- リダクション・generate・インスタンス配列・packed union・並行アサーションは生成箇所ゼロ（3系統調査で一致）
