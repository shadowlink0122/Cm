[English](CONTRIBUTING.en.html)

# Contributing to Cm

Cm言語プロジェクトへの貢献ガイドです。

## ディレクトリ構造

```
Cm/
├── src/                     # ソースコード
│   ├── frontend/           # Lexer, Parser, AST
│   │   ├── lexer/
│   │   ├── parser/
│   │   └── ast/
│   ├── middle/             # HIR, 型検査
│   │   ├── hir/
│   │   ├── type_check/
│   │   └── lowering/
│   ├── backend/            # コード生成, インタプリタ
│   │   ├── rust/
│   │   ├── typescript/
│   │   ├── wasm/
│   │   └── interpreter/
│   └── common/             # 共通ユーティリティ
│       ├── logger.hpp
│       ├── diagnostics.hpp
│       └── span.hpp
├── docs/                    # ドキュメント
│   ├── design/             # 設計ドキュメント
│   ├── spec/               # 言語仕様
│   └── releases/           # リリースノート
├── tests/                   # テスト
│   ├── unit/               # 単体テスト
│   ├── integration/        # 結合テスト
│   ├── e2e/                # E2Eテスト
│   └── regression/         # リグレッションテスト
├── examples/                # サンプルコード
└── tools/                   # 開発ツール
```

## ファイル命名規約

### C++ソース

| 種類 | 規約 | 例 |
|------|------|-----|
| ソースファイル | `snake_case.cpp` | `lexer.cpp` |
| ヘッダファイル | `snake_case.hpp` | `lexer.hpp` |
| クラス名 | `PascalCase` | `class Lexer` |
| 関数名 | `snake_case` | `void parse_expr()` |
| 定数 | `SCREAMING_CASE` | `MAX_TOKENS` |
| 名前空間 | `snake_case` | `namespace cm::frontend` |

### ドキュメント

| 種類 | 規約 | 例 |
|------|------|-----|
| 設計ドキュメント | `lowercase.md` | `hir.md` |
| リリースノート | `vX.Y.Z.md` | `v0.1.0.md` |

### テスト

| 種類 | 規約 | 例 |
|------|------|-----|
| 単体テスト | `*_test.cpp` | `lexer_test.cpp` |
| テストファイル | `*.cm` | `hello.cm` |
| 期待出力 | `*.expected` | `hello.expected` |

---

## コーディング規約

### C++20

```cpp
// ヘッダガード
#pragma once

// 名前空間
namespace cm::frontend {

// クラス定義
class Lexer {
public:
    // コンストラクタ
    explicit Lexer(std::string_view source);
    
    // メソッド
    std::vector<Token> tokenize();
    
private:
    // メンバ変数（末尾アンダースコア）
    std::string source_;
    size_t pos_;
};

} // namespace cm::frontend
```

### フォーマット

```bash
# clang-formatで自動整形
clang-format -i src/**/*.cpp src/**/*.hpp
```

---

## ブランチ戦略

```
main          # 安定版
├── develop   # 開発ブランチ
├── feature/* # 機能開発
├── fix/*     # バグ修正
└── release/* # リリース準備
```

## コミットメッセージ

```
<種類>: <要約>

<詳細>

<種類>:
- feat: 新機能
- fix: バグ修正
- docs: ドキュメント
- refactor: リファクタリング
- test: テスト
- chore: その他
```

例:
```
feat: HIRにパターンマッチを追加

- HirMatch, HirMatchArm ノードを追加
- AST→HIR変換を実装
- tests/unit/hir/match_test.cpp を追加
```

---

## プルリクエスト

1. `develop` からブランチを作成
2. 変更を実装
3. `cm fmt && cm lint` を実行
4. テストを追加・実行
5. PRを作成

### PRテンプレート

```markdown
## 概要
<!-- 変更内容 -->

## 変更種類
- [ ] 新機能
- [ ] バグ修正
- [ ] リファクタリング
- [ ] ドキュメント

## テスト
- [ ] 単体テスト追加
- [ ] 既存テスト通過

## チェックリスト
- [ ] コードフォーマット済み
- [ ] ドキュメント更新済み
```

---

## リリースプロセス

1. `release/vX.Y.Z` ブランチ作成
2. バージョン番号更新
3. `docs/releases/vX.Y.Z.md` 作成
4. テスト実行
5. `main` へマージ
6. タグ作成: `git tag vX.Y.Z`