# Cm言語チュートリアル v0.9.0

**対象バージョン:** v0.9.0  
**最終更新:** 2024-12-16

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

### パス4: コンパイルを学ぶ

推定時間: 2時間

4. **[コンパイラ編](compiler/)** - ビルドとバックエンド
   - [コンパイラの使い方](compiler/usage.md) - コマンド・オプション
   - [LLVMバックエンド](compiler/llvm.md) - ネイティブコンパイル
   - [WASMバックエンド](compiler/wasm.md) - WebAssembly出力

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
- 実践的な課題

### 5. 次のステップ
- 関連チュートリアル
- 推奨学習パス

---

## 📂 ディレクトリ構造

```
tutorials/
├── README.md                    # このファイル
│
├── basics/                      # 基本編（8チュートリアル）
│   ├── introduction.md         # はじめに
│   ├── setup.md                # 環境構築
│   ├── hello-world.md          # Hello, World!
│   ├── variables.md            # 変数と型
│   ├── operators.md            # 演算子
│   ├── control-flow.md         # 制御構文
│   ├── functions.md            # 関数
│   ├── arrays.md               # 配列
│   └── pointers.md             # ポインタ
│
├── types/                       # 型システム編（6チュートリアル）
│   ├── structs.md              # 構造体
│   ├── enums.md                # Enum型
│   ├── typedef.md              # 型エイリアス
│   ├── generics.md             # ジェネリクス
│   ├── interfaces.md           # インターフェース
│   └── constraints.md          # 型制約
│
├── advanced/                    # 高度な機能編（5チュートリアル）
│   ├── match.md                # match式
│   ├── with-keyword.md         # with自動実装
│   ├── operators.md            # 演算子オーバーロード
│   ├── function-pointers.md    # 関数ポインタ
│   └── strings.md              # 文字列操作
│
└── compiler/                    # コンパイラ編（3チュートリアル）
    ├── usage.md                # コンパイラの使い方
    ├── llvm.md                 # LLVMバックエンド
    └── wasm.md                 # WASMバックエンド
```

---

## 🚀 使い方

### 初めて学ぶ場合

1. **[Hello, World!](basics/hello-world.md)** から始める
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
3. サンプルコード(`examples/`)を参照

---

## ✅ 実装状況一覧（v0.9.0）

| カテゴリ | 機能 | インタプリタ | LLVM | WASM | チュートリアル |
|---------|------|------------|------|------|---------------|
| **基本** | プリミティブ型 | ✅ | ✅ | ✅ | ✅ [variables](basics/variables.md) |
| | 制御構文 | ✅ | ✅ | ✅ | ✅ [control-flow](basics/control-flow.md) |
| | 関数 | ✅ | ✅ | ✅ | ✅ [functions](basics/functions.md) |
| **データ** | 構造体 | ✅ | ✅ | ✅ | ✅ [structs](types/structs.md) |
| | Enum | ✅ | ✅ | ✅ | ✅ [enums](types/enums.md) |
| | 配列 | ✅ | ✅ | ✅ | ✅ [arrays](basics/arrays.md) |
| | ポインタ | ✅ | ✅ | ✅ | ✅ [pointers](basics/pointers.md) |
| **型** | typedef | ✅ | ✅ | ✅ | ✅ [typedef](types/typedef.md) |
| | ジェネリクス | ✅ | ✅ | ✅ | ✅ [generics](types/generics.md) |
| | インターフェース | ✅ | ✅ | ✅ | ✅ [interfaces](types/interfaces.md) |
| | 型制約 | ✅ | ✅ | ✅ | ✅ [constraints](types/constraints.md) |
| **高度** | match式 | ✅ | ✅ | ✅ | ✅ [match](advanced/match.md) |
| | with自動実装 | ✅ | ✅ | ⚠️ | ✅ [with](advanced/with-keyword.md) |
| | operator | ✅ | ✅ | ✅ | ✅ [operators](advanced/operators.md) |
| | 関数ポインタ | ✅ | ✅ | ✅ | ✅ [function-pointers](advanced/function-pointers.md) |

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

- [正式言語仕様](../design/CANONICAL_SPEC.md) - 言語の完全な仕様
- [機能リファレンス](../features/) - 機能別の詳細仕様
- [サンプルコード](../../examples/) - 実用例
- [テストケース](../../tests/test_programs/) - 138ファイル

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

- [ ] コンパイラ編（3チュートリアル）
  - [ ] コンパイラの使い方
  - [ ] LLVMバックエンド
  - [ ] WASMバックエンド

---

**チュートリアル総数:** 22ファイル  
**推定学習時間:** 14-17時間  
**対象バージョン:** v0.9.0

---

**更新日:** 2024-12-16  
**著者:** Cm Language Development Team
