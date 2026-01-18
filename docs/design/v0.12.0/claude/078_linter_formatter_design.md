# Cm言語 Linter/Formatter 詳細設計

**文書番号:** 078
**作成日:** 2026-01-13
**バージョン:** v0.12.0
**ステータス:** 設計中

## 1. 概要

Cm言語のLinterとFormatterの統合実装設計。Trivia（空白・コメント）を保持したまま、コードの静的解析とフォーマッティングを行う。

## 2. アーキテクチャ

### 2.1 全体構成

```
┌─────────────────────────────────────────────────────┐
│                     CLI Interface                    │
│                  cm lint / cm fmt                    │
└──────────────┬──────────────────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────────────────┐
│                  Trivia-aware Lexer                 │
│              Token + Trivia の保持                   │
└──────────────┬──────────────────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────────────────┐
│                  Trivia-aware Parser                │
│              AST + Trivia の関連付け                 │
└──────────┬─────────────────────────┬────────────────┘
           │                         │
           ▼                         ▼
┌───────────────────────┐ ┌───────────────────────────┐
│      Linter Core      │ │      Formatter Core       │
│   Rule-based Analysis │ │   Style Configuration    │
└───────────────────────┘ └───────────────────────────┘
           │                         │
           ▼                         ▼
┌───────────────────────┐ ┌───────────────────────────┐
│   Diagnostic Report   │ │    Formatted Output       │
└───────────────────────┘ └───────────────────────────┘
```

### 2.2 Trivia保持の仕組み

```cpp
// Token定義の拡張
struct TokenWithTrivia {
    Token token;                    // 本体のトークン
    vector<Trivia> leading_trivia;  // 前方の空白・コメント
    vector<Trivia> trailing_trivia; // 後方の空白・コメント
};

struct Trivia {
    enum Type {
        Whitespace,      // 空白文字
        Newline,         // 改行
        LineComment,     // // コメント
        BlockComment,    // /* */ コメント
        DocComment       // /// ドキュメントコメント
    };

    Type type;
    string text;
    SourceLocation location;
};
```

## 3. Linter実装

### 3.1 ルールカテゴリ

| カテゴリ | 説明 | 例 |
|---------|------|-----|
| **Style** | コーディングスタイル | 命名規則、インデント |
| **Complexity** | 複雑度チェック | 循環複雑度、ネスト深度 |
| **Safety** | 安全性チェック | 未使用変数、未初期化 |
| **Performance** | パフォーマンス | 不要なコピー、最適化可能 |
| **Correctness** | 正確性チェック | 型不整合、nullポインタ |

### 3.2 ルール定義形式

```cpp
class LintRule {
public:
    virtual ~LintRule() = default;

    // ルールメタデータ
    virtual string get_id() const = 0;        // "CM001"
    virtual string get_name() const = 0;      // "unused-variable"
    virtual Severity get_severity() const = 0; // Warning/Error
    virtual string get_description() const = 0;

    // チェック実装
    virtual void visit(ASTNode* node, LintContext& ctx) = 0;
};

// 具体的なルール例
class UnusedVariableRule : public LintRule {
    string get_id() const override { return "CM001"; }
    string get_name() const override { return "unused-variable"; }
    Severity get_severity() const override { return Severity::Warning; }

    void visit(ASTNode* node, LintContext& ctx) override {
        if (auto var = dynamic_cast<VarDecl*>(node)) {
            if (!ctx.is_used(var->name)) {
                ctx.report_diagnostic(
                    var->location,
                    "Unused variable: " + var->name,
                    get_id()
                );
            }
        }
    }
};
```

### 3.3 実装済みルール一覧（予定）

#### Style Rules (CM001-CM099)
- **CM001**: unused-variable - 未使用変数の検出
- **CM002**: naming-convention - 命名規則違反
- **CM003**: line-length - 行長制限超過
- **CM004**: trailing-whitespace - 末尾空白
- **CM005**: missing-const - constキーワード欠落

#### Complexity Rules (CM100-CM199)
- **CM100**: cyclomatic-complexity - 循環複雑度が閾値超過
- **CM101**: nesting-depth - ネスト深度が閾値超過
- **CM102**: function-length - 関数が長すぎる
- **CM103**: parameter-count - パラメータ数が多すぎる

#### Safety Rules (CM200-CM299)
- **CM200**: uninitialized-variable - 未初期化変数の使用
- **CM201**: null-dereference - nullポインタ参照の可能性
- **CM202**: resource-leak - リソースリークの可能性
- **CM203**: use-after-move - move後の使用

#### Performance Rules (CM300-CM399)
- **CM300**: unnecessary-copy - 不要なコピー
- **CM301**: inefficient-loop - 非効率なループ
- **CM302**: redundant-computation - 冗長な計算

### 3.4 設定ファイル形式

```yaml
# .cmlint.yml
rules:
  # グローバル設定
  severity: warning  # default severity

  # 個別ルール設定
  CM001:
    enabled: true
    severity: error

  CM100:
    enabled: true
    max_complexity: 10

  CM101:
    enabled: true
    max_nesting: 4

# 除外パターン
exclude:
  - "tests/**/*.cm"
  - "vendor/**/*.cm"
  - "*.generated.cm"

# カスタムルールのパス
custom_rules:
  - "./tools/lint_rules"
```

## 4. Formatter実装

### 4.1 フォーマットスタイル

#### 4.1.1 インデント
```cm
// 4スペース（デフォルト）
int main() {
    if (condition) {
        do_something();
    }
}

// タブ（オプション）
int main() {
	if (condition) {
		do_something();
	}
}
```

#### 4.1.2 ブレーススタイル
```cm
// K&R（デフォルト）
int main() {
    if (condition) {
        // ...
    } else {
        // ...
    }
}

// Allman
int main()
{
    if (condition)
    {
        // ...
    }
    else
    {
        // ...
    }
}
```

#### 4.1.3 行の折り返し
```cm
// 80文字で折り返し
int very_long_function_name(
    int first_parameter,
    int second_parameter,
    int third_parameter
) {
    return first_parameter +
           second_parameter +
           third_parameter;
}
```

### 4.2 Formatter設定

```yaml
# .cmfmt.yml
style:
  # インデント
  indent_style: space  # space | tab
  indent_width: 4

  # ブレース
  brace_style: kr  # kr | allman | stroustrup

  # 行長
  line_width: 80

  # 空白
  spaces_around_operators: true
  spaces_after_commas: true
  spaces_in_parens: false

  # 改行
  blank_lines_between_functions: 1
  blank_lines_at_eof: 1

  # アライメント
  align_assignments: false
  align_struct_fields: true

  # コメント
  format_comments: true
  wrap_comments: true
```

### 4.3 Formatter アルゴリズム

```cpp
class Formatter {
private:
    FormatConfig config;

public:
    string format(ASTNodeWithTrivia* root) {
        FormatterContext ctx(config);

        // 1. ASTを走査してフォーマット情報を収集
        collect_format_info(root, ctx);

        // 2. インデントレベルを計算
        calculate_indentation(root, ctx);

        // 3. 改行位置を決定
        determine_line_breaks(root, ctx);

        // 4. 空白を調整
        adjust_whitespace(root, ctx);

        // 5. コメントを整形
        format_comments(root, ctx);

        // 6. 最終的なテキストを生成
        return generate_output(root, ctx);
    }
};
```

### 4.4 Trivia処理の詳細

```cpp
class TriviaFormatter {
public:
    void format_leading_trivia(vector<Trivia>& trivia, FormatterContext& ctx) {
        for (auto& t : trivia) {
            switch (t.type) {
            case Trivia::LineComment:
                // "//"の後にスペースを確保
                if (!t.text.starts_with("// ")) {
                    t.text = "// " + t.text.substr(2);
                }
                break;

            case Trivia::BlockComment:
                // ブロックコメントのインデントを調整
                format_block_comment(t, ctx);
                break;

            case Trivia::Whitespace:
                // インデントレベルに応じて調整
                t.text = ctx.get_indent_string();
                break;
            }
        }
    }
};
```

## 5. 統合とCLI

### 5.1 CLIコマンド

```bash
# Linter
cm lint file.cm                    # 単一ファイルをチェック
cm lint src/                       # ディレクトリを再帰的にチェック
cm lint --config .cmlint.yml      # 設定ファイル指定
cm lint --fix                     # 自動修正可能な問題を修正
cm lint --format json             # JSON形式で出力

# Formatter
cm fmt file.cm                     # 単一ファイルをフォーマット
cm fmt src/                        # ディレクトリを再帰的にフォーマット
cm fmt --check                    # チェックのみ（変更しない）
cm fmt --config .cmfmt.yml        # 設定ファイル指定
cm fmt --stdin                    # 標準入力から読み込み
```

### 5.2 エラー出力形式

```
src/main.cm:10:5: warning: Unused variable 'x' [CM001]
    int x = 10;
        ^

src/lib.cm:25:1: error: Function 'process' exceeds complexity limit (15 > 10) [CM100]
void process() {
^~~~~~~~~~~~~

Found 1 error and 1 warning in 2 files.
```

## 6. 実装計画

### Phase 1: 基盤実装（2週間）
- [ ] Trivia保持Lexer
- [ ] Trivia保持Parser
- [ ] 基本的なLinterフレームワーク
- [ ] 基本的なFormatterフレームワーク

### Phase 2: 基本ルール（1週間）
- [ ] Style rules (CM001-CM010)
- [ ] 基本的なフォーマット機能
- [ ] CLI統合

### Phase 3: 高度なルール（2週間）
- [ ] Complexity rules
- [ ] Safety rules
- [ ] Performance rules
- [ ] 設定ファイルサポート

### Phase 4: 最適化と統合（1週間）
- [ ] パフォーマンス最適化
- [ ] エディタ統合（LSP）
- [ ] CI/CD統合

## 7. テスト戦略

### 7.1 単体テスト

```cpp
TEST(LinterTest, UnusedVariable) {
    const char* code = R"(
        int main() {
            int x = 10;  // unused
            int y = 20;
            return y;
        }
    )";

    auto diagnostics = lint(code);
    EXPECT_EQ(diagnostics.size(), 1);
    EXPECT_EQ(diagnostics[0].rule_id, "CM001");
    EXPECT_EQ(diagnostics[0].line, 3);
}
```

### 7.2 フォーマッターテスト

```cpp
TEST(FormatterTest, IndentStyle) {
    const char* input = "int main(){if(x){return 1;}}";
    const char* expected = R"(int main() {
    if (x) {
        return 1;
    }
})";

    EXPECT_EQ(format(input), expected);
}
```

## 8. パフォーマンス目標

- **Linter**: 1000行/秒以上
- **Formatter**: 5000行/秒以上
- **メモリ使用量**: ファイルサイズの10倍以下
- **インクリメンタル処理**: 変更部分のみ再解析

## 9. エディタ統合（将来）

### 9.1 VS Code拡張
```json
{
    "cm.lint.enable": true,
    "cm.lint.onSave": true,
    "cm.format.enable": true,
    "cm.format.onSave": true
}
```

### 9.2 Language Server Protocol
- 診断情報のリアルタイム提供
- コード補完
- ホバー情報
- リファクタリング支援

## 10. 関連ドキュメント

- [Trivia保持Lexer設計](./079_trivia_lexer_design.md)
- [LSP統合設計](./080_lsp_integration.md)
- [パフォーマンス最適化](./081_performance_optimization.md)