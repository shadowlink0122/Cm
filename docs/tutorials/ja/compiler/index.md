---
title: コンパイラ編
parent: Tutorials
has_children: true
nav_order: 5
---

[English](../../en/compiler/)

# コンパイラ編

Cm言語コンパイラの使い方とバックエンドを学ぶチュートリアル集です。推定学習時間: 3時間

バックエンド別にディレクトリが分かれています: `common/`（共通ツール）、`native/`（LLVMネイティブ・UEFI）、`wasm/`、`js/`、`sv/`（SystemVerilog/FPGA）

---

## 共通（common/）

| タイトル | 難易度 | 内容 |
|---------|--------|------|
| [コンパイラの使い方](common/usage.html) | 🟢 初級 | コマンド・オプション |
| [プリプロセッサ](common/preprocessor.html) | 🟡 中級 | 条件付きコンパイル |
| [Linter](common/linter.html) | 🟢 初級 | 静的解析（cm lint） |
| [Formatter](common/formatter.html) | 🟢 初級 | コードフォーマット（cm fmt） |
| [設定ファイル](common/config.html) | 🟢 初級 | .cmconfig.yml（言語・コンパイル既定値・lint設定） |
| [最適化](common/optimization.html) | 🔴 上級 | O0-O3、--funroll-loops、末尾呼び出し最適化 |
| [サニタイザ](common/sanitizer.html) | 🟡 中級 | --sanitize（address/bounds）による実行時メモリ検査 |

## ネイティブ（native/）

| タイトル | 難易度 | 内容 |
|---------|--------|------|
| [LLVMバックエンド](native/index.html) | 🟡 中級 | ネイティブコンパイル |
| [UEFIベアメタル開発](native/uefi.html) | 🔴 上級 | UEFIアプリケーション開発（no_std） |

## WebAssembly（wasm/）

| タイトル | 難易度 | 内容 |
|---------|--------|------|
| [WASMバックエンド](wasm/index.html) | 🟡 中級 | WebAssembly出力 |

## JavaScript（js/）

| タイトル | 難易度 | 内容 |
|---------|--------|------|
| [JSバックエンド](js/index.html) | 🟡 中級 | JavaScript出力 |
| [npmパッケージ連携](js/npm-interop.html) | 🟡 中級 | use "package"・構造体互換・コールバック・メソッドthis束縛 |

## SystemVerilog / FPGA（sv/）

| タイトル | 難易度 | 内容 |
|---------|--------|------|
| [SVバックエンド概要](sv/index.html) | 🟡 中級 | FPGA向けSV生成（概要・コンパイルオプション） |
| [型とポート](sv/types.html) | 🟡 中級 | 型マッピング・ポート・リテラル |
| [プロセスと代入](sv/processes.html) | 🟡 中級 | always_ff/comb・暗黙的変換 |
| [制御構文とループ](sv/control-flow.html) | 🟡 中級 | if/case・ループ再構成・定数ループ展開 |
| [データ構造](sv/data.html) | 🟡 中級 | 連接・enum FSM・配列・文字列 |
| [メモリ初期化](sv/memory.html) | 🟡 中級 | 配列初期値・$readmemh・--emit-memfile |
| [モジュール階層](sv/hierarchy.html) | 🟡 中級 | exportされたIO構造体によるサブモジュール化 |
| [実機I/O](sv/board-io.html) | 🟡 中級 | #[sv::pin]・--emit-constraints・トライステート・CDC同期 |
| [状態初期化とシミュレーション](sv/state-sim.html) | 🟡 中級 | 初期値・initial・テストベンチ・アサーション |
| [意味論保証](sv/semantics.html) | 🟡 中級 | Cm↔SV意味論対応の保証事項 |

---

<!-- nav -->
← 前: [標準ライブラリの拡張方法](../stdlib/extending.html) ｜ [目次](../index.html) ｜ 次: [コンパイラ編 - コンパイラの使い方](common/usage.html) →
