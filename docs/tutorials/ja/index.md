---
layout: default
title: Tutorials
nav_order: 2
has_children: true
---

[English](../en/)

# Cm言語チュートリアル v0.11.0

**対象バージョン:** v0.11.0  
**最終更新:** 2026-01-06

Cm言語の全機能を段階的に学べる包括的なチュートリアル集です。

---

## 📚 学習パス

### パス1: 基本を学ぶ（初級者向け）

推定時間: 3-4時間

1. **[基本編](basics/introduction.html)** - 言語の基礎（8チュートリアル）
   - [はじめに](basics/introduction.html) - Cm言語の特徴と設計思想
   - [環境構築](basics/setup.html) - コンパイラのビルドとセットアップ
   - [Hello, World!](basics/hello-world.html) - 最初のプログラム
   - [変数と型](basics/variables.html) - プリミティブ型、const/static
   - [演算子](basics/operators.html) - 算術・比較・論理演算
   - [制御構文](basics/control-flow.html) - if/while/for/switch/defer
   - [関数](basics/functions.html) - 定義・オーバーロード・デフォルト引数
   - [配列](basics/arrays.html) - 宣言・メソッド・for-in
   - [ポインタ](basics/pointers.html) - アドレス・デリファレンス・Array Decay

### パス2: 型システムを学ぶ（中級者向け）

推定時間: 4-5時間

2. **[型システム編](types/structs.html)** - 高度な型機能
   - [構造体](types/structs.html) - 定義・コンストラクタ・ネスト
   - [Enum型](types/enums.html) - 列挙型・switch/matchでの使用
   - [typedef](types/typedef.html) - 型エイリアス・リテラル型
   - [ジェネリクス](types/generics.html) - 型パラメータ・推論・モノモーフィゼーション
   - [インターフェース](types/interfaces.html) - interface/impl/self
   - [型制約](types/constraints.html) - AND/OR境界・where句
   - [所有権と借用](types/ownership.html) - 移動セマンティクス・借用
   - [ライフタイム](types/lifetimes.html) - 参照の有効期間

### パス3: 高度な機能を学ぶ（上級者向け）

推定時間: 5-6時間

3. **[高度な機能編](advanced/match.html)** - 言語の強力な機能
   - [match式](advanced/match.html) - パターンマッチング・網羅性チェック
   - [with自動実装](advanced/with-keyword.html) - Eq/Ord/Clone/Hash
   - [演算子オーバーロード](advanced/operators.html) - カスタム演算子
   - [関数ポインタ](advanced/function-pointers.html) - 高階関数
   - [文字列操作](advanced/strings.html) - メソッド・スライス

### パス4: コンパイルを学ぶ

推定時間: 2時間

4. **[コンパイラ編](compiler/usage.html)** - ビルドとバックエンド
   - [コンパイラの使い方](compiler/usage.html) - コマンド・オプション
   - [LLVMバックエンド](compiler/llvm.html) - ネイティブコンパイル
   - [WASMバックエンド](compiler/wasm.html) - WebAssembly出力
   - [プリプロセッサ](compiler/preprocessor.html) - 条件付きコンパイル

### パス5: 内部構造を学ぶ（開発者向け）

推定時間: 3時間

5. **[内部構造編](internals/architecture.html)** - コンパイラの仕組み
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

- 配列
- ポインタ
- 構造体
- Enum型
- インターフェース

### 🔴 上級 - 型システムとメモリ管理の深い理解が必要

- ジェネリクス
- 型制約
- match式
- with自動実装
- 演算子オーバーロード

---

## 📖 チュートリアルの構成

各チュートリアルは以下の形式で統一されています：

### 1. 概要
- 学習内容
- 所要時間
- 難易度

### 2. 本文
- コード例
- 詳しい説明
- 実行結果

### 3. よくある間違い
- つまずきやすいポイント
- エラー例と修正方法

### 4. 練習問題
- 理解度チェック
- 実踐的な課題

### 5. 次のステップ
- 関連チュートリアル
- 推奨学習パス

---

## 🚀 使い方

### 初めて学ぶ場合

1. **[Hello, World!](basics/hello-world.html)** から始める
2. パス1（基本編）を順番に進む
3. 各章の練習問題を解く
4. パス2、パス3へ進む

### 特定の機能を学びたい場合

1. 目次から必要なチュートリアルを選ぶ
2. 前提知識を確認
3. 関連チュートリアルも確認

### 復習する場合

1. 各チュートリアルのコード例を実行
2. 練習問題を再挑戦
3. サンプルコード(`https://github.com/shadowlink0122/Cm/tree/main/examples`)を参照

---

## ✅ 実装状況一覧（v0.11.0）

| カテゴリ | 機能 | インタプリタ | LLVM | WASM | チュートリアル |
|---------|------|------------|------|------|---------------|
| **基本** | プリミティブ型 | ✅ | ✅ | ✅ | ✅ [variables](basics/variables.html) |
| | 制御構文 | ✅ | ✅ | ✅ | ✅ [control-flow](basics/control-flow.html) |
| | 関数 | ✅ | ✅ | ✅ | ✅ [functions](basics/functions.html) |
| **データ** | 構造体 | ✅ | ✅ | ✅ | ✅ [structs](types/structs.html) |
| | Enum | ✅ | ✅ | ✅ | ✅ [enums](types/enums.html) |
| | 配列 | ✅ | ✅ | ✅ | ✅ [arrays](basics/arrays.html) |
| | ポインタ | ✅ | ✅ | ✅ | ✅ [pointers](basics/pointers.html) |
| **型** | typedef | ✅ | ✅ | ✅ | ✅ [typedef](types/typedef.html) |
| | ジェネリクス | ✅ | ✅ | ✅ | ✅ [generics](types/generics.html) |
| | インターフェース | ✅ | ✅ | ✅ | ✅ [interfaces](types/interfaces.html) |
| | 型制約 | ✅ | ✅ | ✅ | ✅ [constraints](types/constraints.html) |
| **高度** | match式 | ✅ | ✅ | ✅ | ✅ [match](advanced/match.html) |
| | with自動実装 | ✅ | ✅ | ✅ | ✅ [with](advanced/with-keyword.html) |
| | operator | ✅ | ✅ | ✅ | ✅ [operators](advanced/operators.html) |
| | 関数ポインタ | ✅ | ✅ | ✅ | ✅ [function-pointers](advanced/function-pointers.html) |

凡例: ✅ 完全対応 | ⚠️ 次バージョン予定

---

## 💡 学習のヒント

### 効率的に学ぶために

1. **実際に書く** - サンプルを写経する
2. **エラーを読む** - エラーメッセージから学ぶ
3. **小さく始める** - 簡単なコードから
4. **テストする** - 期待通り動くか確認
5. **参照する** - `examples/`や`tests/`を見る

### つまずいたら

1. **エラーメッセージを確認** - 何が問題か
2. **デバッグモード** - `--debug`で詳細表示
3. **サンプルコード** - 動作例を参考に
4. **完全ガイド** - 全機能を網羅
5. **質問する** - GitHubイシューで

---

## 🔗 関連リンク

- [正式言語仕様](../../design/CANONICAL_SPEC.html) - 言語の完全な仕様
- [機能リファレンス](../../features/) - 機能別の詳細仕様
- [サンプルコード](https://github.com/shadowlink0122/Cm/tree/main/examples) - 実用例
- [テストケース](https://github.com/shadowlink0122/Cm/tree/main/tests/test_programs/) - 138ファイル

---

## 📊 進捗トラッカー

学習の進捗を記録しましょう：

- [ ] 基本編（8チュートリアル）
  - [ ] はじめに
  - [ ] 環境構築
  - [ ] Hello, World!
  - [ ] 変数と型
  - [ ] 演算子
  - [ ] 制御構文
  - [ ] 関数
  - [ ] 配列
  - [ ] ポインタ

- [ ] 型システム編（6チュートリアル）
  - [ ] 構造体
  - [ ] Enum型
  - [ ] typedef
  - [ ] ジェネリクス
  - [ ] インターフェース
  - [ ] 型制約

- [ ] 高度な機能編（5チュートリアル）
  - [ ] match式
  - [ ] with自動実装
  - [ ] 演算子オーバーロード
  - [ ] 関数ポインタ
  - [ ] 文字列操作

- [ ] コンパイラ編（4チュートリアル）
  - [ ] コンパイラの使い方
  - [ ] LLVMバックエンド
  - [ ] WASMバックエンド
  - [ ] プリプロセッサ

- [ ] 内部構造編（3チュートリアル）
  - [ ] アーキテクチャ
  - [ ] アルゴリズム
  - [ ] 最適化

---

**チュートリアル総数:** 25ファイル  
**推定学習時間:** 17-20時間  
**対象バージョン:** v0.11.0

---

**最終更新:** 2026-01-06  
**著者:** Cm Language Development Team

---
[English](../en/)
