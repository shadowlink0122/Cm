# Cm言語 統一診断システム設計

**文書番号:** 079
**作成日:** 2026-01-13
**バージョン:** v0.12.0
**ステータス:** 設計確定

## 1. 概要

コンパイラフロントエンドとLinter/Formatterを完全統合した診断システム。**型チェックとLintは同じセマンティック解析基盤で実行され**、コンパイルエラー、警告、Lintルールを一元管理する。

### 核心的な特徴
- **1回の解析**: Lexer/Parser/セマンティック解析を1回だけ実行
- **情報共有**: 型情報、シンボルテーブル、CFGを型チェックとLintで共有
- **統一診断**: E/W/L番号体系で全診断を一元管理
- **高速化**: 重複解析の排除により約46%の性能向上

## 2. 統合アーキテクチャ

### 2.1 共有コンポーネント

```
┌────────────────────────────────────────────────────────┐
│                    Source Code                         │
└────────────────────────────────────────────────────────┘
                            │
                            ▼
┌────────────────────────────────────────────────────────┐
│              Trivia-aware Lexer (共有)                 │
│            Token + Trivia + Location                   │
└────────────────────────────────────────────────────────┘
                            │
                            ▼
┌────────────────────────────────────────────────────────┐
│              Trivia-aware Parser (共有)                │
│                   AST with Trivia                      │
└────────────────────────────────────────────────────────┘
                            │
                            ▼
┌────────────────────────────────────────────────────────┐
│           Semantic Analyzer (共有)                     │
│     Type Checking + Symbol Table + Control Flow        │
└────────────────────────────────────────────────────────┘
                            │
         ┌──────────────────┼──────────────────┐
         ▼                  ▼                  ▼
┌─────────────────┐ ┌─────────────┐ ┌─────────────────┐
│  Compiler Core  │ │ Linter Core │ │ Formatter Core  │
│ (Error + HIR/MIR)│ │ (Style/Best)│ │ (Pretty Print)  │
└─────────────────┘ └─────────────┘ └─────────────────┘
         │                  │                  │
         ▼                  ▼                  ▼
    Compilation      Lint Warnings      Formatted Code
```

### 2.2 統一診断レベル

```cpp
enum class DiagnosticLevel {
    // コンパイルを停止
    Error = 0,        // 構文エラー、型エラーなど

    // コンパイルは継続するが警告
    Warning = 1,      // 未使用変数、到達不可能コードなど

    // Lintレベル（オプショナル）
    Suggestion = 2,   // スタイル違反、改善提案
    Hint = 3,         // ベストプラクティス、パフォーマンス

    // 内部用
    Note = 4,         // エラーの補足情報
    Help = 5          // 修正方法の提案
};
```

## 3. 統一診断システム

### 3.1 診断定義

```cpp
// すべての診断を統一的に定義
class DiagnosticDefinition {
public:
    string id;                    // "E001", "W001", "L001"
    string name;                  // "undefined-variable"
    DiagnosticLevel default_level;
    string message_template;      // "{0} is not defined"

    // どの段階で検出可能か
    enum Stage {
        Lexer,      // 字句解析
        Parser,     // 構文解析
        Semantic,   // 意味解析
        TypeCheck,  // 型検査
        Lint,       // Lint専用（スタイルなど）
        Codegen     // コード生成
    };
    Stage detection_stage;

    // 自動修正可能か
    bool is_fixable;

    // 修正提案を生成
    virtual vector<FixSuggestion> suggest_fixes(
        const DiagnosticContext& ctx
    ) const { return {}; }
};
```

### 3.2 診断カタログ

```cpp
// すべての診断を一元管理
class DiagnosticCatalog {
private:
    // カテゴリ別の診断定義
    map<string, DiagnosticDefinition> diagnostics = {
        // ========== エラー（E001-E999）==========
        // 構文エラー
        {"E001", {"E001", "syntax-error", Error,
                  "Unexpected token '{0}'", Parser}},
        {"E002", {"E002", "missing-semicolon", Error,
                  "Expected ';' after statement", Parser}},

        // 型エラー
        {"E100", {"E100", "type-mismatch", Error,
                  "Type mismatch: expected '{0}', got '{1}'", TypeCheck}},
        {"E101", {"E101", "undefined-variable", Error,
                  "Variable '{0}' is not defined", Semantic}},
        {"E102", {"E102", "undefined-function", Error,
                  "Function '{0}' is not defined", Semantic}},

        // 所有権エラー（v0.11.0）
        {"E200", {"E200", "use-after-move", Error,
                  "Variable '{0}' used after move", Semantic}},
        {"E201", {"E201", "modify-while-borrowed", Error,
                  "Cannot modify '{0}' while borrowed", Semantic}},

        // ========== 警告（W001-W999）==========
        {"W001", {"W001", "unused-variable", Warning,
                  "Variable '{0}' is never used", Semantic, true}},
        {"W002", {"W002", "unreachable-code", Warning,
                  "Code after return is unreachable", Semantic}},
        {"W003", {"W003", "implicit-conversion", Warning,
                  "Implicit conversion from '{0}' to '{1}'", TypeCheck}},

        // ========== Lintルール（L001-L999）==========
        // スタイル
        {"L001", {"L001", "naming-convention", Suggestion,
                  "Name '{0}' does not follow {1} convention", Lint, true}},
        {"L002", {"L002", "missing-const", Suggestion,
                  "Variable '{0}' could be const", Lint, true}},

        // パフォーマンス
        {"L100", {"L100", "unnecessary-copy", Hint,
                  "Unnecessary copy of '{0}'", Lint, true}},
        {"L101", {"L101", "inefficient-loop", Hint,
                  "Loop can be optimized using range-for", Lint, true}},
    };

public:
    const DiagnosticDefinition* get(const string& id) const {
        auto it = diagnostics.find(id);
        return it != diagnostics.end() ? &it->second : nullptr;
    }
};
```

## 4. 統合実装

### 4.1 共通セマンティック解析

```cpp
// コンパイラとLinterで共有されるセマンティック情報
class UnifiedSemanticAnalyzer {
private:
    ASTNodeWithTrivia* ast;
    SymbolTable symbols;
    TypeEnvironment types;
    ControlFlowGraph cfg;
    UseDefChain use_def;

public:
    // 一度の解析ですべての情報を収集
    void analyze(ASTNodeWithTrivia* root) {
        // 1. シンボル収集
        collect_symbols(root);

        // 2. 型推論と型チェック
        infer_and_check_types(root);

        // 3. 制御フロー解析
        build_control_flow_graph(root);

        // 4. 使用-定義チェイン構築
        build_use_def_chains(root);

        // 5. 診断の収集（エラー、警告、Lint）
        collect_all_diagnostics(root);
    }

    // すべての診断を一度に収集
    vector<Diagnostic> collect_all_diagnostics(ASTNodeWithTrivia* node) {
        vector<Diagnostic> diagnostics;

        // コンパイラエラー/警告
        collect_compiler_diagnostics(node, diagnostics);

        // Lintルール（設定で有効なもののみ）
        if (config.lint_enabled) {
            collect_lint_diagnostics(node, diagnostics);
        }

        return diagnostics;
    }
};
```

### 4.2 診断エンジン

```cpp
class DiagnosticEngine {
private:
    DiagnosticCatalog catalog;
    DiagnosticConfig config;
    vector<Diagnostic> diagnostics;

public:
    // 診断を報告
    void report(const string& id, SourceLocation loc,
                const vector<string>& args = {}) {
        auto* def = catalog.get(id);
        if (!def) return;

        // 設定でレベルが変更されている場合
        auto level = config.get_level_override(id)
                     .value_or(def->default_level);

        // 無効化されている場合はスキップ
        if (config.is_disabled(id)) return;

        diagnostics.push_back({
            .id = id,
            .level = level,
            .location = loc,
            .message = format_message(def->message_template, args),
            .fixes = def->suggest_fixes(get_context(loc))
        });
    }

    // エラーがあるか（コンパイル停止判定）
    bool has_errors() const {
        return any_of(diagnostics.begin(), diagnostics.end(),
            [](const auto& d) { return d.level == Error; });
    }

    // 自動修正を適用
    void apply_fixes() {
        for (auto& diag : diagnostics) {
            if (!diag.fixes.empty() && config.auto_fix) {
                apply_fix(diag.fixes[0]);  // 最初の修正を適用
            }
        }
    }
};
```

## 5. CLI統合

### 5.1 統一されたCLIインターフェース

```bash
# コンパイル（エラーと警告のみ）
cm build main.cm

# コンパイル + Lint
cm build main.cm --lint

# Lintのみ（コンパイルしない）
cm check main.cm

# すべての診断を表示
cm check main.cm --pedantic

# 特定のルールを有効/無効
cm check main.cm --enable=L001,L002 --disable=W001

# 自動修正
cm check main.cm --fix

# フォーマット（診断なし）
cm fmt main.cm
```

### 5.2 設定ファイル

```yaml
# .cm-config.yml - 統一設定ファイル
diagnostics:
  # レベルのカスタマイズ
  W001: error     # 未使用変数をエラーに昇格
  L001: warning   # 命名規則を警告に昇格
  L100: disabled  # 不要なコピーチェックを無効化

  # カテゴリ単位の設定
  categories:
    performance: hint    # すべてのパフォーマンスルールをhintに
    style: warning      # すべてのスタイルルールをwarningに

# Linter設定
lint:
  enabled: true
  auto_fix: true

  # カスタムルール
  rules:
    naming_convention:
      functions: snake_case
      types: PascalCase
      constants: UPPER_CASE

# Formatter設定
format:
  indent_width: 4
  line_width: 80
  brace_style: kr
```

## 6. 実装の利点

### 6.1 コードの再利用
- Lexer/Parser/Semantic Analyzerを共有
- シンボルテーブルと型情報を一度だけ構築
- 制御フロー解析の結果を共有

### 6.2 一貫性
- エラー番号体系の統一（E/W/L + 番号）
- 診断メッセージフォーマットの統一
- 修正提案の統一されたインターフェース

### 6.3 パフォーマンス
- 一度の解析ですべての診断を収集
- インクリメンタル解析が容易
- キャッシュの効率的な利用

### 6.4 拡張性
- 新しい診断の追加が容易
- カスタムルールのプラグイン対応
- IDE統合（LSP）への道筋が明確

## 7. 段階的移行計画

### Phase 1: 基盤統合（1週間）
- [ ] DiagnosticCatalogの実装
- [ ] 既存のエラー/警告をカタログに移行
- [ ] DiagnosticEngineの実装

### Phase 2: セマンティック統合（2週間）
- [ ] UnifiedSemanticAnalyzerの実装
- [ ] シンボルテーブルの共有化
- [ ] 制御フロー解析の統合

### Phase 3: Linter統合（1週間）
- [ ] LintルールをDiagnosticCatalogに追加
- [ ] 既存のLinterロジックを移行
- [ ] 自動修正機能の実装

### Phase 4: CLI/設定統合（1週間）
- [ ] 統一CLIコマンドの実装
- [ ] 統一設定ファイルのサポート
- [ ] 後方互換性の確保

## 8. 既存コードベースとの統合例

```cpp
// 現在のコンパイラコード（frontend/type_check.cpp）
void TypeChecker::check_variable(VarDecl* var) {
    if (!symbol_table.lookup(var->name)) {
        // 従来のエラー報告
        error(var->loc, "Undefined variable: " + var->name);
    }

    // 未使用チェック（警告）
    if (!is_used(var->name)) {
        warning(var->loc, "Unused variable: " + var->name);
    }
}

// ↓ 統合後

void TypeChecker::check_variable(VarDecl* var) {
    if (!symbol_table.lookup(var->name)) {
        // 統一診断システムを使用
        diag.report("E101", var->loc, {var->name});
    }

    // 未使用チェックは自動的に実行される
    // （UnifiedSemanticAnalyzerが処理）
}
```

## 9. 関連ドキュメント

- [080_diagnostic_catalog.md](./080_diagnostic_catalog.md) - 診断カタログ（E/W/L定義）
- [081_implementation_guide.md](./081_implementation_guide.md) - 実装ガイド
- [078_linter_formatter_design.md](./078_linter_formatter_design.md) - 初期設計（参考）