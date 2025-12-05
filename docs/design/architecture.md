# Cm コンパイラアーキテクチャ設計

## 概要

Cm言語処理系は、HIR → Rust/WASM/TSトランスパイラとして実装します。将来的にはMIR最適化層とネイティブ直接生成を追加します。

**開発言語**: C++20 (Clang 17+推奨, GCC 13+, MSVC 2019+)

## 開発段階

| Phase | 内容 | パイプライン |
|-------|------|-------------|
| **1 (現在)** | トランスパイラ | HIR → Rust/WASM/TS |
| 2 | 最適化追加 | HIR → MIR → 出力 |
| 3 | ネイティブ直接 | MIR → Cranelift/LLVM |

## コンパイルパイプライン (Phase 1)

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        Cm Compiler Pipeline (Phase 1)                       │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│   Source Code (.cm)                                                         │
│         │                                                                   │
│         ▼                                                                   │
│   ┌─────────────┐                                                           │
│   │   Lexer     │  字句解析: ソースコード → トークン列                        │
│   └─────────────┘                                                           │
│         │                                                                   │
│         ▼                                                                   │
│   ┌─────────────┐                                                           │
│   │   Parser    │  構文解析: トークン列 → AST                                │
│   └─────────────┘                                                           │
│         │                                                                   │
│         ▼                                                                   │
│   ┌─────────────┐                                                           │
│   │    AST      │  抽象構文木: ソース構造の忠実な表現                        │
│   └─────────────┘                                                           │
│         │                                                                   │
│         ▼                                                                   │
│   ┌─────────────┐                                                           │
│   │ Type Check  │  型検査: 型の整合性検証                                   │
│   └─────────────┘                                                           │
│         │                                                                   │
│         ▼                                                                   │
│   ┌─────────────┐                                                           │
│   │    HIR      │  High-level IR: 型情報付き、脱糖済み                       │
│   │ (Rust∩TS)   │  マルチターゲット対応設計                                  │
│   └─────────────┘                                                           │
│         │                                                                   │
│         ├────────────────────────────────────────┬───────────────┐          │
│         ▼                    ▼                   ▼               ▼          │
│   ┌──────────┐        ┌──────────┐        ┌──────────┐    ┌───────────┐    │
│   │   Rust   │        │   WASM   │        │    TS    │    │Interpreter│    │
│   │  (主力)  │        │(Rust経由)│        │          │    │ (開発用)  │    │
│   └────┬─────┘        └────┬─────┘        └────┬─────┘    └───────────┘    │
│        │                   │                   │                            │
│        ▼                   ▼                   ▼                            │
│   [rustc/cargo]       [wasm-pack]          [tsc]                            │
│        │                   │                   │                            │
│        ▼                   ▼                   ▼                            │
│     Native              Browser              Node.js                        │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

## 各フェーズの役割

### 1. Lexer（字句解析器）

**入力**: ソースコード（文字列）
**出力**: トークン列

責務:
- 文字列をトークンに分割
- コメントの除去
- 文字列リテラルの処理
- 位置情報（行番号・列番号）の追跡

### 2. Parser（構文解析器）

**入力**: トークン列
**出力**: AST（抽象構文木）
**実装**: 手書き再帰下降パーサ

責務:
- 文法規則に従った構文解析
- 構文エラーの検出と報告
- ASTノードの構築

実装方針:
- 文法規則ごとに `parse_*` 関数を定義
- Pratt Parserで演算子優先順位を処理
- エラー回復（同期トークンまでスキップ）

```cpp
// src/frontend/parser/parser.hpp
class Parser {
public:
    explicit Parser(std::vector<Token> tokens);
    std::unique_ptr<Ast> parse();

private:
    // トップレベル
    std::unique_ptr<AstFunction> parse_function();
    std::unique_ptr<AstStruct> parse_struct();
    
    // 文
    std::unique_ptr<AstStmt> parse_stmt();
    std::unique_ptr<AstStmt> parse_if_stmt();
    std::unique_ptr<AstStmt> parse_for_stmt();
    
    // 式（Pratt Parser）
    std::unique_ptr<AstExpr> parse_expr(int precedence = 0);
    std::unique_ptr<AstExpr> parse_primary();
    
    // ユーティリティ
    Token peek() const;
    Token advance();
    bool match(TokenKind kind);
    void expect(TokenKind kind, std::string_view msg);
};
```

### 3. AST（抽象構文木）

ソースコードの構造を忠実に表現するデータ構造。

特徴:
- ソースコードの構造をそのまま反映
- 位置情報を保持（エラー報告用）
- 糖衣構文はそのまま表現

### 4. Type Checker（型検査器）

**入力**: AST
**出力**: 型付きAST / 型エラー

責務:
- 型の推論と検証
- オーバーロード解決
- ジェネリクスのインスタンス化
- 型エラーの検出と報告

### 5. HIR（High-level IR）

**詳細**: [hir.md](hir.md)

特徴:
- 型情報が完全に付与
- 糖衣構文が脱糖済み
- 名前解決済み
- **Rust/TS両対応の共通表現**

### 6. バックエンド

**詳細**: [backends.md](backends.md)

| バックエンド | 用途 | 経路 |
|-------------|------|------|
| Rust (主力) | ネイティブ | HIR → Rust → rustc |
| WASM | Web | HIR → Rust → wasm-pack |
| TypeScript | JS統合 | HIR → TS → tsc |
| Interpreter | 開発 | HIR直接実行 |

## Cb言語との設計比較

| 項目 | Cb | Cm |
|------|-----|-----|
| 中間表現 | なし | HIR (→ MIR in Phase 2) |
| 最適化 | 限定的 | Phase 2でSSA最適化 |
| ターゲット | インタプリタのみ | Rust/WASM/TS |
| パッケージ管理 | なし | Cargo風 |
| 開発言語 | C++ | C++20 |

## ディレクトリ構成

```
src/
├── frontend/
│   ├── lexer/
│   │   ├── token.hpp       # トークン定義
│   │   └── lexer.hpp       # Lexerクラス
│   ├── parser/
│   │   ├── parser.hpp      # Parserクラス
│   │   └── grammar.hpp     # 文法定義
│   └── ast/
│       ├── ast.hpp         # ASTノード定義
│       └── visitor.hpp     # Visitorパターン
├── middle/
│   ├── hir/
│   │   ├── hir.hpp         # HIRノード定義
│   │   └── builder.hpp     # HIRビルダー
│   ├── type_check/
│   │   ├── types.hpp       # 型定義
│   │   └── checker.hpp     # 型検査器
│   └── lowering/
│       └── ast_to_hir.hpp  # AST→HIR
├── backend/
│   ├── rust/
│   │   └── emitter.hpp     # Rust出力
│   ├── typescript/
│   │   └── emitter.hpp     # TypeScript出力
│   ├── wasm/
│   │   └── emitter.hpp     # WASM設定生成
│   └── interpreter/
│       └── interpreter.hpp # HIRインタープリター
└── common/
    ├── source.hpp          # ソースコード管理
    ├── diagnostics.hpp     # エラー報告
    └── span.hpp            # 位置情報
```

## 将来の拡張

### Phase 2: MIR最適化

```
HIR → MIR (SSA/CFG) → 最適化パス → 各バックエンド
```

詳細: [mir.md](mir.md)

### Phase 3: ネイティブ直接生成

```
MIR → LIR → Cranelift or LLVM → Native
```

詳細: [lir.md](lir.md)

### その他

- **LSP対応**: HIRを活用した高精度補完
- **増分コンパイル**: 変更差分のみ再コンパイル
