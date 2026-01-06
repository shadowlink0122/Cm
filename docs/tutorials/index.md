---
layout: default
title: Tutorials
nav_order: 2
has_children: true
---

# Cm言語チュートリアル v0.11.0

**対象バージョン:** v0.11.0 (開発中)  
**最終更新:** 2025-01-05

Cm言語の全機能を段階的に学べる包括的なチュートリアル集です。

---

## 📚 学習パス

### パス1: 基本を学ぶ（初級者向け）

推定時間: 3-4時間

1. **[基本編](basics/)** - 言語の基礎（8チュートリアル）
   - [はじめに](basics/introduction.md) - Cm言語の特徴と設計思想
   - [環境構築](basics/setup.md) - コンパイラのビルドとセットアップ
   - [Hello, World!](basics/hello-world.md) - 最初のプログラム
   - [変数と型](basics/variables.md) - プリミティブ型、const/static
   - [演算子](basics/operators.md) - 算術・比較・論理演算
   - [制御構文](basics/control-flow.md) - if/while/for/switch/defer
   - [関数](basics/functions.md) - 定義・オーバーロード・デフォルト引数
   - [配列](basics/arrays.md) - 宣言・メソッド・for-in
   - [ポインタ](basics/pointers.md) - アドレス・デリファレンス・Array Decay
   - [モジュール](basics/modules.md) - import/export・エイリアス

### パス2: 型システムを学ぶ（中級者向け）

推定時間: 4-5時間

2. **[型システム編](types/)** - 高度な型機能
   - [構造体](types/structs.md) - 定義・コンストラクタ・ネスト
   - [Enum型](types/enums.md) - 列挙型・switch/matchでの使用
   - [typedef](types/typedef.md) - 型エイリアス・リテラル型
   - [ジェネリクス](types/generics.md) - 型パラメータ・推論・モノモーフィゼーション
   - [インターフェース](types/interfaces.md) - interface/impl/self
   - [型制約](types/constraints.md) - AND/OR境界・where句

### パス3: 高度な機能を学ぶ（上級者向け）

推定時間: 5-6時間

3. **[高度な機能編](advanced/)** - 言語の強力な機能
   - [match式](advanced/match.md) - パターンマッチング・網羅性チェック
   - [with自動実装](advanced/with-keyword.md) - Eq/Ord/Clone/Hash
   - [演算子オーバーロード](advanced/operators.md) - カスタム演算子
   - [関数ポインタ](advanced/function-pointers.md) - 高階関数
   - [文字列操作](advanced/strings.md) - メソッド・スライス
   - [ラムダ式](advanced/lambda.md) - 無名関数
   - [スライス型](advanced/slices.md) - 動的スライス・高階関数

### パス4: コンパイルを学ぶ

推定時間: 2時間

4. **[コンパイラ編](compiler/)** - ビルドとバックエンド
   - [コンパイラの使い方](compiler/usage.md) - コマンド・オプション
   - [LLVMバックエンド](compiler/llvm.md) - ネイティブコンパイル
   - [WASMバックエンド](compiler/wasm.md) - WebAssembly出力
   - [最適化](compiler/optimization.md) - MIR最適化パス

---

## ✅ 実装状況一覧

| カテゴリ | 機能 | v0.10.0 | v0.11.0 | チュートリアル |
|---------|------|---------|---------|---------------|
| **基本** | プリミティブ型 | ✅ | ✅ | [variables](basics/variables.md) |
| | 制御構文 | ✅ | ✅ | [control-flow](basics/control-flow.md) |
| **型** | typedef | ✅ | ✅ | [typedef](types/typedef.md) |
| | typedefポインタ | ✅ | ✅ | [typedef](types/typedef.md) |
| | ジェネリクス | ✅ | ✅ | [generics](types/generics.md) |
| **モジュール** | エイリアス (as) | 🟡 | ✅ | [modules](basics/modules.md) |
| | 選択インポート | 🟡 | ✅ | [modules](basics/modules.md) |
| **高度** | with Eq/Ord | ✅ | ✅ | [with](advanced/with-keyword.md) |
| | ラムダ式 | ✅ | ✅ | [lambda](advanced/lambda.md) |
| **安全性** | 境界チェック | ❌ | ✅ | [arrays](basics/arrays.md) |
| | C ABI互換性 | ❌ | ✅ | [ffi](advanced/ffi.md) |
| | メモリ管理(Leak修正) | ❌ | ✅ | [slices](advanced/slices.md) |

凡例: ✅ 完全対応 | 🟡 部分実装 | ❌ 未実装

---

## 🎯 難易度別ガイド

### 🟢 初級 - プログラミング経験者
- Hello, World! / 変数と型 / 演算子 / 制御構文 / 関数

### 🟡 中級 - Cm言語の基本を理解している
- 配列 / ポインタ / 構造体 / Enum型 / インターフェース

### 🔴 上級 - 型システムとメモリ管理の深い理解が必要
- ジェネリクス / 型制約 / match式 / with自動実装 / 演算子オーバーロード

---

## 📊 進捗トラッカー

学習の進捗を記録しましょう：

- [ ] 基本編（8チュートリアル）
- [ ] 型システム編（6チュートリアル）
- [ ] 高度な機能編（5チュートリアル）
- [ ] コンパイラ編（3チュートリアル）

---

**チュートリアル総数:** 28ファイル  
**対象バージョン:** v0.11.0

---

**最終更新:** 2025-01-05  
**著者:** Cm Language Development Team
