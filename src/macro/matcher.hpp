#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../lexer/token.hpp"
#include "token_tree.hpp"

namespace cm::macro {

// マッチした値の型
struct MatchedFragment {
    enum class Kind {
        TOKEN,      // 単一トークン
        TOKEN_SEQ,  // トークンのシーケンス
        REPETITION, // 繰り返しマッチ結果
    };

    Kind kind;
    std::variant<
        Token,                               // 単一トークン
        std::vector<Token>,                  // トークンシーケンス
        std::vector<MatchedFragment>        // 繰り返し結果
    > value;

    // コンストラクタ
    explicit MatchedFragment(Token token)
        : kind(Kind::TOKEN), value(std::move(token)) {}

    explicit MatchedFragment(std::vector<Token> tokens)
        : kind(Kind::TOKEN_SEQ), value(std::move(tokens)) {}

    explicit MatchedFragment(std::vector<MatchedFragment> repetitions)
        : kind(Kind::REPETITION), value(std::move(repetitions)) {}

    // ヘルパー関数
    bool is_token() const { return kind == Kind::TOKEN; }
    bool is_token_seq() const { return kind == Kind::TOKEN_SEQ; }
    bool is_repetition() const { return kind == Kind::REPETITION; }

    const Token* get_token() const {
        return is_token() ? std::get_if<Token>(&value) : nullptr;
    }

    const std::vector<Token>* get_token_seq() const {
        return is_token_seq() ? std::get_if<std::vector<Token>>(&value) : nullptr;
    }

    const std::vector<MatchedFragment>* get_repetition() const {
        return is_repetition() ? std::get_if<std::vector<MatchedFragment>>(&value) : nullptr;
    }
};

// マッチング結果を保持するバインディング
using MatchBindings = std::map<std::string, MatchedFragment>;

// マッチング結果
struct MatchResult {
    bool success;
    MatchBindings bindings;
    std::string error_message;

    static MatchResult Success(MatchBindings bindings) {
        return {true, std::move(bindings), ""};
    }

    static MatchResult Failure(const std::string& error) {
        return {false, {}, error};
    }
};

// マッチング状態（内部使用）
struct MatchState {
    MatchBindings bindings;
    std::vector<std::string> error_messages;
    size_t deepest_match_pos = 0;  // デバッグ用：最も深くマッチした位置
};

// マクロマッチャー
// パターンと入力トークンのマッチングを行う
class MacroMatcher {
public:
    MacroMatcher() = default;
    ~MacroMatcher() = default;

    // パターンと入力のマッチング
    MatchResult match(
        const std::vector<Token>& input,
        const MacroPattern& pattern
    );

    // トークンツリーと入力のマッチング（再帰用）
    MatchResult match_tree(
        const std::vector<Token>& input,
        const std::vector<TokenTree>& pattern
    );

private:
    // 再帰的マッチング
    bool match_recursive(
        const std::vector<Token>& input,
        const std::vector<TokenTree>& pattern,
        size_t input_pos,
        size_t pattern_pos,
        MatchState& state
    );

    // 単一トークンのマッチング
    bool match_token(
        const Token& input_token,
        const Token& pattern_token
    );

    // デリミタ付きトークン群のマッチング
    bool match_delimited(
        const std::vector<Token>& input,
        size_t& input_pos,
        const DelimitedTokens& pattern,
        MatchState& state
    );

    // メタ変数のマッチング
    bool match_metavar(
        const std::vector<Token>& input,
        size_t& input_pos,
        const MetaVariable& metavar,
        MatchState& state
    );

    // 繰り返しのマッチング
    bool match_repetition(
        const std::vector<Token>& input,
        size_t& input_pos,
        const RepetitionNode& repetition,
        MatchState& state
    );

    // フラグメント指定子に応じたマッチング
    std::optional<std::vector<Token>> match_fragment(
        const std::vector<Token>& input,
        size_t& input_pos,
        FragmentSpecifier spec
    );

    // 各フラグメント指定子のマッチング関数
    std::optional<std::vector<Token>> match_expr(
        const std::vector<Token>& input,
        size_t& input_pos
    );

    std::optional<std::vector<Token>> match_stmt(
        const std::vector<Token>& input,
        size_t& input_pos
    );

    std::optional<std::vector<Token>> match_type(
        const std::vector<Token>& input,
        size_t& input_pos
    );

    std::optional<std::vector<Token>> match_ident(
        const std::vector<Token>& input,
        size_t& input_pos
    );

    std::optional<std::vector<Token>> match_path(
        const std::vector<Token>& input,
        size_t& input_pos
    );

    std::optional<std::vector<Token>> match_literal(
        const std::vector<Token>& input,
        size_t& input_pos
    );

    std::optional<std::vector<Token>> match_block(
        const std::vector<Token>& input,
        size_t& input_pos
    );

    std::optional<std::vector<Token>> match_pattern(
        const std::vector<Token>& input,
        size_t& input_pos
    );

    std::optional<std::vector<Token>> match_item(
        const std::vector<Token>& input,
        size_t& input_pos
    );

    std::optional<std::vector<Token>> match_meta(
        const std::vector<Token>& input,
        size_t& input_pos
    );

    std::optional<std::vector<Token>> match_tt(
        const std::vector<Token>& input,
        size_t& input_pos
    );

    // ユーティリティ関数
    bool is_expr_start(const Token& token) const;
    bool is_stmt_start(const Token& token) const;
    bool is_type_start(const Token& token) const;
    bool is_pattern_start(const Token& token) const;

    // デリミタのバランスを確認
    size_t find_matching_delimiter(
        const std::vector<Token>& tokens,
        size_t start_pos
    ) const;

    // エラーメッセージ生成
    std::string generate_error(const MatchState& state) const;
};

}  // namespace cm::macro