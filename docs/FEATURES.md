---
layout: default
title: Features
---

# Cm言語 実装済み機能一覧

Cm言語コンパイラおよびランタイムで現在利用可能な機能の一覧です。

## 💎 基本機能

| 機能 | 状態 | 詳細 |
|------|------|------|
| **プリミティブ型** | ✅ | int, uint, float, double, bool, char, string |
| **演算子** | ✅ | 算術、比較、論理、ビット演算、三項演算子 |
| **制御構文** | ✅ | if/else, while, for, switch, defer |
| **関数** | ✅ | 定義、デフォルト引数、可変長引数（自由関数の同名オーバーロードは未対応・重複定義はコンパイルエラー） |
| **モジュール** | ✅ | import, export, 名前空間 |
| **const強化** | ✅ | 配列サイズにconst変数を使用可能、定数式のコンパイル時評価 |

## 🏗️ 型システム

| 機能 | 状態 | 詳細 |
|------|------|------|
| **構造体 (struct)** | ✅ | 定義、初期化、メンバアクセス、コンストラクタ(`self()`) |
| **デストラクタ** | ✅ | `~self()`によるリソース自動解放 |
| **列挙型 (enum)** | ✅ | 強型付け列挙型、整数値指定 |
| **ネスト型宣言** | ✅ | struct/enum本体内のstruct/enum宣言、`Outer::Inner` 型パス・`Outer::Inner::MEM` チェーンアクセス（v0.17.1） |
| **Tagged Union** | ✅ | 関連データ付きenum、match分解 |
| **組み込みResult/Option** | ✅ | `Result<T,E>` / `Option<T>`、`is_ok`/`unwrap`/`unwrap_or`/`expect` 等のメソッド、`?` 演算子によるエラー伝播、must_use警告（v0.16.0） |
| **ユニオン実行時型判別** | ✅ | `is` 演算子、match型パターン（`int i => ...`）、`as` のタグ検査（v0.16.0） |
| **ジェネリクス** | ✅ | 型パラメータ、関数・構造体ジェネリクス、ジェネリックコンストラクタ、複数型パラメータ |
| **インターフェース** | ✅ | interface定義、impl実装、動的ディスパッチ |
| **型制約 (where)** | ✅ | AND境界 (`T: A + B`), OR境界 (`T: A | B`) |
| **型エイリアス** | ✅ | `typedef NewType = OldType` |
| **インラインユニオン型** | ✅ | `int \| null` 構文でnull許容型を簡潔に記述 |
| **null型** | ✅ | 独立したnull型、ユニオン型のメンバーとして使用 |
| **ポインタ** | ✅ | 生ポインタ、`->` 演算子によるフィールドアクセス |

## 🚀 高度な機能

| 機能 | 状態 | 詳細 |
|------|------|------|
| **パターンマッチング** | ✅ | `match` 式、リテラル・Enum・型・ワイルドカード、パターンガード、don't careビットマッチ（`0b1?00`）、値を返す式形式（v0.16.0） |
| **自動実装（with / #[derive]）** | ✅ | `Eq`, `Ord`, `Copy`, `Clone`, `Hash`, `Debug`, `Display`, `Css` の自動導出。`#[derive(Eq)]` と `with Eq` は等価 |
| **演算子オーバーロード** | ✅ | `impl T { operator ... }` で直接定義、複合代入(`+=`等)自動対応、ビット演算子 |
| **関数ポインタ** | ✅ | 関数を値として扱う、ラムダ式 |
| **文字列補間** | ✅ | `println("x = {x}")` 形式の出力。式・ネスト呼び出し・メソッド呼び出しの埋め込みとスコープ検査（v0.16.0） |
| **外部関数 (FFI)** | ✅ | `extern "C"` ブロック、Cライブラリ連携 |
| **mustキーワード** | ✅ | 戻り値使用の強制 |
| **インラインアセンブリ** | ✅ | `__asm__` キーワード、x86_64/ARM64対応 |
| **条件付きコンパイル** | ✅ | `#ifdef`/`#ifndef`/`#else`/`#endif`、定義済みマクロ |

## 📦 標準ライブラリ

| モジュール | 状態 | 詳細 |
|-----------|------|------|
| **std::io** | ✅ | println/print（Zero-libc実装）。OS依存機能（ファイルストリーム等）は native::io |
| **std::fs** | ✅ | ファイル操作（基本API + Result API。Native/JIT。v0.16.0） |
| **std::math** | ✅ | checked_div / checked_mod（`Option<int>` を返す安全な除算。v0.16.0） |
| **native::thread** | ✅ | spawn, join, detach, sleep_ms |
| **native::sync** | ✅ | Mutex, Channel |
| **std::collections** | ✅ | Vector\<T\>, Queue\<T\>, HashMap\<K,V\> |
| **native::gpu** | ✅ | Metal GPU演算（macOS） |
| **native::net** | ✅ | TCP/UDP通信 |
| **native::http** | ✅ | HTTP/HTTPS通信 |

## ⚙️ バックエンド

| バックエンド | 状態 | 詳細 |
|------------|------|------|
| **JITコンパイラ** | ✅ | LLVM ORC JIT、高速な実行 |
| **LLVM (Native)** | ✅ | ネイティブ実行ファイル生成 (x86_64, ARM64) |
| **LLVM (WASM)** | ✅ | WebAssembly出力、ブラウザ/WASI実行 |
| **JavaScript** | ✅ | Node.js/ブラウザ向けJS生成（ポインタ・64bit精度に制限あり） |
| **TypeScript出力** | ✅ | `--target=ts` で型注釈付きTS出力（struct→export interface・関数/変数の型注釈。tscで型検査可。v0.17.0） |
| **SystemVerilog** | ✅ | FPGA向けSV生成（合成可能サブセット。v0.16.0で大幅拡充） |
| **UEFI** | ✅ | UEFIアプリケーション（Boot Servicesライブラリ、no_std） |
| **baremetal** | ✅ | ベアメタル（ARM/x86、no_std） |

バックエンドごとの構文・機能の対応可否は [バックエンド対応マトリクス](design/backend_support_matrix.html) を参照。

## 🛠️ ツール・最適化

| 機能 | 状態 | 詳細 |
|------|------|------|
| **cm lint** | ✅ | コード品質チェック、診断カタログ、`--strict` で宣言の命名規則チェック（L001） |
| **cm fmt** | ✅ | コードフォーマッタ（`--check` 対応） |
| **cm test / #[test]** | ✅ | 統一テスト属性とテストコマンド（SVシミュレーション/JIT実行の自動ディスパッチ。v0.16.0） |
| **サニタイザ（--sanitize）** | ✅ | address/thread/memory（LLVM計装+ランタイム）・bounds（trap方式）・undefined（MIRレベルのゼロ除算/null参照検査、native/wasm/jit/js）。`.cmconfig.yml` の `compile: sanitize:` でも設定可（v0.16.2） |
| **MIR最適化** | ✅ | SCCP、定数畳み込み・恒等式簡約、GVN、コピー伝播、DSE、DCE、CFG簡約、LICM、定数ループ展開（MIRインライン化は休眠中の既知の問題。native/JIT/WASMはLLVM側インライナが担当） |
| **末尾呼び出し最適化** | ✅ | 再帰関数のスタック最適化 |
| **無限ループ検出** | ✅ | コンパイル時の静的無限ループ解析 |
| **自動デバッグ出力** | ✅ | `#debug` ディレクティブによるトレース |
| **マルチアーキテクチャ** | ✅ | `make build ARCH=arm64/x86_64` |

## 📊 診断システム

| カテゴリ | コード範囲 | 説明 |
|---------|-----------|------|
| **構文エラー** | E001-E099 | パーサーエラー |
| **型エラー** | E100-E299 | 型チェックエラー |
| **ポインタエラー** | E300-E399 | null参照、const違反、`->`演算子提案 |
| **ジェネリクスエラー** | E400-E499 | 型引数不一致、制約違反 |
| **Enum/Matchエラー** | E500-E599 | 網羅性、重複アーム |
| **リテラルエラー** | E600-E699 | オーバーフロー、定数式必須 |
| **警告** | W100-W401 | 未使用変数、潜在的問題 |
| **Lintルール** | L200-L402 | スタイル、ベストプラクティス |
| **SVバックエンド** | SV001-SV007 | 循環モジュール依存、非合成型（ポインタ/浮動小数点/string/動的配列）、非対応構文（インラインアセンブリ等） |

