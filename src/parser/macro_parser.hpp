#pragma once

#include "../lexer/token.hpp"
#include "../macro/token_tree.hpp"
#include "../utils/error.hpp"

#include <memory>
#include <string>
#include <vector>

namespace cm::parser {

// マクロパーサー
// macro_rules! の定義を解析してMacroDefinitionを生成
class MacroParser {
   public:
    MacroParser() = default;
    ~MacroParser() = default;

    // macro_rules! 定義全体を解析
    // 例: macro_rules! vec { ... }
    std::unique_ptr<macro::MacroDefinition> parse_macro_rules(std::vector<Token>& tokens,
                                                              size_t& pos);

   private:
    // マクロ名を解析
    std::string parse_macro_name(const std::vector<Token>& tokens, size_t& pos);

    // マクロルールセットを解析
    std::vector<macro::MacroRule> parse_macro_rules_body(const std::vector<Token>& tokens,
                                                         size_t& pos);

    // 単一のマクロルールを解析
    // 例: ($($x:expr),*) => { ... }
    macro::MacroRule parse_single_rule(const std::vector<Token>& tokens, size_t& pos);

    // マクロパターンを解析
    macro::MacroPattern parse_pattern(const std::vector<Token>& tokens, size_t& pos);

    // マクロトランスクライバーを解析
    macro::MacroTranscriber parse_transcriber(const std::vector<Token>& tokens, size_t& pos);

    // トークンツリーを解析
    macro::TokenTree parse_token_tree(const std::vector<Token>& tokens, size_t& pos);

    // デリミタ付きトークン群を解析
    std::unique_ptr<macro::DelimitedTokens> parse_delimited(const std::vector<Token>& tokens,
                                                            size_t& pos,
                                                            macro::DelimiterKind delimiter);

    // メタ変数を解析 ($name:spec)
    std::optional<macro::MetaVariable> parse_metavar(const std::vector<Token>& tokens, size_t& pos);

    // 繰り返しノードを解析 $(...)*
    std::unique_ptr<macro::RepetitionNode> parse_repetition(const std::vector<Token>& tokens,
                                                            size_t& pos);

    // フラグメント指定子を解析
    std::optional<macro::FragmentSpecifier> parse_fragment_spec(const std::vector<Token>& tokens,
                                                                size_t& pos);

    // ヘルパー関数
    bool is_delimiter_open(const Token& token) const;
    bool is_delimiter_close(const Token& token) const;
    macro::DelimiterKind get_delimiter_kind(const Token& token) const;
    Token get_matching_delimiter(macro::DelimiterKind kind) const;
    bool is_repetition_op(const Token& token) const;
    macro::RepetitionOp get_repetition_op(const Token& token) const;

    // エラー処理
    void expect_token(const std::vector<Token>& tokens, size_t& pos, TokenType expected,
                      const std::string& message);

    void error(const std::string& message, const SourceLocation& location);
};

}  // namespace cm::parser