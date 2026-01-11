#include "macro_parser.hpp"

#include <stdexcept>
#include <fmt/format.h>

namespace cm::parser {

// macro_rules! 定義全体を解析
std::unique_ptr<macro::MacroDefinition> MacroParser::parse_macro_rules(
    std::vector<Token>& tokens,
    size_t& pos
) {
    if (pos >= tokens.size() || tokens[pos].value != "macro_rules") {
        error("Expected 'macro_rules'", tokens[pos].location);
        return nullptr;
    }

    SourceLocation def_location = tokens[pos].location;
    pos++;  // macro_rules をスキップ

    // ! を期待
    if (pos >= tokens.size() || tokens[pos].value != "!") {
        error("Expected '!' after macro_rules", tokens[pos].location);
        return nullptr;
    }
    pos++;  // ! をスキップ

    // マクロ名を解析
    std::string name = parse_macro_name(tokens, pos);

    // { を期待
    if (pos >= tokens.size() || tokens[pos].value != "{") {
        error("Expected '{' after macro name", tokens[pos].location);
        return nullptr;
    }
    pos++;  // { をスキップ

    // マクロルールセットを解析
    std::vector<macro::MacroRule> rules = parse_macro_rules_body(tokens, pos);

    // } を期待
    if (pos >= tokens.size() || tokens[pos].value != "}") {
        error("Expected '}' to close macro definition", tokens[pos].location);
        return nullptr;
    }
    pos++;  // } をスキップ

    auto definition = std::make_unique<macro::MacroDefinition>();
    definition->name = name;
    definition->rules = std::move(rules);
    definition->location = def_location;

    return definition;
}

// マクロ名を解析
std::string MacroParser::parse_macro_name(
    const std::vector<Token>& tokens,
    size_t& pos
) {
    if (pos >= tokens.size() || tokens[pos].type != TokenType::Identifier) {
        error("Expected macro name", tokens[pos].location);
        return "";
    }

    std::string name = tokens[pos].value;
    pos++;
    return name;
}

// マクロルールセットを解析
std::vector<macro::MacroRule> MacroParser::parse_macro_rules_body(
    const std::vector<Token>& tokens,
    size_t& pos
) {
    std::vector<macro::MacroRule> rules;

    while (pos < tokens.size() && tokens[pos].value != "}") {
        // 単一のルールを解析
        rules.push_back(parse_single_rule(tokens, pos));

        // セミコロンがあればスキップ（最後のルールには不要）
        if (pos < tokens.size() && tokens[pos].value == ";") {
            pos++;
        }
    }

    return rules;
}

// 単一のマクロルールを解析
macro::MacroRule MacroParser::parse_single_rule(
    const std::vector<Token>& tokens,
    size_t& pos
) {
    macro::MacroRule rule;

    // パターンを解析
    rule.pattern = parse_pattern(tokens, pos);

    // => を期待
    if (pos >= tokens.size() || tokens[pos].value != "=>") {
        error("Expected '=>' in macro rule", tokens[pos].location);
    }
    pos++;  // => をスキップ

    // トランスクライバーを解析
    rule.transcriber = parse_transcriber(tokens, pos);

    return rule;
}

// マクロパターンを解析
macro::MacroPattern MacroParser::parse_pattern(
    const std::vector<Token>& tokens,
    size_t& pos
) {
    macro::MacroPattern pattern;

    // パターンは必ず括弧で囲まれている必要がある
    if (!is_delimiter_open(tokens[pos])) {
        error("Macro pattern must be delimited", tokens[pos].location);
        return pattern;
    }

    // デリミタ付きトークン群として解析
    auto tree = parse_token_tree(tokens, pos);
    if (tree.is_delimited()) {
        if (auto* delimited = tree.get_delimited()) {
            pattern.tokens = delimited->tokens;
        }
    }

    return pattern;
}

// マクロトランスクライバーを解析
macro::MacroTranscriber MacroParser::parse_transcriber(
    const std::vector<Token>& tokens,
    size_t& pos
) {
    macro::MacroTranscriber transcriber;

    // トランスクライバーも必ず括弧で囲まれている必要がある
    if (!is_delimiter_open(tokens[pos])) {
        error("Macro transcriber must be delimited", tokens[pos].location);
        return transcriber;
    }

    // デリミタ付きトークン群として解析
    auto tree = parse_token_tree(tokens, pos);
    if (tree.is_delimited()) {
        if (auto* delimited = tree.get_delimited()) {
            transcriber.tokens = delimited->tokens;
        }
    }

    return transcriber;
}

// トークンツリーを解析
macro::TokenTree MacroParser::parse_token_tree(
    const std::vector<Token>& tokens,
    size_t& pos
) {
    if (pos >= tokens.size()) {
        error("Unexpected end of tokens", tokens[pos - 1].location);
        return macro::TokenTree(Token{});
    }

    const Token& token = tokens[pos];

    // $ で始まる場合はメタ変数または繰り返し
    if (token.value == "$") {
        pos++;  // $ をスキップ

        // $(...) の繰り返しパターン
        if (pos < tokens.size() && is_delimiter_open(tokens[pos])) {
            auto repetition = parse_repetition(tokens, pos);
            return macro::TokenTree(std::move(repetition));
        }
        // $name:spec のメタ変数
        else {
            auto metavar_opt = parse_metavar(tokens, pos);
            if (metavar_opt) {
                return macro::TokenTree(*metavar_opt);
            } else {
                error("Invalid metavariable", tokens[pos].location);
                return macro::TokenTree(Token{});
            }
        }
    }
    // デリミタで始まる場合はデリミタ付きトークン群
    else if (is_delimiter_open(token)) {
        auto delimiter_kind = get_delimiter_kind(token);
        auto delimited = parse_delimited(tokens, pos, delimiter_kind);
        return macro::TokenTree(std::move(delimited));
    }
    // それ以外は単一トークン
    else {
        Token single_token = token;
        pos++;
        return macro::TokenTree(single_token);
    }
}

// デリミタ付きトークン群を解析
std::unique_ptr<macro::DelimitedTokens> MacroParser::parse_delimited(
    const std::vector<Token>& tokens,
    size_t& pos,
    macro::DelimiterKind delimiter
) {
    auto delimited = std::make_unique<macro::DelimitedTokens>();
    delimited->delimiter = delimiter;

    pos++;  // 開き括弧をスキップ

    // 対応する閉じ括弧まで解析
    int depth = 1;
    while (pos < tokens.size() && depth > 0) {
        if (is_delimiter_open(tokens[pos]) &&
            get_delimiter_kind(tokens[pos]) == delimiter) {
            depth++;
        } else if (is_delimiter_close(tokens[pos]) &&
                   get_delimiter_kind(tokens[pos]) == delimiter) {
            depth--;
            if (depth == 0) {
                break;  // 対応する閉じ括弧を見つけた
            }
        }

        // 内部のトークンツリーを解析
        delimited->tokens.push_back(parse_token_tree(tokens, pos));
    }

    if (depth != 0) {
        error("Unmatched delimiter", tokens[pos].location);
    }

    pos++;  // 閉じ括弧をスキップ

    return delimited;
}

// メタ変数を解析 ($name:spec)
// 注：この関数は $ がすでにスキップされた状態で呼ばれる
std::optional<macro::MetaVariable> MacroParser::parse_metavar(
    const std::vector<Token>& tokens,
    size_t& pos
) {
    if (pos >= tokens.size() || tokens[pos].type != TokenType::Identifier) {
        return std::nullopt;
    }

    macro::MetaVariable metavar;
    metavar.name = tokens[pos].value;
    pos++;

    // : を期待
    if (pos >= tokens.size() || tokens[pos].value != ":") {
        return std::nullopt;
    }
    pos++;

    // フラグメント指定子を解析
    auto spec_opt = parse_fragment_spec(tokens, pos);
    if (!spec_opt) {
        return std::nullopt;
    }

    metavar.specifier = *spec_opt;
    return metavar;
}

// 繰り返しノードを解析 $(...)*
// 注：この関数は $ がすでにスキップされた状態で呼ばれる
std::unique_ptr<macro::RepetitionNode> MacroParser::parse_repetition(
    const std::vector<Token>& tokens,
    size_t& pos
) {
    auto repetition = std::make_unique<macro::RepetitionNode>();

    // 括弧で囲まれたパターンを解析
    if (!is_delimiter_open(tokens[pos])) {
        error("Expected delimiter after $ in repetition", tokens[pos].location);
        return repetition;
    }

    auto delimiter_kind = get_delimiter_kind(tokens[pos]);
    auto delimited = parse_delimited(tokens, pos, delimiter_kind);

    // パターンをコピー
    repetition->pattern = delimited->tokens;

    // セパレータがあるかチェック（例：,）
    if (pos < tokens.size() && !is_repetition_op(tokens[pos])) {
        repetition->separator = tokens[pos];
        pos++;
    }

    // 繰り返し演算子を解析（*, +, ?）
    if (pos >= tokens.size() || !is_repetition_op(tokens[pos])) {
        error("Expected repetition operator (*, +, ?)", tokens[pos].location);
        return repetition;
    }

    repetition->op = get_repetition_op(tokens[pos]);
    pos++;

    return repetition;
}

// フラグメント指定子を解析
std::optional<macro::FragmentSpecifier> MacroParser::parse_fragment_spec(
    const std::vector<Token>& tokens,
    size_t& pos
) {
    if (pos >= tokens.size() || tokens[pos].type != TokenType::Identifier) {
        return std::nullopt;
    }

    auto spec = macro::string_to_fragment_spec(tokens[pos].value);
    if (spec) {
        pos++;
    }
    return spec;
}

// ヘルパー関数
bool MacroParser::is_delimiter_open(const Token& token) const {
    return token.value == "(" || token.value == "[" || token.value == "{";
}

bool MacroParser::is_delimiter_close(const Token& token) const {
    return token.value == ")" || token.value == "]" || token.value == "}";
}

macro::DelimiterKind MacroParser::get_delimiter_kind(const Token& token) const {
    if (token.value == "(" || token.value == ")") {
        return macro::DelimiterKind::PAREN;
    }
    if (token.value == "[" || token.value == "]") {
        return macro::DelimiterKind::BRACKET;
    }
    if (token.value == "{" || token.value == "}") {
        return macro::DelimiterKind::BRACE;
    }
    // デフォルト（エラー時）
    return macro::DelimiterKind::PAREN;
}

Token MacroParser::get_matching_delimiter(macro::DelimiterKind kind) const {
    switch (kind) {
        case macro::DelimiterKind::PAREN:
            return Token{TokenType::Symbol, ")", SourceLocation{}};
        case macro::DelimiterKind::BRACKET:
            return Token{TokenType::Symbol, "]", SourceLocation{}};
        case macro::DelimiterKind::BRACE:
            return Token{TokenType::Symbol, "}", SourceLocation{}};
        default:
            return Token{};
    }
}

bool MacroParser::is_repetition_op(const Token& token) const {
    return token.value == "*" || token.value == "+" || token.value == "?";
}

macro::RepetitionOp MacroParser::get_repetition_op(const Token& token) const {
    if (token.value == "*") return macro::RepetitionOp::ZERO_OR_MORE;
    if (token.value == "+") return macro::RepetitionOp::ONE_OR_MORE;
    if (token.value == "?") return macro::RepetitionOp::ZERO_OR_ONE;
    // デフォルト（エラー時）
    return macro::RepetitionOp::ZERO_OR_MORE;
}

// エラー処理
void MacroParser::expect_token(
    const std::vector<Token>& tokens,
    size_t& pos,
    TokenType expected,
    const std::string& message
) {
    if (pos >= tokens.size() || tokens[pos].type != expected) {
        error(message, tokens[pos].location);
    }
    pos++;
}

void MacroParser::error(
    const std::string& message,
    const SourceLocation& location
) {
    throw std::runtime_error(fmt::format(
        "[MACRO_PARSER] ERROR at {}:{}: {}",
        location.line,
        location.column,
        message
    ));
}

}  // namespace cm::parser