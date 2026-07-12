# バックエンド対応マトリクス

作成日: 2026-07-11（v0.16.0）
本文書は、Cm言語の構文・機能がどのバックエンドでサポートされるかを宣言する単一情報源である。
文法（[cm_grammar.md](cm_grammar.md)）と正式仕様（[CANONICAL_SPEC.md](CANONICAL_SPEC.md)）は言語を単一に定義するが、バックエンドごとの適用可否は本文書が定義する。

## 記号

- ✅ 対応（共通テストまたは専用テストで検証済み）
- ⚠️ 制限つき（注記の条件でのみ動作）
- ❌ 明示エラー（コンパイル時にエラーコード付きで停止）
- ― 対象外（設計上サポートしない。エラーになるとは限らないため使用しないこと）

## 言語コア構文

| 構文 | JIT | Native | WASM | JS | SV | UEFI | baremetal |
|---|---|---|---|---|---|---|---|
| 制御フロー（if/while/for/match） | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 関数・オーバーロード | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| struct / impl / interface | ✅ | ✅ | ✅ | ✅ | ⚠️ 合成可能な範囲（メソッドは function automatic 化） | ✅ | ✅ |
| 自動実装 `with` / `#[derive]`（Eq/Ord/Copy/Clone/Hash/Debug/Display/Css） | ✅ | ✅ | ✅ | ✅ Cssはjs/web専用 | ⚠️ Eq/Ord等の演算子は式写像の範囲、Debug/Displayはstring制約（SV005）に従う | ⚠️ 未検証（string出力はテキストAPI経由） | ⚠️ 未検証（Debug/Displayは出力手段なし） |
| enum（タグのみ） | ✅ | ✅ | ✅ | ✅ | ✅ typedef enum化 | ✅ | ✅ |
| enum（ペイロード付き / tagged union） | ✅ | ✅ | ✅ | ✅ | ❌ 非合成型 | ✅ | ✅ |
| ジェネリクス | ✅ | ✅ | ✅ | ✅ | ⚠️ `#[sv::parameter]` によるモジュールパラメータ写像 | ✅ | ✅ |
| クロージャ / ラムダ | ✅ | ✅ | ✅ | ✅ | ❌ 非合成 | ✅ | ✅ |
| マクロ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `#ifdef` 条件付きコンパイル / `-D` 定義 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `#[target(...)]` 宣言フィルタ | ✅ | ✅ | ✅ | ✅ | ―（platform: svで分離） | ✅ | ✅ |
| `#[test]` / `cm test` | ✅ JIT実行 | ✅ | ✅ | ✅ | ✅ TB生成+iverilog実行 | ― | ― |
| インラインアセンブリ `__asm__` | ✅ | ✅ | ⚠️ WASM命令のみ | ❌ 実行不可 | ― | ✅ | ✅ |

## 型

| 型 | JIT | Native | WASM | JS | SV | UEFI | baremetal |
|---|---|---|---|---|---|---|---|
| 整数（tiny〜ulong） | ✅ | ✅ | ✅ | ⚠️ 53bit精度（BigInt迂回あり） | ✅ 幅写像 | ✅ | ✅ |
| bool / char | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| float / double | ✅ | ✅ | ✅ | ✅ | ❌ SV004 | ✅ | ✅ |
| string | ✅ | ✅ | ✅ | ✅ | ⚠️ const 3文字までは24bit定数、超過・非constは ❌ SV005 | ⚠️ 専用テキストAPI | ⚠️ 出力手段なし |
| ポインタ `T*` | ✅ | ✅ | ✅ | ⚠️ 基本のみ（`void*` はエラー） | ❌ SV002 | ✅ | ✅ |
| 固定長配列 `T[N]` | ✅ | ✅ | ✅ | ✅ | ✅ RAM/ROM推論 | ✅ | ✅ |
| 動的配列 / スライス | ✅ | ✅ | ✅ | ⚠️ 一部skip | ❌ SV006 | ⚠️ 自前アロケータ必須 | ⚠️ 自前アロケータ必須 |
| `bit[N]` / ビットスライス | ―（SV専用） | ― | ― | ― | ✅ | ― | ― |

## 標準ライブラリ

| モジュール | JIT | Native | WASM | JS | SV | UEFI | baremetal |
|---|---|---|---|---|---|---|---|
| std::io（println等） | ✅ | ✅ | ✅ | ✅ console写像 | ―（$displayはTB内のみ） | ❌ no_std検査 | ❌ no_std検査 |
| std::debug::assert | ✅ | ✅ | ✅ | ✅ throw | ✅ 即時アサーション | ✅ | ✅ |
| std::math / core / iter | ✅ | ✅ | ✅ | ✅ | ⚠️ 合成可能な範囲 | ✅ | ✅ |
| std::collections | ✅ | ✅ | ✅ | ❌ skip中 | ❌ 動的メモリ | ⚠️ | ⚠️ |
| std::mem（malloc等） | ✅ | ✅ | ✅ | ⚠️ GCエミュレーション | ― | ❌ no_std検査 | ❌ no_std検査 |
| std::thread / sync | ✅ | ✅ | ― | ❌ シングルスレッド | ― | ❌ | ❌ |
| std::net / http | ✅ | ✅ | ― | ❌（今後） | ― | ❌ | ❌ |
| std::gpu（Metal） | ✅ | ✅ macOSのみ | ― | ❌ | ― | ― | ― |
| ファイルI/O | ✅ | ✅ | ⚠️ WASI | ❌（今後） | ―（$readmemhのみ） | ❌ Boot Services経由 | ❌ |
| js::fetch / timer / console | ― | ― | ― | ✅ | ― | ― | ― |
| uefi::*（Boot Services） | ― | ― | ― | ― | ― | ✅ | ― |

## プラットフォーム固有機能

| 機能 | 対応バックエンド | 備考 |
|---|---|---|
| SV専用キーワード（posedge/negedge/wire/reg/always系/assign/initial/bit） | SVのみ | `//! platform: sv` で字句レベルから有効化。他プラットフォームでは通常の識別子 |
| `#[input]/#[output]/#[inout]` ポート、`#[sv::*]` 属性群 | SVのみ | pin/param/parameter/sync/tri/bram/lutram/memfile 等 |
| don't-careビットマッチ `0b1?00` | SVで検証済み | if-elseチェーンに脱糖（意味論は全バックエンド共通の設計だが、テストはSVのみ） |
| `//! sv: hierarchy` モジュール階層化 | SVのみ | |
| `module NAME;` トップモジュール名宣言 | SVのみ | 他バックエンドではnamespace相当 |
| Boot Services（uefi::table等） | UEFIのみ | |
| `__NO_STD__`/`__BAREMETAL__`/`__UEFI__` 定義 | UEFI/baremetal | no_std検査（println/malloc/ファイルIO/スレッド等の禁止）と対 |

## SVバックエンドの非対応宣言（対象外）

以下はCmから表現しない設計上の非目標である（詳細は [tutorials/ja/compiler/sv/semantics.md](../tutorials/ja/compiler/sv/semantics.md)）。

`force/release`、`specify`ブロック、UDP、信号強度、`fork/join`、イベント、DPI-C、SV interface/modport（Cmのinterfaceとは別概念。階層+構造体で代替）、遅延 `#10`（TB生成内部でのみ使用）。

型検査を通過した非対応構文がコード生成に到達した場合は SV007 で明示エラーになる（設計09 G1）。

## テスト方針（どのスイートがどこで走るか）

| バックエンド | 実行スイート | 備考 |
|---|---|---|
| interpreter | common | |
| JIT | common + llvm + jit | |
| LLVM Native | common + llvm | O0/O3 |
| WASM | common + wasm | wasmtime実行 |
| JS | common + js | 既知のskip 23カテゴリ（ポインタ/メモリ/std系） |
| SV | sv のみ | **commonは実行しない（2026-07-11 方針決定）**。合成可能サブセットの検証は tests/sv と実機回路（CmCPU）で行う |
| UEFI | uefi のみ | **commonは実行しない（同上）**。コンパイル検証中心 |
| baremetal | baremetal のみ | **commonは実行しない（同上）**。no_std検査のエラーテスト含む |

## 既知の問題

| 問題 | 影響 | 状態 |
|---|---|---|
| LLVM O3 + Linux x86_64 で到達不能コードの `ud2` によるSIGILL | common/functions/recursive_function、common/interface/operator_explicit をskip | mir_to_llvm.cpp に到達可能性解析の回避策実装済み。macOS/ARM64は影響なし |
| WASMでネストVec（Vec<Vec<T>>）の2行目push後に既存行のlen()が破壊される | common/collections/nested_vector_lifecycle_test をskip（理由記録済み） | 実バグとして設計09 G2で記録。修正は別課題 |
| 改行を含む選択的import構文 `import mod::{A,\n B}` がパーサ未対応 | common/advanced_modules/import_features をskip | 全バックエンド共通の構文ギャップ |
| WASMのO3で配列ポインタキャスト後の読み出しが0になる | common/memory/array_ptr_cast をO3のみskip（O0・ネイティブは正常） | 実バグとして設計09 G2で記録。修正は別課題 |
| std::fs が未実装 | common/fs・common/file_io の2テストをskip | 実装時に有効化 |
| JSの53bit精度・狭整数ラップ・ポインタ制限 | common 23カテゴリskip | 本マトリクスの対象外（JSギャップは別途） |
| 可変長スライスへの高階関数（map/filter/reduce/some/every/find等）がサイレントに空結果を返す | 固定長配列は正常。スライスはHIRが `array_size.value_or(0)` でサイズ0を埋め込むため0件として実行される | 2026-07-12の検証で発見。修正案（サイズ-1番兵 + ランタイムのCmSlice*展開）設計済み・未実装 |
| ユニオン型の実行時型判別手段（match型パターン・`is`）が未実装 | `as` はタグ検査つき（2026-07-12〜、失敗時パニック）だが、事前に安全に判別する構文がない | 設計11の可変長引数（ユニオンスライス糖衣）の前提機能として要設計 |
| JSバックエンドのユニオンはタグを持たないboxed値表現 | 誤った型での `as` 取り出しのタグ検査が機能しない（common/errors/union_cast_mismatch はjsをskip） | タグ付き表現への移行は別課題 |

## 更新規則

- バックエンドの可否に影響する変更（新機能・明示エラー化・skip追加）を行うときは、本マトリクスを同じコミットで更新する
- skipファイルには必ず理由をコメントで記録する（理由なしの空skipは追加しない）
