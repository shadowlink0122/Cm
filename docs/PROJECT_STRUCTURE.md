---
layout: default
title: Project Structure
---

[English](PROJECT_STRUCTURE.en.html)

# Cm言語 プロジェクト構造

*最終更新: 2026年2月*

## ディレクトリ構成

```
Cm/
├── src/                      # ソースコード
│   ├── main.cpp             # エントリポイント
│   ├── common/              # 共通ユーティリティ
│   │   ├── debug_messages.hpp
│   │   ├── span.hpp
│   │   ├── source_location.hpp
│   │   └── diagnostics.hpp
│   ├── frontend/            # フロントエンド
│   │   ├── lexer/           # 字句解析
│   │   ├── parser/          # 構文解析
│   │   ├── ast/             # 抽象構文木
│   │   └── types/           # 型チェッカー
│   │       └── checking/    # 型チェック実装（分割）
│   ├── hir/                 # 高レベル中間表現
│   │   └── lowering/        # HIR lowering（分割）
│   ├── mir/                 # 中レベル中間表現（SSA形式）
│   │   ├── lowering/        # MIR lowering（分割）
│   │   └── optimizations/   # 最適化パス
│   ├── codegen/             # コード生成
│   │   ├── interpreter/     # MIRインタプリタ
│   │   ├── js/              # JavaScriptバックエンド
│   │   └── llvm/            # LLVMバックエンド
│   │       ├── native/      # ネイティブコード生成
│   │       └── wasm/        # WebAssemblyコード生成
│   ├── preprocessor/        # プリプロセッサ
│   │   └── import.*         # import処理、ソースマップ
│   ├── diagnostics/         # 診断システム
│   │   └── definitions/     # エラー/警告/Lint定義
│   ├── fmt/                 # フォーマッタ
│   └── module/              # モジュール解決
│
├── tests/                   # テストファイル
│   ├── test_programs/       # Cmテストプログラム
│   │   ├── basic/           # 基本テスト
│   │   ├── control_flow/    # 制御フロー
│   │   ├── types/           # 型システム
│   │   ├── generics/        # ジェネリクス
│   │   ├── interface/       # インターフェース
│   │   ├── modules/         # モジュールシステム
│   │   ├── array/           # 配列
│   │   ├── pointer/         # ポインタ
│   │   ├── string/          # 文字列
│   │   └── ...
│   └── *.cpp                # C++ユニットテスト
│
├── docs/                    # ドキュメント
│   ├── tutorials/           # チュートリアル（ja/en）
│   ├── design/              # 設計文書（v0.13.0, v0.14.0）
│   ├── archive/             # アーカイブ済み文書
│   ├── flyer/               # フライヤー
│   └── releases/            # リリースノート
│
├── std/                     # 標準ライブラリ
│
├── examples/                # サンプルコード
│
├── scripts/                 # ユーティリティスクリプト
│   ├── test-in-docker.sh    # Docker CI環境テスト
│   ├── docker-shell.sh      # 対話シェル
│   └── validate_bnf.py      # BNF検証
│
├── build/                   # ビルド出力（.gitignore）
│
├── CMakeLists.txt           # ビルド設定
├── Makefile                 # テスト実行用
├── ROADMAP.md               # 開発ロードマップ
├── README.md                # プロジェクト説明
├── CONTRIBUTING.md          # 貢献ガイド
└── VERSION                  # バージョン番号
```

## アーキテクチャ

```
ソースコード
    ↓
プリプロセッサ (import展開、ソースマップ生成)
    ↓
Lexer (トークン化)
    ↓
Parser (AST生成)
    ↓
TypeChecker (型検査)
    ↓
HIR Lowering (脱糖)
    ↓
MIR Lowering (CFG構築)
    ↓
最適化 (定数畳み込み、DCE等)
    ↓
┌─────────────────────────────────────────────────┐
│              コード生成                           │
├─────────────────────────────────────────────────┤
│ インタプリタ │ JIT │ LLVM Native │ WASM │ JS   │
└─────────────────────────────────────────────────┘
```

## ルールと規約

### プロジェクトルート配置禁止

- ✗ ソースファイル (*.cpp, *.hpp, *.cm)
- ✗ 実行ファイル
- ✗ 個別の実装ドキュメント

### デバッグ出力形式

```
[STAGE] メッセージ
```

例：
```
[LEXER] Starting tokenization
[PARSER] Parsing function: main
[MIR] Lowering to MIR
```

### ファイルサイズガイドライン

- 1ファイル1000行目安
- 大きくなった場合は分割を検討

## テスト実行

```bash
# C++ユニットテスト
ctest --test-dir build

# インタプリタテスト（パラレル）
make tip

# LLVMテスト（パラレル）
make tlp

# WASMテスト
make tlwp

# JSテスト
make tjp

# 全テスト
make tall
```

## 関連ドキュメント

- [開発環境](DEVELOPMENT.html)
- [クイックスタート](QUICKSTART.html)
- [機能一覧](FEATURES.html)