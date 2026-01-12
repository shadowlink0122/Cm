# BNF形式パーサーリファクタリング設計書

作成日: 2026-01-11
対象バージョン: v0.11.0
ステータス: 設計提案

## エグゼクティブサマリー

現在のCm言語パーサーを**BNF（Backus-Naur Form）形式**で定義し、文法の明確化とパーサーの保守性向上を実現します。既存の実装を活用しながら、段階的にBNFベースの構造へ移行します。

## 1. 現状の課題

### 1.1 現在のパーサー実装の問題点

1. **文法定義が暗黙的**: パーサーコードから文法を推測する必要がある
2. **構文の一貫性欠如**: 同じ構造でも異なる実装パターン
3. **拡張性の問題**: 新構文追加時に影響範囲が不明確
4. **ドキュメント不足**: 正式な文法仕様書が存在しない

### 1.2 現在のパーサー構造

```cpp
// 現在: 手書きの再帰下降パーサー
class Parser {
    std::shared_ptr<ASTNode> parse_statement() {
        if (current_token == TokenType::IF) {
            return parse_if_statement();
        } else if (current_token == TokenType::WHILE) {
            return parse_while_statement();
        }
        // ... 多数の分岐
    }
};
```

## 2. BNF文法定義

### 2.1 Cm言語の完全BNF定義

```bnf
// ============================================
// Cm Language Grammar in EBNF
// ============================================

// ------------------
// Program Structure
// ------------------
program ::= top_level_decl*

top_level_decl ::= function_decl
                 | struct_decl
                 | trait_decl
                 | impl_decl
                 | typedef_decl
                 | macro_decl
                 | import_statement

// ------------------
// Declarations
// ------------------
function_decl ::= generic_params? type identifier '(' param_list? ')'
                  where_clause? block

struct_decl ::= 'struct' identifier generic_params?
                where_clause? '{' struct_member* '}'

trait_decl ::= 'trait' identifier generic_params?
               '{' trait_member* '}'

impl_decl ::= 'impl' generic_params? identifier 'for' type
              where_clause? '{' impl_member* '}'

typedef_decl ::= 'typedef' identifier '=' type ';'

macro_decl ::= 'macro' identifier generic_params? '(' param_list? ')'
               where_clause? block

// ------------------
// Generic Parameters
// ------------------
generic_params ::= '<' generic_param_list '>'

generic_param_list ::= generic_param (',' generic_param)*

generic_param ::= type_param
                | const_param

type_param ::= identifier (':' type_constraint)? ('=' type)?

const_param ::= identifier ':' 'const' type ('=' const_expr)?

type_constraint ::= union_type         // int | double | float
                  | identifier          // Interface name

// ------------------
// Where Clause (ブロック直前)
// ------------------
where_clause ::= 'where' where_bound (',' where_bound)*

where_bound ::= identifier ':' interface_bound

interface_bound ::= identifier ('+' identifier)*

// ------------------
// Types
// ------------------
type ::= primitive_type
       | identifier                    // User-defined type
       | type '[' const_expr ']'      // Array
       | type '*'                      // Pointer
       | type '&'                      // Reference
       | identifier '<' type_list '>' // Generic instantiation
       | union_type                    // int | double

primitive_type ::= 'int' | 'double' | 'float' | 'bool' | 'char'
                 | 'void' | 'string' | 'size_t'
                 | 'tiny' | 'short' | 'long'
                 | 'uint' | 'utiny' | 'ushort' | 'ulong'

union_type ::= type ('|' type)+

// ------------------
// Statements
// ------------------
statement ::= expr_statement
            | var_decl_statement
            | if_statement
            | while_statement
            | for_statement
            | return_statement
            | break_statement
            | continue_statement
            | block

expr_statement ::= expression ';'

var_decl_statement ::= type identifier ('=' expression)? ';'

if_statement ::= 'if' '(' expression ')' statement
                 ('else' statement)?

while_statement ::= 'while' '(' expression ')' statement

for_statement ::= 'for' '(' for_init? ';' expression? ';' expression? ')'
                  statement
                | 'for' '(' type identifier ':' expression ')' statement

return_statement ::= 'return' expression? ';'

break_statement ::= 'break' ';'

continue_statement ::= 'continue' ';'

block ::= '{' statement* '}'

// ------------------
// Expressions
// ------------------
expression ::= assignment_expr

assignment_expr ::= logical_or_expr
                   | logical_or_expr assign_op assignment_expr

assign_op ::= '=' | '+=' | '-=' | '*=' | '/=' | '%='
            | '&=' | '|=' | '^=' | '<<=' | '>>='

logical_or_expr ::= logical_and_expr
                   | logical_or_expr '||' logical_and_expr

logical_and_expr ::= bitwise_or_expr
                    | logical_and_expr '&&' bitwise_or_expr

bitwise_or_expr ::= bitwise_xor_expr
                   | bitwise_or_expr '|' bitwise_xor_expr

bitwise_xor_expr ::= bitwise_and_expr
                    | bitwise_xor_expr '^' bitwise_and_expr

bitwise_and_expr ::= equality_expr
                    | bitwise_and_expr '&' equality_expr

equality_expr ::= relational_expr
                 | equality_expr ('==' | '!=') relational_expr

relational_expr ::= shift_expr
                   | relational_expr ('<' | '>' | '<=' | '>=') shift_expr

shift_expr ::= additive_expr
              | shift_expr ('<<' | '>>') additive_expr

additive_expr ::= multiplicative_expr
                 | additive_expr ('+' | '-') multiplicative_expr

multiplicative_expr ::= unary_expr
                       | multiplicative_expr ('*' | '/' | '%') unary_expr

unary_expr ::= postfix_expr
             | unary_op unary_expr
             | 'sizeof' '(' type ')'
             | 'typeof' '(' expression ')'

unary_op ::= '+' | '-' | '!' | '~' | '&' | '*' | '++' | '--'

postfix_expr ::= primary_expr
                | postfix_expr '[' expression ']'      // Array index
                | postfix_expr '(' argument_list? ')'  // Function call
                | postfix_expr '.' identifier          // Member access
                | postfix_expr '->' identifier         // Pointer member
                | postfix_expr '++'                    // Post-increment
                | postfix_expr '--'                    // Post-decrement

primary_expr ::= identifier
               | literal
               | '(' expression ')'
               | 'new' type '(' argument_list? ')'
               | 'delete' expression

literal ::= integer_literal
          | float_literal
          | char_literal
          | string_literal
          | bool_literal

// ------------------
// Parameters and Arguments
// ------------------
param_list ::= parameter (',' parameter)*

parameter ::= type identifier ('=' const_expr)?
            | type '...' identifier    // Variadic

argument_list ::= expression (',' expression)*

// ------------------
// Lexical Elements
// ------------------
identifier ::= [a-zA-Z_][a-zA-Z0-9_]*

integer_literal ::= [0-9]+
                   | '0x' [0-9a-fA-F]+
                   | '0b' [01]+

float_literal ::= [0-9]+ '.' [0-9]+ ([eE] [+-]? [0-9]+)?

char_literal ::= "'" . "'"

string_literal ::= '"' .* '"'

bool_literal ::= 'true' | 'false'
```

## 3. BNFベースパーサーの実装設計

### 3.1 パーサージェネレーター風のアプローチ

```cpp
// BNF定義を使用したパーサー基底クラス
class BNFParser {
protected:
    // BNFルールを関数ポインタにマップ
    using ParseFunc = std::function<ASTNodePtr()>;
    std::map<std::string, ParseFunc> rules;

    // ルール登録
    void define_rule(const std::string& name, ParseFunc func) {
        rules[name] = func;
    }

    // ルール実行
    ASTNodePtr parse_rule(const std::string& name) {
        if (auto it = rules.find(name); it != rules.end()) {
            return it->second();
        }
        error("Unknown rule: " + name);
    }

public:
    BNFParser() {
        initialize_rules();
    }

    virtual void initialize_rules() = 0;
};
```

### 3.2 Cmパーサーの実装（BNF準拠）

```cpp
class CmBNFParser : public BNFParser {
public:
    void initialize_rules() override {
        // プログラム構造
        define_rule("program", [this] {
            return parse_program();
        });

        // 宣言
        define_rule("function_decl", [this] {
            return parse_function_decl();
        });

        define_rule("struct_decl", [this] {
            return parse_struct_decl();
        });

        // ジェネリクス
        define_rule("generic_params", [this] {
            return parse_generic_params();
        });

        define_rule("where_clause", [this] {
            return parse_where_clause();
        });

        // 型
        define_rule("type", [this] {
            return parse_type();
        });

        define_rule("union_type", [this] {
            return parse_union_type();
        });

        // 文
        define_rule("statement", [this] {
            return parse_statement();
        });

        // 式
        define_rule("expression", [this] {
            return parse_expression();
        });

        // ... すべてのルールを定義
    }

private:
    // 各ルールの実装（既存コードを活用）
    ASTNodePtr parse_program() {
        auto program = std::make_shared<ProgramNode>();
        while (!is_eof()) {
            program->add(parse_rule("top_level_decl"));
        }
        return program;
    }

    ASTNodePtr parse_function_decl() {
        // 既存のparse_function_decl_v2()を活用
        auto generics = maybe_parse("generic_params");
        auto return_type = parse_rule("type");
        auto name = expect_identifier();
        expect(TokenType::LPAREN);
        auto params = maybe_parse("param_list");
        expect(TokenType::RPAREN);
        auto where_clause = maybe_parse("where_clause");
        auto body = parse_rule("block");

        return std::make_shared<FunctionDecl>(
            name, generics, params, return_type, where_clause, body
        );
    }

    // where句のパース（新規）
    ASTNodePtr parse_where_clause() {
        if (!accept_keyword("where")) return nullptr;

        auto clause = std::make_shared<WhereClause>();
        do {
            auto param = expect_identifier();
            expect(TokenType::COLON);
            auto bounds = parse_interface_bounds();
            clause->add_bound(param, bounds);
        } while (accept(TokenType::COMMA));

        return clause;
    }

    // ユニオン型のパース（新規）
    ASTNodePtr parse_union_type() {
        auto first_type = parse_primary_type();
        if (!check(TokenType::PIPE)) {
            return first_type;
        }

        auto union_type = std::make_shared<UnionType>();
        union_type->add(first_type);

        while (accept(TokenType::PIPE)) {
            union_type->add(parse_primary_type());
        }

        return union_type;
    }
};
```

### 3.3 BNFコンビネーター（高度な実装）

```cpp
// BNFコンビネーターライブラリ
namespace BNF {
    // 基本コンビネーター
    template<typename T>
    class Rule {
    public:
        virtual std::optional<T> parse(TokenStream& tokens) = 0;
    };

    // 連結: A B
    template<typename A, typename B>
    class Sequence : public Rule<std::pair<A, B>> {
        Rule<A>* rule_a;
        Rule<B>* rule_b;

    public:
        std::optional<std::pair<A, B>> parse(TokenStream& tokens) override {
            auto a = rule_a->parse(tokens);
            if (!a) return std::nullopt;

            auto b = rule_b->parse(tokens);
            if (!b) {
                tokens.rollback();
                return std::nullopt;
            }

            return std::make_pair(*a, *b);
        }
    };

    // 選択: A | B
    template<typename T>
    class Choice : public Rule<T> {
        std::vector<Rule<T>*> alternatives;

    public:
        std::optional<T> parse(TokenStream& tokens) override {
            for (auto* alt : alternatives) {
                if (auto result = alt->parse(tokens)) {
                    return result;
                }
            }
            return std::nullopt;
        }
    };

    // 繰り返し: A*
    template<typename T>
    class Kleene : public Rule<std::vector<T>> {
        Rule<T>* rule;

    public:
        std::optional<std::vector<T>> parse(TokenStream& tokens) override {
            std::vector<T> results;
            while (auto result = rule->parse(tokens)) {
                results.push_back(*result);
            }
            return results;  // 0回でも成功
        }
    };

    // オプション: A?
    template<typename T>
    class Optional : public Rule<std::optional<T>> {
        Rule<T>* rule;

    public:
        std::optional<std::optional<T>> parse(TokenStream& tokens) override {
            return std::optional<std::optional<T>>(rule->parse(tokens));
        }
    };
}

// 使用例：Cmパーサーの定義
class CmGrammar {
public:
    // 型パラメータ: T ':' type_constraint? ('=' type)?
    auto type_param() {
        return BNF::Sequence(
            identifier(),
            BNF::Optional(
                BNF::Sequence(
                    token(TokenType::COLON),
                    type_constraint()
                )
            ),
            BNF::Optional(
                BNF::Sequence(
                    token(TokenType::ASSIGN),
                    type()
                )
            )
        );
    }

    // where句: 'where' where_bound (',' where_bound)*
    auto where_clause() {
        return BNF::Sequence(
            keyword("where"),
            BNF::Sequence(
                where_bound(),
                BNF::Kleene(
                    BNF::Sequence(
                        token(TokenType::COMMA),
                        where_bound()
                    )
                )
            )
        );
    }
};
```

## 4. 既存実装との統合

### 4.1 段階的移行計画

#### Phase 1: BNF文法の文書化（1週間）
- 現在のパーサーから完全なBNF文法を抽出
- CANONICAL_SPEC.mdとして正式化
- 曖昧性の検出と解決

#### Phase 2: ルールベース実装の追加（2週間）
- BNFParserクラスの実装
- 既存の関数をルールとして登録
- 新機能はBNFルールとして追加

#### Phase 3: パーサーコンビネーター導入（3週間）
- オプション: 高度な実装を希望する場合
- パフォーマンス最適化
- エラーレポートの改善

### 4.2 既存コードの活用

```cpp
// 既存の実装をBNFルールとしてラップ
class CmParserAdapter : public CmBNFParser {
private:
    // 既存のParser インスタンス
    Parser legacy_parser;

public:
    void initialize_rules() override {
        // 既存の関数をそのまま使用
        define_rule("expression", [this] {
            return legacy_parser.parse_expression();
        });

        // 新機能はBNF準拠で実装
        define_rule("where_clause", [this] {
            return parse_where_clause_bnf();
        });

        // 徐々に置き換え
        define_rule("generic_params", [this] {
            // 新しいBNF準拠の実装
            if (!accept(TokenType::LESS)) return nullptr;

            auto params = parse_rule("generic_param_list");

            expect(TokenType::GREATER);
            return params;
        });
    }
};
```

## 5. BNF検証ツール

### 5.1 文法検証器

```cpp
class GrammarValidator {
public:
    struct ValidationResult {
        bool is_valid;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
    };

    ValidationResult validate(const BNFGrammar& grammar) {
        ValidationResult result{true, {}, {}};

        // 左再帰の検出
        detect_left_recursion(grammar, result);

        // 到達不可能なルール
        find_unreachable_rules(grammar, result);

        // 曖昧性の検出
        detect_ambiguity(grammar, result);

        // FIRST/FOLLOW集合の計算
        compute_first_follow(grammar, result);

        return result;
    }

private:
    void detect_left_recursion(const BNFGrammar& grammar,
                              ValidationResult& result) {
        // A ::= A α のパターンを検出
        for (const auto& rule : grammar.rules) {
            if (is_left_recursive(rule)) {
                result.errors.push_back(
                    "Left recursion in rule: " + rule.name
                );
                result.is_valid = false;
            }
        }
    }
};
```

### 5.2 テストフレームワーク

```cpp
// BNFルールのユニットテスト
class BNFParserTest {
public:
    void test_generic_params() {
        auto parser = create_parser("<T: int | double, N: const int>");

        auto result = parser.parse_rule("generic_params");

        ASSERT_NOT_NULL(result);
        ASSERT_EQ(result->params.size(), 2);
        ASSERT_EQ(result->params[0]->name, "T");
        ASSERT_TRUE(result->params[0]->has_type_constraint());
    }

    void test_where_clause() {
        auto parser = create_parser("where T: Comparable + Clone");

        auto result = parser.parse_rule("where_clause");

        ASSERT_NOT_NULL(result);
        ASSERT_EQ(result->bounds.size(), 1);
        ASSERT_EQ(result->bounds[0].param_name, "T");
        ASSERT_EQ(result->bounds[0].interfaces.size(), 2);
    }
};
```

## 6. エラーレポートの改善

### 6.1 BNFベースのエラーメッセージ

```cpp
class BNFErrorReporter {
    std::string current_rule;
    std::vector<std::string> rule_stack;

public:
    void enter_rule(const std::string& rule) {
        rule_stack.push_back(rule);
        current_rule = rule;
    }

    void exit_rule() {
        if (!rule_stack.empty()) {
            rule_stack.pop_back();
            current_rule = rule_stack.empty() ? "" : rule_stack.back();
        }
    }

    void report_error(const Token& token, const std::string& expected) {
        std::cerr << "Parse error at " << token.location << "\n";
        std::cerr << "  In rule: " << current_rule << "\n";
        std::cerr << "  Expected: " << expected << "\n";
        std::cerr << "  Found: " << token.text << "\n";

        // ルールスタックを表示
        std::cerr << "  Rule stack:\n";
        for (const auto& rule : rule_stack) {
            std::cerr << "    - " << rule << "\n";
        }
    }
};
```

### 6.2 期待される構文の提案

```
error: unexpected token in generic parameter
  --> main.cm:10:15
   |
10 | <T: const int>
   |     ^^^^^ expected type constraint or '>''
   |
   = note: parsing rule 'generic_param'
   = help: valid syntax:
           <T>                    // unconstrained
           <T: int | double>      // type constraint
           <N: const int>         // const parameter
           <T = DefaultType>      // with default
```

## 7. パフォーマンス考慮事項

### 7.1 最適化手法

1. **メモ化**: 同じルールの再パースを避ける
2. **先読みテーブル**: FIRST/FOLLOW集合の事前計算
3. **インライン展開**: 頻繁に使用される単純なルール

```cpp
class OptimizedBNFParser {
    // メモ化キャッシュ
    std::map<std::pair<std::string, size_t>, ASTNodePtr> memo_cache;

    // 先読みテーブル
    std::map<std::string, std::set<TokenType>> first_sets;
    std::map<std::string, std::set<TokenType>> follow_sets;

public:
    ASTNodePtr parse_rule_memoized(const std::string& rule) {
        auto key = std::make_pair(rule, current_position());

        if (auto it = memo_cache.find(key); it != memo_cache.end()) {
            return it->second;
        }

        auto result = parse_rule(rule);
        memo_cache[key] = result;
        return result;
    }
};
```

## 8. ツール生成

### 8.1 パーサージェネレーター

```python
# bnf2cpp.py - BNFからC++パーサーを生成
def generate_parser(bnf_file, output_file):
    grammar = parse_bnf(bnf_file)

    with open(output_file, 'w') as f:
        f.write(generate_header(grammar))

        for rule in grammar.rules:
            f.write(generate_parse_function(rule))

        f.write(generate_footer())

def generate_parse_function(rule):
    return f"""
    ASTNodePtr parse_{rule.name}() {
        // Generated from: {rule.definition}
        {generate_parsing_code(rule)}
    }
    """
```

### 8.2 ドキュメント生成

```cpp
// BNFから自動的にドキュメントを生成
class DocumentationGenerator {
public:
    void generate_syntax_reference(const BNFGrammar& grammar,
                                  const std::string& output_file) {
        std::ofstream doc(output_file);

        doc << "# Cm Language Syntax Reference\n\n";
        doc << "Generated from BNF grammar\n\n";

        for (const auto& rule : grammar.rules) {
            doc << "## " << rule.name << "\n\n";
            doc << "```bnf\n";
            doc << rule.name << " ::= " << rule.definition << "\n";
            doc << "```\n\n";

            if (!rule.examples.empty()) {
                doc << "Examples:\n";
                for (const auto& example : rule.examples) {
                    doc << "```cm\n" << example << "\n```\n";
                }
            }
            doc << "\n";
        }
    }
};
```

## 9. 利点

### 9.1 明確な文法定義
- 正式なBNF仕様書
- 曖昧性の排除
- 一貫した構文

### 9.2 保守性の向上
- 新機能追加が容易
- 影響範囲が明確
- テストが書きやすい

### 9.3 ツールサポート
- 構文ハイライトの自動生成
- IDEサポートの改善
- 静的解析ツールの作成が容易

## 10. まとめ

このBNFベースのリファクタリングにより：

✅ **正式な文法定義の確立**
✅ **パーサーの保守性向上**
✅ **既存実装の段階的移行**
✅ **エラーレポートの改善**
✅ **ツール生成の自動化**

既存の実装を活用しながら、より構造化されたパーサーアーキテクチャへ移行できます。

---

**作成者:** Claude Code
**ステータス:** 設計提案
**次ステップ:** BNF文法の正式化から開始