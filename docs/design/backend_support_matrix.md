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
| ポインタ `T*` | ✅ | ✅ | ✅ | ⚠️ 基本・フィールド・二重ポインタ対応（`void*`・ptr⇔intキャストはエラー） | ❌ SV002 | ✅ | ✅ |
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
| native::thread / sync | ✅ | ✅ | ― | ❌ シングルスレッド | ― | ❌ | ❌ |
| native::net / http | ✅ | ✅ | ― | ❌（今後） | ― | ❌ | ❌ |
| native::gpu（Metal） | ✅ | ✅ macOSのみ | ― | ❌ | ― | ― | ― |
| ファイルI/O | ✅ | ✅ | ⚠️ WASI | ❌（今後） | ―（$readmemhのみ） | ❌ Boot Services経由 | ❌ |
| js::fetch / timer / console | ― | ― | ― | ✅ | ― | ― | ― |
| uefi::*（Boot Services） | ― | ― | ― | ― | ― | ✅ | ― |

## プラットフォーム固有機能

| 機能 | 対応バックエンド | 備考 |
|---|---|---|
| SV専用キーワード（posedge/negedge/wire/reg/always系/assign/initial/bit） | SVのみ | `//! platform: sv` で字句レベルから有効化。他プラットフォームでは通常の識別子 |
| `#[input]/#[output]/#[inout]` ポート、`#[sv::*]` 属性群 | SVのみ | pin/param/parameter/sync/tri/bram/lutram/memfile 等 |
| don't-careビットマッチ `0b1?00` | SVで検証済み | if-elseチェーンに脱糖（意味論は全バックエンド共通の設計だが、テストはSVのみ） |
| exportされたIO構造体によるモジュール階層化 | SVのみ | |
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
| WASM | common + wasm | wasmtime実行（無い環境はnodeのWASIラッパーへフォールバック。両者とも欠落時はスイートが明示エラーで失敗） |
| JS | common + js | 理由付き個別skipのみ（void*/malloc系・53bit・ptr⇔intキャスト。2026-07-15にカテゴリ一括skipを棚卸し） |
| SV | sv のみ | **commonは実行しない（2026-07-11 方針決定）**。合成可能サブセットの検証は tests/sv と実機回路（CmCPU）で行う |
| UEFI | uefi のみ | **commonは実行しない（同上）**。コンパイル検証中心 |
| baremetal | baremetal のみ | **commonは実行しない（同上）**。no_std検査のエラーテスト含む |

## 既知の問題

| 問題 | 影響 | 状態 |
|---|---|---|
| LLVM O3 + Linux x86_64 で到達不能コードの `ud2` によるSIGILL | common/functions/recursive_function、common/interface/operator_explicit をskip | mir_to_llvm.cpp に到達可能性解析の回避策実装済み。macOS/ARM64は影響なし |
| exportリスト型モジュールの選択的import抽出（`import ./mod::{A, f}` のトップレベルエイリアス生成・export選択コピー時の非公開依存関数の同伴）が未対応 | common/advanced_modules/import_features をskip | namespace内の構造体型・非修飾呼び出しの解決は2026-07-15修正済み（回帰: namespace_struct_resolution）。残りはプリプロセッサのモジュール機能として次バージョン以降 |
| グローバル配列の初期化子（`const uint[4] T = [5, 6, 7, 8];` 等のトップレベル宣言）がソフトウェア系バックエンド（JITで確認）で反映されず、要素の読み出しが0になる | SVターゲットではinitialブロックとして機能する（CmCPUのprog_rom等）。ソフトウェア系で必要な場合は関数内ローカル配列を使う | 全バックエンド共通のグローバル初期化経路の実装が必要。既存テストにトップレベル配列初期化子のケースはなく未検出だった |
| 推移的importでexport再宣言が重複して出力される（同一シンボルの宣言が複数回現れる） | コンパイラ側の重複許容・名前デデュープで実害は出ていない | export再宣言の複数行初期化子切り詰め（コメント・文字列リテラル内の `;` `{` `}` 誤検出）は修正済み（回帰: tests/sv/import/multiline_export_array・multiline_export_string） |
| SVターゲットで要素長が不揃いの文字列配列の文字インデックス（`ARR[i][j]`）が誤った文字を返す（格納幅と実要素長の不一致。要素長が全て同じ場合は正常） | 文字インデックスを使う文字列配列は要素長を揃える（実例: CmCPUのCTRL_ABBREVSは全要素3文字で正常動作） | 文字列配列の格納幅と要素長情報の管理を見直す必要がある。tests/sv/import/multiline_export_string はこの問題を避けてコンパイル検証のみとしている |
| std::fs はJS/WASM未対応（ネイティブランタイムのcm_file_*依存） | common/fs・common/file_io はjs/llvm-wasmのみskip（native/JITは有効） | WASM対応はWASIのfd系API実装が必要。JSはNode fs委譲を別途検討 |
| `import std::io;` + `io::println()` の名前空間形式stdインポートが未対応 | 選択的import（`import std::io::println`）を使用する | モジュールシステムの残ギャップ（import_featuresの名前空間課題と同系統） |
| JSの `void*` 非対応（明示エラー）・53bit精度・ポインタ⇔整数キャスト不可 | libc malloc/free系（collections/std::mem/allocator等）と64bit大値・ptr⇔intキャストのテストを理由付きで個別skip（2026-07-15にカテゴリ一括skipを棚卸しし、動作する26テスト［基本/フィールド/二重ポインタ・impl経由書き戻し等］を有効化） | ポインタはオブジェクト参照で基本対応。void*はJSで表現不能のため明示エラーを維持 |
| `arr[i]` の範囲外アクセスが未検査（固定長=未定義値、スライス=0を黙って返す） | 安全にハンドリングする場合は `arr.get(i)` → `Option<T>` を使用する（v0.16.0追加） | Rust同様の範囲外パニック化は意味論変更のため別途検討 |
| matchアームの式本体内にネストした式形式match（`Ok(v) => match (f(v)) {...}` 等）は関数呼び出しscrutineeを単一評価へ退避できない | アーム本体をブロック形式にして変数へ受けてからmatchする | scrutinee退避プリパスは条件評価される位置（アーム式本体・ガード・短絡右辺・三項分岐・while条件）からは持ち出せないため |
| UEFIターゲットはLLVM最適化パスをスキップする（MIR最適化のみ適用） | O1〜O3指定でもLLVMレベルの最適化は行われない | v0.14.1 Bug#13の回避策（O2のインライン展開+DCEがefi_mainの制御フローを破壊）。再有効化は制御フロー保護の設計が必要 |
| SVターゲットはDCE/CopyProp等の文除去系MIR最適化を実行しない（O1以上で定数畳み込み・恒等式簡約のみ適用） | 未使用の一時変数代入等は生成SVに残る（合成ツール側で除去される） | 文除去系パスはHWロジック（出力代入・always内の中間信号）を消すため。文数・CFG形状を保存する書き換えのみ許可 |
| MIRのインライン化パスが休眠状態（呼び出し先の旧形式Constant表現のみ認識し、現行のFunctionRefを認識しない） | MIRレベルのインライン展開は行われない（native/JIT/WASMはLLVM側のインライナが担うため最終コードへの影響は限定的。LLVMを通らないSV/JS/インタプリタはインライン展開なし） | 有効化実験でperform_inliningの潜在バグ（デストラクタ順序破壊・SIGSEGV）が露出したため休眠を維持。再設計とセットで有効化する（tests/regression/cases/mir_optimization/README.md参照） |

## 更新規則

- バックエンドの可否に影響する変更（新機能・明示エラー化・skip追加）を行うときは、本マトリクスを同じコミットで更新する
- skipファイルには必ず理由をコメントで記録する（理由なしの空skipは追加しない）
