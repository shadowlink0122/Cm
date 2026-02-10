---
layout: default
title: Tutorials
nav_order: 2
has_children: true
---

[English](../en/)

# Cm言語チュートリアル v0.14.0

**対象バージョン:** v0.14.0  
**最終更新:** 2026-02-10

Cm言語の全機能を段階的に学べる包括的なチュートリアル集です。

---

## 📚 学習パス

### パス1: 基本を学ぶ（初級者向け）

推定時間: 3-4時間

1. **[基本編](basics/introduction.html)** - 言語の基礎（10チュートリアル）
   - [はじめに](basics/introduction.html) - Cm言語の特徴と設計思想
   - [環境構築](basics/setup.html) - コンパイラのビルドとセットアップ
   - [Hello, World!](basics/hello-world.html) - 最初のプログラム
   - [変数と型](basics/variables.html) - プリミティブ型、const/static
   - [演算子](basics/operators.html) - 算術・比較・論理演算
   - [制御構文](basics/control-flow.html) - if/while/for/switch/defer
   - [関数](basics/functions.html) - 定義・オーバーロード・デフォルト引数
   - [配列](basics/arrays.html) - 宣言・メソッド・for-in
   - [ポインタ](basics/pointers.html) - アドレス・デリファレンス・Array Decay
   - [モジュール](basics/modules.html) - import/export

### パス2: 型システムを学ぶ（中級者向け）

推定時間: 4-5時間

2. **[型システム編](types/structs.html)** - 高度な型機能
   - [構造体](types/structs.html) - 定義・コンストラクタ・ネスト
   - [Enum型](types/enums.html) - 列挙型・Tagged Union・match分解
   - [typedef](types/typedef.html) - 型エイリアス・リテラル型
   - [ジェネリクス](types/generics.html) - 型パラメータ・推論・モノモーフィゼーション
   - [インターフェース](types/interfaces.html) - interface/impl/self
   - [型制約](types/constraints.html) - AND/OR境界・where句
   - [所有権と借用](types/ownership.html) - 移動セマンティクス・借用
   - [ライフタイム](types/lifetimes.html) - 参照の有効期間

### パス3: 高度な機能を学ぶ（上級者向け）

推定時間: 5-6時間

3. **[高度な機能編](advanced/match.html)** - 言語の強力な機能
   - [match式](advanced/match.html) - パターンマッチング・ガード・網羅性チェック
   - [with自動実装](advanced/with-keyword.html) - Eq/Ord/Clone/Hash
   - [演算子オーバーロード](advanced/operators.html) - カスタム演算子
   - [関数ポインタ](advanced/function-pointers.html) - 高階関数
   - [ラムダ式](advanced/lambda.html) - クロージャ
   - [文字列操作](advanced/strings.html) - メソッド・スライス
   - [スライス](advanced/slices.html) - 動的配列
   - [FFI](advanced/ffi.html) - C言語連携・use libc
   - [extern宣言](advanced/extern.html) - C/C++関数の呼び出し
   - [インラインアセンブリ](advanced/inline-asm.html) - __asm__
   - [const](advanced/const.html) - コンパイル時定数
   - [mustキーワード](advanced/must.html) - 戻り値使用の強制
   - [マクロ](advanced/macros.html) - 条件付きコンパイル

### パス4: 標準ライブラリを学ぶ

推定時間: 3-4時間

4. **[標準ライブラリ編](stdlib/)** - Native向けstdモジュール
   - [入出力 (io)](stdlib/io.html) - println/input/ファイルI/O
   - [メモリ管理 (mem)](stdlib/mem.html) - alloc/size_of/Allocator
   - [数学関数 (math)](stdlib/math.html) - sin/sqrt/PI/gcd
   - [コア (core)](stdlib/core-utils.html) - min/max/clamp/型エイリアス
   - [Vector](stdlib/collections/vector.html) - 動的配列・Vector\<Vector\<int\>\>
   - [Queue](stdlib/collections/queue.html) - FIFOキュー
   - [HashMap](stdlib/collections/hashmap.html) - 連想配列
   - [HTTP通信](stdlib/http.html) - HttpClient/HttpServer/HTTPS
   - [TCP/UDP通信](stdlib/network/tcp.html) - ソケット/DNS/poll
   - [並行処理](stdlib/concurrency/) - スレッド/Mutex/Channel/Atomic
   - [GPU計算](stdlib/gpu.html) - Apple Metal GPGPU
   - [拡張ガイド](stdlib/extending.html) - C/C++/ObjC++による拡張

### パス5: コンパイラを学ぶ

推定時間: 3時間

4. **[コンパイラ編](compiler/usage.html)** - ビルドとバックエンド
   - [コンパイラの使い方](compiler/usage.html) - コマンド・オプション
   - [LLVMバックエンド](compiler/llvm.html) - ネイティブコンパイル
   - [WASMバックエンド](compiler/wasm.html) - WebAssembly出力
   - [JSバックエンド](compiler/js-compilation.html) - JavaScript出力
   - [プリプロセッサ](compiler/preprocessor.html) - 条件付きコンパイル
   - [Linter](compiler/linter.html) - 静的解析（cm lint）
   - [Formatter](compiler/formatter.html) - コードフォーマット（cm fmt）
   - [最適化](compiler/optimization.html) - O0-O3、末尾呼び出し最適化

### パス6: 内部構造を学ぶ（開発者向け）

推定時間: 3時間

6. **[内部構造編](internals/architecture.html)** - コンパイラの仕組み
   - [アーキテクチャ](internals/architecture.html) - 全体構成とパイプライン
   - [アルゴリズム](internals/algorithms.html) - 解析と最適化のアルゴリズム
   - [最適化](internals/optimization.html) - MIR/LLVMレベルの最適化

---

## 🎯 難易度別ガイド

### 🟢 初級 - プログラミング経験者

- Hello, World!
- 変数と型
- 演算子
- 制御構文
- 関数

### 🟡 中級 - Cm言語の基本を理解している

- 配列・ポインタ
- 構造体・Enum型
- インターフェース
- スレッド

### 🔴 上級 - 型システムとメモリ管理の深い理解が必要

- ジェネリクス
- 型制約
- match式・パターンガード
- FFI・インラインASM

---

## ✅ 実装状況一覧（v0.14.0）

| カテゴリ | 機能 | LLVM | WASM | JS | チュートリアル |
|---------|------|------|------|-----|---------------|
| **基本** | プリミティブ型 | ✅ | ✅ | ✅ | ✅ [variables](basics/variables.html) |
| | 制御構文 | ✅ | ✅ | ✅ | ✅ [control-flow](basics/control-flow.html) |
| | 関数 | ✅ | ✅ | ✅ | ✅ [functions](basics/functions.html) |
| | モジュール | ✅ | ✅ | ✅ | ✅ [modules](basics/modules.html) |
| **データ** | 構造体 | ✅ | ✅ | ✅ | ✅ [structs](types/structs.html) |
| | Enum/Tagged Union | ✅ | ✅ | ✅ | ✅ [enums](types/enums.html) |
| | 配列 | ✅ | ✅ | ✅ | ✅ [arrays](basics/arrays.html) |
| | ポインタ | ✅ | ✅ | ❌ | ✅ [pointers](basics/pointers.html) |
| **型** | ジェネリクス | ✅ | ✅ | ✅ | ✅ [generics](types/generics.html) |
| | インターフェース | ✅ | ✅ | ✅ | ✅ [interfaces](types/interfaces.html) |
| | 型制約 | ✅ | ✅ | ✅ | ✅ [constraints](types/constraints.html) |
| **高度** | match式・ガード | ✅ | ✅ | ✅ | ✅ [match](advanced/match.html) |
| | with自動実装 | ✅ | ✅ | ✅ | ✅ [with](advanced/with-keyword.html) |
| | クロージャ・ラムダ | ✅ | ✅ | ✅ | ✅ [lambda](advanced/lambda.html) |
| | インラインASM | ✅ | ❌ | ❌ | ✅ [inline-asm](advanced/inline-asm.html) |
| | extern宣言 | ✅ | ✅ | ❌ | ✅ [extern](advanced/extern.html) |
| | FFI | ✅ | ❌ | ❌ | ✅ [ffi](advanced/ffi.html) |
| **std** | HTTP/HTTPS | ✅ | ❌ | ❌ | ✅ [http](stdlib/http.html) |
| | TCP/UDP/DNS | ✅ | ❌ | ❌ | ✅ [tcp](stdlib/network/tcp.html) |
| | スレッド | ✅ | ❌ | ❌ | ✅ [thread](stdlib/concurrency/thread.html) |
| | Mutex/RwLock | ✅ | ❌ | ❌ | ✅ [mutex](stdlib/concurrency/mutex.html) |
| | Channel | ✅ | ❌ | ❌ | ✅ [channel](stdlib/concurrency/channel.html) |
| | Atomic | ✅ | ❌ | ❌ | ✅ [atomic](stdlib/concurrency/atomic.html) |
| | GPU (Metal) | ✅ | ❌ | ❌ | ✅ [gpu](stdlib/gpu.html) |
| **ツール** | Linter | ✅ | - | - | ✅ [linter](compiler/linter.html) |
| | Formatter | ✅ | - | - | ✅ [formatter](compiler/formatter.html) |
| | プリプロセッサ | ✅ | ✅ | ❌ | ✅ [preprocessor](compiler/preprocessor.html) |
| **バックエンド** | JSコンパイル | - | - | ✅ | ✅ [js-compilation](compiler/js-compilation.html) |

凡例: ✅ 完全対応 | ⚠️ 部分対応 | ❌ 未対応

---

## 💡 学習のヒント

### 効率的に学ぶために

1. **実際に書く** - サンプルを写経する
2. **エラーを読む** - エラーメッセージから学ぶ
3. **小さく始める** - 簡単なコードから
4. **テストする** - 期待通り動くか確認
5. **参照する** - `tests/test_programs/`を見る

### つまずいたら

1. **エラーメッセージを確認** - 何が問題か
2. **デバッグモード** - `--debug`で詳細表示
3. **テストコード** - `tests/test_programs/`の動作例を参考に
4. **質問する** - GitHubイシューで

---

## 🔗 関連リンク

- [正式言語仕様](../../design/CANONICAL_SPEC.html) - 言語の完全な仕様
- [設計ドキュメント](../../design/) - アーキテクチャ・設計文書
- [テストケース](https://github.com/shadowlink0122/Cm/tree/main/tests/test_programs/) - 368ファイル

---

## 📊 進捗トラッカー

学習の進捗を記録しましょう：

- [ ] 基本編（10チュートリアル）
  - [ ] はじめに
  - [ ] 環境構築
  - [ ] Hello, World!
  - [ ] 変数と型
  - [ ] 演算子
  - [ ] 制御構文
  - [ ] 関数
  - [ ] 配列
  - [ ] ポインタ
  - [ ] モジュール

- [ ] 型システム編（8チュートリアル）
  - [ ] 構造体
  - [ ] Enum型
  - [ ] typedef
  - [ ] ジェネリクス
  - [ ] インターフェース
  - [ ] 型制約
  - [ ] 所有権と借用
  - [ ] ライフタイム

- [ ] 高度な機能編（12チュートリアル）
  - [ ] match式
  - [ ] with自動実装
  - [ ] 演算子オーバーロード
  - [ ] 関数ポインタ
  - [ ] ラムダ式
  - [ ] 文字列操作
  - [ ] スライス
  - [ ] FFI
  - [ ] スレッド
  - [ ] const
  - [ ] mustキーワード
  - [ ] マクロ

- [ ] コンパイラ編（8チュートリアル）
  - [ ] コンパイラの使い方
  - [ ] LLVMバックエンド
  - [ ] WASMバックエンド
  - [ ] JSバックエンド
  - [ ] プリプロセッサ
  - [ ] Linter
  - [ ] Formatter
  - [ ] 最適化

- [ ] 内部構造編（3チュートリアル）
  - [ ] アーキテクチャ
  - [ ] アルゴリズム
  - [ ] 最適化

---

**チュートリアル総数:** 42ファイル  
**推定学習時間:** 18-22時間  
**対象バージョン:** v0.14.0

---

**最終更新:** 2026-02-10  
**著者:** Cm Language Development Team

---
[English](../en/)
