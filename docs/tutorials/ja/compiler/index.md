---
title: コンパイラ編
parent: Tutorials
has_children: true
nav_order: 5
---

[English](../../en/compiler/)

# コンパイラ編

Cm言語コンパイラの使い方とバックエンドを学ぶチュートリアル集です。推定学習時間: 3時間

---

## 📖 チュートリアル一覧

| # | タイトル | 難易度 | 内容 |
|---|---------|--------|------|
| 1 | [コンパイラの使い方](usage.html) | 🟢 初級 | コマンド・オプション |
| 2 | [LLVMバックエンド](llvm.html) | 🟡 中級 | ネイティブコンパイル |
| 3 | [WASMバックエンド](wasm.html) | 🟡 中級 | WebAssembly出力 |
| 4 | [JSバックエンド](js-compilation.html) | 🟡 中級 | JavaScript出力 |
| 5 | [プリプロセッサ](preprocessor.html) | 🟡 中級 | 条件付きコンパイル |
| 6 | [Linter](linter.html) | 🟢 初級 | 静的解析（cm lint） |
| 7 | [Formatter](formatter.html) | 🟢 初級 | コードフォーマット（cm fmt） |
| 8 | [最適化](optimization.html) | 🔴 上級 | O0-O3、末尾呼び出し最適化 |
| 9 | [SystemVerilogバックエンド](sv.html) | 🟡 中級 | FPGA向けSV生成（概要） |
| 10 | [SV: 型とポート](sv-types.html) | 🟡 中級 | 型マッピング・ポート・リテラル |
| 11 | [SV: プロセスと代入](sv-processes.html) | 🟡 中級 | always_ff/comb・暗黙的変換 |
| 12 | [SV: 制御構文とループ](sv-control-flow.html) | 🟡 中級 | if/case・whileループ再構成 |
| 13 | [SV: データ構造](sv-data.html) | 🟡 中級 | 連接・enum FSM・配列・文字列 |
| 14 | [SV: 状態初期化とシミュレーション](sv-state-sim.html) | 🟡 中級 | 初期値・initial・テストベンチ |
| 15 | [SV: 意味論保証](sv-semantics.html) | 🟡 中級 | Cm↔SV意味論対応の保証事項 |

---

← [標準ライブラリ編](../stdlib/) | **次のセクション:** [内部構造編](../internals/) →
