[English](PROJECT_STRUCTURE.en.html)

# Cm言語 プロジェクト構造

*最終更新: 2025年1月*

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
│   │   └── llvm/            # LLVMバックエンド
│   │       ├── native/      # ネイティブコード生成
│   │       └── wasm/        # WebAssemblyコード生成
│   ├── preprocessor/        # プリプロセッサ
│   │   └── import.*         # import処理、ソースマップ
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
│   ├── design/              # 設計文書
│   │   └── MODULE_SYSTEM_FINAL.md
│   ├── spec/                # 言語仕様
│   ├── guides/              # ガイド
│   ├── tutorials/           # チュートリアル
│   └── archive/             # アーカイブ済み文書
│
├── std/                     # 標準ライブラリ
│
├── examples/                # サンプルコード
│
├── scripts/                 # ユーティリティスクリプト
│   └── run_tests.sh         # テスト実行スクリプト
│
├── build/                   # ビルド出力（.gitignore）
│
├── CMakeLists.txt           # ビルド設定
├── Makefile                 # テスト実行用
├── ROADMAP.md               # 開発ロードマップ
├── README.md                # プロジェクト説明
├── CLAUDE.md                # AI開発ガイド
├── CONTRIBUTING.md          # 貢献ガイド
└── CHANGELOG.md             # 変更履歴
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
┌─────────────────────────────────────┐
│         コード生成                    │
├─────────────────────────────────────┤
│ インタプリタ │ LLVM Native │  WASM   │
└─────────────────────────────────────┘
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

# インタプリタテスト (203テスト)
make tip

# LLVMテスト
make tlp

# WASMテスト
make tlwp

# 全テスト
make tall
```

## 関連ドキュメント

- [開発環境](DEVELOPMENT.html)
- [クイックスタート](QUICKSTART.html)
- [機能一覧](FEATURES.html)