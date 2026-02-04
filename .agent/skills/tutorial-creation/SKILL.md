---
name: tutorial-creation
description: チュートリアル作成ガイド
---

# チュートリアル作成ガイド

新機能追加時にチュートリアルを作成するためのスキルです。

## ディレクトリ構造

```
docs/tutorials/
├── ja/                    # 日本語版
│   ├── basics/            # 基本（変数、関数、制御構文等）
│   ├── types/             # 型システム（構造体、列挙型、インターフェース等）
│   ├── advanced/          # 応用（FFI、スレッド、マクロ等）
│   ├── compiler/          # コンパイラ機能（最適化等）
│   └── internals/         # 内部構造（アーキテクチャ等）
└── en/                    # 英語版（同構造）
```

## チュートリアル作成手順

### 1. フロントマター

```yaml
---
title: タイトル
parent: 親カテゴリ（Basics/Types/Advanced等）
nav_order: 番号
---
```

### 2. 構成テンプレート

```markdown
# タイトル

概要説明（1-2文）

## 基本的な使い方

```cm
// コード例
```

## 詳細説明

### サブセクション1

説明とコード例

### サブセクション2

説明とコード例

## 関連項目

- [関連チュートリアル1](リンク)
- [関連チュートリアル2](リンク)
```

### 3. コード例のルール

- 完全に動作するコードを記載
- コメントは日本語（ja）/ 英語（en）で
- 出力例がある場合はコメントで明記

### 4. 両言語同時作成

ja版とen版を同時に作成すること。

## チェックリスト

新しいチュートリアル作成時：

- [ ] `docs/tutorials/ja/[category]/[topic].md` 作成
- [ ] `docs/tutorials/en/[category]/[topic].md` 作成
- [ ] フロントマター（title, parent, nav_order）設定
- [ ] 動作するコード例を含める
- [ ] 関連チュートリアルへのリンク追加

## v0.12.0以降の機能とチュートリアル対応表

| 機能 | バージョン | チュートリアル | 状態 |
|-----|-----------|--------------|------|
| const強化 | v0.12.0 | advanced/const.md | ✅ 存在 |
| Linter | v0.12.0 | compiler/linter.md | ✅ 存在 |
| Formatter | v0.12.0 | compiler/formatter.md | ✅ 存在 |
| 末尾呼び出し最適化 | v0.12.0 | compiler/optimization.md | ✅ 存在 |
| 多次元配列最適化 | v0.12.0 | basics/arrays.md | ✅ 存在 |
| スレッド | v0.13.0 | advanced/thread.md | ✅ 存在 |
| Enum（関連データ付き） | v0.13.0 | types/enums.md | ✅ 更新済み |
| Match Guard | v0.13.0 | advanced/match.md | ✅ 存在 |
| mustキーワード | v0.13.0 | advanced/must.md | ✅ 存在 |
