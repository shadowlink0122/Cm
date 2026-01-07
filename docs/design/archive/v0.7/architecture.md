[English](architecture.en.html)

# Cm コンパイラアーキテクチャ設計

## 概要

Cm言語処理系は、LLVMバックエンドによるネイティブコンパイラとして実装されています。

> **2025年12月より、LLVMバックエンドを唯一のコード生成方式として採用しています。**
> 以前検討されていたRust/TypeScript/C++へのトランスパイルは廃止されました。

**開発言語**: C++20 (Clang 17+推奨, GCC 13+, MSVC 2019+)

## コンパイルパイプライン

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        Cm Compiler Pipeline                                 │
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
│   └─────────────┘                                                           │
│         │                                                                   │
│         ▼                                                                   │
│   ┌─────────────┐                                                           │
│   │    MIR      │  Mid-level IR: SSA形式、CFGベース                         │
│   └─────────────┘                                                           │
│         │                                                                   │
│         ├────────────────────────────────────────┐                          │
│         ▼                                        ▼                          │
│   ┌─────────────┐                          ┌───────────┐                    │
│   │  LLVM IR    │                          │Interpreter│                    │
│   │ (CodeGen)   │                          │ (開発用)  │                    │
│   └─────┬───────┘                          └───────────┘                    │
│         │                                                                   │
│         ├─────────────────────┬─────────────────────┐                       │
│         ▼                     ▼                     ▼                       │
│     ┌───────┐            ┌─────────┐           ┌────────┐                   │
│     │x86_64 │            │  ARM64  │           │  WASM  │                   │
│     └───┬───┘            └────┬────┘           └───┬────┘                   │
│         │                     │                    │                        │
│         ▼                     ▼                    ▼                        │
│   Linux/Windows/macOS    macOS/Linux/組み込み   Browser                     │
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

**詳細**: [hir.md](hir.html)

特徴:
- 型情報が完全に付与
- 糖衣構文が脱糖済み
- 名前解決済み

### 6. MIR（Mid-level IR）

**詳細**: [mir.md](mir.html)

特徴:
- SSA形式（Static Single Assignment）
- CFG（Control Flow Graph）ベース
- 最適化に適した表現

### 7. LLVMバックエンド

**詳細**: [../llvm_backend_implementation.md](../llvm_backend_implementation.html)

| ターゲット | 用途 | 経路 |
|------------|------|------|
| x86_64 | デスクトップ | MIR → LLVM IR → x86_64 |
| ARM64 | Apple Silicon/Linux | MIR → LLVM IR → ARM64 |
| WASM | Web | MIR → LLVM IR → WASM |

### 廃止されたバックエンド

以下のバックエンドは2025年12月に廃止されました（ソースコードは参考のため保持）:

| バックエンド | 廃止理由 |
|-------------|----------|
| Rustトランスパイラ | LLVMバックエンドに置き換え |
| TypeScriptトランスパイラ | LLVMバックエンドに置き換え |

## Cb言語との設計比較

| 項目 | Cb | Cm |
|------|-----|-----|
| 中間表現 | なし | HIR → MIR |
| コード生成 | なし | LLVM IR |
| 最適化 | 限定的 | MIR最適化 + LLVM最適化 |
| ターゲット | インタプリタのみ | ネイティブ / WASM |
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
│   ├── mir/
│   │   ├── mir_nodes.hpp   # MIRノード定義
│   │   └── mir_lowering.hpp# HIR→MIR
│   ├── type_check/
│   │   ├── types.hpp       # 型定義
│   │   └── checker.hpp     # 型検査器
│   └── lowering/
│       └── ast_to_hir.hpp  # AST→HIR
├── codegen/
│   └── llvm/               # LLVMバックエンド（メイン）
│       ├── context.hpp/cpp
│       ├── mir_to_llvm.hpp/cpp
│       ├── llvm_codegen.hpp/cpp
│       └── runtime.c       # Cランタイム
├── backends/               # レガシーバックエンド（参考のため保持）
│   ├── rust/               # 廃止
│   ├── typescript/         # 廃止
│   └── interpreter/
│       └── interpreter.hpp # MIRインタープリター
└── common/
    ├── source.hpp          # ソースコード管理
    ├── diagnostics.hpp     # エラー報告
    └── span.hpp            # 位置情報
```

## 将来の拡張

### WebAssemblyターゲット

```
MIR → LLVM IR → WASM (wasm32-unknown-unknown)
```

### その他

- **LSP対応**: HIRを活用した高精度補完
- **増分コンパイル**: 変更差分のみ再コンパイル