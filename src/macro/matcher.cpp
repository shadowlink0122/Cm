#include "matcher.hpp"

#include <fmt/format.h>
#include <sstream>

namespace cm::macro {

// パターンと入力のマッチング
MatchResult MacroMatcher::match(
    const std::vector<Token>& input,
    const MacroPattern& pattern
) {
    return match_tree(input, pattern.tokens);
}

// トークンツリーと入力のマッチング
MatchResult MacroMatcher::match_tree(
    const std::vector<Token>& input,
    const std::vector<TokenTree>& pattern
) {
    MatchState state;

    if (match_recursive(input, pattern, 0, 0, state)) {
        // 入力を完全に消費したか確認
        if (state.deepest_match_pos == input.size()) {
            return MatchResult::Success(std::move(state.bindings));
        } else {
            return MatchResult::Failure(fmt::format(
                "Unexpected tokens after position {}",
                state.deepest_match_pos
            ));
        }
    }

    return MatchResult::Failure(generate_error(state));
}

// 再帰的マッチング
bool MacroMatcher::match_recursive(
    const std::vector<Token>& input,
    const std::vector<TokenTree>& pattern,
    size_t input_pos,
    size_t pattern_pos,
    MatchState& state
) {
    // 最深マッチ位置を更新
    if (input_pos > state.deepest_match_pos) {
        state.deepest_match_pos = input_pos;
    }

    // パターン終了
    if (pattern_pos >= pattern.size()) {
        // 入力も終了していれば成功
        return input_pos >= input.size();
    }

    // 入力終了（パターンはまだある）
    if (input_pos >= input.size()) {
        // 残りのパターンがすべてオプション（*や?）なら成功の可能性
        for (size_t i = pattern_pos; i < pattern.size(); ++i) {
            if (!pattern[i].is_repetition()) {
                return false;
            }
            auto* rep = pattern[i].get_repetition();
            if (rep && rep->op != RepetitionOp::ZERO_OR_MORE &&
                rep->op != RepetitionOp::ZERO_OR_ONE) {
                return false;
            }
        }
        return true;
    }

    const TokenTree& current_pattern = pattern[pattern_pos];

    // トークンのマッチング
    if (current_pattern.is_token()) {
        auto* pattern_token = current_pattern.get_token();
        if (pattern_token && input_pos < input.size()) {
            if (match_token(input[input_pos], *pattern_token)) {
                return match_recursive(input, pattern,
                                     input_pos + 1, pattern_pos + 1, state);
            }
        }
        return false;
    }

    // デリミタ付きトークン群のマッチング
    if (current_pattern.is_delimited()) {
        auto* delimited = current_pattern.get_delimited();
        if (delimited) {
            size_t new_input_pos = input_pos;
            if (match_delimited(input, new_input_pos, *delimited, state)) {
                return match_recursive(input, pattern,
                                     new_input_pos, pattern_pos + 1, state);
            }
        }
        return false;
    }

    // メタ変数のマッチング
    if (current_pattern.is_metavar()) {
        auto* metavar = current_pattern.get_metavar();
        if (metavar) {
            size_t new_input_pos = input_pos;
            if (match_metavar(input, new_input_pos, *metavar, state)) {
                return match_recursive(input, pattern,
                                     new_input_pos, pattern_pos + 1, state);
            }
        }
        return false;
    }

    // 繰り返しのマッチング
    if (current_pattern.is_repetition()) {
        auto* repetition = current_pattern.get_repetition();
        if (repetition) {
            size_t new_input_pos = input_pos;
            if (match_repetition(input, new_input_pos, *repetition, state)) {
                return match_recursive(input, pattern,
                                     new_input_pos, pattern_pos + 1, state);
            }
        }
        return false;
    }

    return false;
}

// 単一トークンのマッチング
bool MacroMatcher::match_token(
    const Token& input_token,
    const Token& pattern_token
) {
    // トークンの型と値が一致すればマッチ
    return input_token.type == pattern_token.type &&
           input_token.value == pattern_token.value;
}

// デリミタ付きトークン群のマッチング
bool MacroMatcher::match_delimited(
    const std::vector<Token>& input,
    size_t& input_pos,
    const DelimitedTokens& pattern,
    MatchState& state
) {
    if (input_pos >= input.size()) {
        return false;
    }

    // 開きデリミタの確認
    const Token& open_token = input[input_pos];
    std::string expected_open;
    std::string expected_close;

    switch (pattern.delimiter) {
        case DelimiterKind::PAREN:
            expected_open = "(";
            expected_close = ")";
            break;
        case DelimiterKind::BRACKET:
            expected_open = "[";
            expected_close = "]";
            break;
        case DelimiterKind::BRACE:
            expected_open = "{";
            expected_close = "}";
            break;
    }

    if (open_token.value != expected_open) {
        return false;
    }

    // 対応する閉じデリミタを見つける
    size_t close_pos = find_matching_delimiter(input, input_pos);
    if (close_pos == std::string::npos) {
        state.error_messages.push_back("Unmatched delimiter");
        return false;
    }

    // デリミタ内のトークンを抽出
    std::vector<Token> inner_tokens(
        input.begin() + input_pos + 1,
        input.begin() + close_pos
    );

    // 内部パターンとマッチング
    MatchState inner_state;
    if (!match_recursive(inner_tokens, pattern.tokens, 0, 0, inner_state)) {
        return false;
    }

    // バインディングをマージ
    for (const auto& [name, value] : inner_state.bindings) {
        state.bindings[name] = value;
    }

    input_pos = close_pos + 1;  // 閉じデリミタの次へ
    return true;
}

// メタ変数のマッチング
bool MacroMatcher::match_metavar(
    const std::vector<Token>& input,
    size_t& input_pos,
    const MetaVariable& metavar,
    MatchState& state
) {
    auto matched = match_fragment(input, input_pos, metavar.specifier);
    if (!matched) {
        state.error_messages.push_back(fmt::format(
            "Failed to match metavar ${} as {}",
            metavar.name,
            fragment_spec_to_string(metavar.specifier)
        ));
        return false;
    }

    // バインディングに追加
    if (matched->size() == 1) {
        state.bindings[metavar.name] = MatchedFragment(matched->at(0));
    } else {
        state.bindings[metavar.name] = MatchedFragment(*matched);
    }

    return true;
}

// 繰り返しのマッチング
bool MacroMatcher::match_repetition(
    const std::vector<Token>& input,
    size_t& input_pos,
    const RepetitionNode& repetition,
    MatchState& state
) {
    std::vector<MatchedFragment> matches;
    size_t match_count = 0;
    size_t current_pos = input_pos;

    while (current_pos < input.size()) {
        // セパレータのチェック（2回目以降のマッチ）
        if (match_count > 0 && repetition.separator) {
            if (current_pos >= input.size() ||
                input[current_pos].value != repetition.separator->value) {
                break;  // セパレータがないので繰り返し終了
            }
            current_pos++;  // セパレータをスキップ
        }

        // パターンのマッチングを試みる
        MatchState iter_state;
        size_t iter_start = current_pos;

        if (!match_recursive(input, repetition.pattern, current_pos, 0, iter_state)) {
            break;  // マッチ失敗、繰り返し終了
        }

        // マッチ成功
        // TODO: iter_stateのバインディングをmatchesに追加
        match_count++;
        current_pos = iter_state.deepest_match_pos;

        // 進捗がない場合は無限ループを防ぐため終了
        if (current_pos == iter_start) {
            break;
        }
    }

    // 必要な繰り返し回数をチェック
    bool success = false;
    switch (repetition.op) {
        case RepetitionOp::ZERO_OR_MORE:
            success = true;  // 0回以上なので常に成功
            break;
        case RepetitionOp::ONE_OR_MORE:
            success = match_count > 0;
            break;
        case RepetitionOp::ZERO_OR_ONE:
            success = match_count <= 1;
            break;
    }

    if (success) {
        input_pos = current_pos;
        // TODO: matchesをstateのバインディングに追加
    }

    return success;
}

// フラグメント指定子に応じたマッチング
std::optional<std::vector<Token>> MacroMatcher::match_fragment(
    const std::vector<Token>& input,
    size_t& input_pos,
    FragmentSpecifier spec
) {
    switch (spec) {
        case FragmentSpecifier::EXPR:
            return match_expr(input, input_pos);
        case FragmentSpecifier::STMT:
            return match_stmt(input, input_pos);
        case FragmentSpecifier::TY:
            return match_type(input, input_pos);
        case FragmentSpecifier::IDENT:
            return match_ident(input, input_pos);
        case FragmentSpecifier::PATH:
            return match_path(input, input_pos);
        case FragmentSpecifier::LITERAL:
            return match_literal(input, input_pos);
        case FragmentSpecifier::BLOCK:
            return match_block(input, input_pos);
        case FragmentSpecifier::PAT:
            return match_pattern(input, input_pos);
        case FragmentSpecifier::ITEM:
            return match_item(input, input_pos);
        case FragmentSpecifier::META:
            return match_meta(input, input_pos);
        case FragmentSpecifier::TT:
            return match_tt(input, input_pos);
        default:
            return std::nullopt;
    }
}

// 式のマッチング
std::optional<std::vector<Token>> MacroMatcher::match_expr(
    const std::vector<Token>& input,
    size_t& input_pos
) {
    if (input_pos >= input.size()) {
        return std::nullopt;
    }

    std::vector<Token> result;
    size_t start = input_pos;

    // 簡単な実装：バランスの取れたトークンを式として扱う
    int paren_depth = 0;
    int bracket_depth = 0;
    int brace_depth = 0;

    while (input_pos < input.size()) {
        const Token& token = input[input_pos];

        // デリミタの深さを追跡
        if (token.value == "(") paren_depth++;
        else if (token.value == ")") {
            if (paren_depth == 0) break;
            paren_depth--;
        }
        else if (token.value == "[") bracket_depth++;
        else if (token.value == "]") {
            if (bracket_depth == 0) break;
            bracket_depth--;
        }
        else if (token.value == "{") brace_depth++;
        else if (token.value == "}") {
            if (brace_depth == 0) break;
            brace_depth--;
        }
        // 式の終端を示すトークン
        else if (paren_depth == 0 && bracket_depth == 0 && brace_depth == 0) {
            if (token.value == ";" || token.value == "," ||
                token.value == ")" || token.value == "]" || token.value == "}") {
                break;
            }
        }

        result.push_back(token);
        input_pos++;
    }

    if (result.empty() || paren_depth != 0 || bracket_depth != 0 || brace_depth != 0) {
        input_pos = start;
        return std::nullopt;
    }

    return result;
}

// 文のマッチング
std::optional<std::vector<Token>> MacroMatcher::match_stmt(
    const std::vector<Token>& input,
    size_t& input_pos
) {
    if (input_pos >= input.size()) {
        return std::nullopt;
    }

    std::vector<Token> result;
    size_t start = input_pos;

    // セミコロンまでまたはブロック全体を文として扱う
    if (input[input_pos].value == "{") {
        // ブロック文
        size_t close = find_matching_delimiter(input, input_pos);
        if (close != std::string::npos) {
            for (size_t i = input_pos; i <= close; ++i) {
                result.push_back(input[i]);
            }
            input_pos = close + 1;
            return result;
        }
    } else {
        // セミコロンで終わる文
        while (input_pos < input.size()) {
            result.push_back(input[input_pos]);
            if (input[input_pos].value == ";") {
                input_pos++;
                return result;
            }
            input_pos++;
        }
    }

    input_pos = start;
    return std::nullopt;
}

// 型のマッチング
std::optional<std::vector<Token>> MacroMatcher::match_type(
    const std::vector<Token>& input,
    size_t& input_pos
) {
    if (input_pos >= input.size()) {
        return std::nullopt;
    }

    std::vector<Token> result;

    // 基本型または識別子
    if (input[input_pos].type == TokenType::Identifier ||
        input[input_pos].type == TokenType::Keyword) {
        result.push_back(input[input_pos]);
        input_pos++;

        // ジェネリック型パラメータ
        if (input_pos < input.size() && input[input_pos].value == "<") {
            size_t close = find_matching_delimiter(input, input_pos);
            if (close != std::string::npos) {
                for (size_t i = input_pos; i <= close; ++i) {
                    result.push_back(input[i]);
                }
                input_pos = close + 1;
            }
        }

        return result;
    }

    return std::nullopt;
}

// 識別子のマッチング
std::optional<std::vector<Token>> MacroMatcher::match_ident(
    const std::vector<Token>& input,
    size_t& input_pos
) {
    if (input_pos >= input.size() ||
        input[input_pos].type != TokenType::Identifier) {
        return std::nullopt;
    }

    std::vector<Token> result{input[input_pos]};
    input_pos++;
    return result;
}

// パスのマッチング（std::vector など）
std::optional<std::vector<Token>> MacroMatcher::match_path(
    const std::vector<Token>& input,
    size_t& input_pos
) {
    if (input_pos >= input.size()) {
        return std::nullopt;
    }

    std::vector<Token> result;

    // 識別子
    if (input[input_pos].type != TokenType::Identifier) {
        return std::nullopt;
    }
    result.push_back(input[input_pos]);
    input_pos++;

    // :: で区切られた追加の識別子
    while (input_pos + 1 < input.size() &&
           input[input_pos].value == ":" &&
           input[input_pos + 1].value == ":") {
        result.push_back(input[input_pos]);      // :
        result.push_back(input[input_pos + 1]);  // :
        input_pos += 2;

        if (input_pos < input.size() &&
            input[input_pos].type == TokenType::Identifier) {
            result.push_back(input[input_pos]);
            input_pos++;
        } else {
            break;
        }
    }

    return result;
}

// リテラルのマッチング
std::optional<std::vector<Token>> MacroMatcher::match_literal(
    const std::vector<Token>& input,
    size_t& input_pos
) {
    if (input_pos >= input.size()) {
        return std::nullopt;
    }

    const Token& token = input[input_pos];
    if (token.type == TokenType::Number ||
        token.type == TokenType::String ||
        token.type == TokenType::Character ||
        (token.type == TokenType::Keyword &&
         (token.value == "true" || token.value == "false" || token.value == "null"))) {
        std::vector<Token> result{token};
        input_pos++;
        return result;
    }

    return std::nullopt;
}

// ブロックのマッチング
std::optional<std::vector<Token>> MacroMatcher::match_block(
    const std::vector<Token>& input,
    size_t& input_pos
) {
    if (input_pos >= input.size() || input[input_pos].value != "{") {
        return std::nullopt;
    }

    size_t close = find_matching_delimiter(input, input_pos);
    if (close == std::string::npos) {
        return std::nullopt;
    }

    std::vector<Token> result;
    for (size_t i = input_pos; i <= close; ++i) {
        result.push_back(input[i]);
    }
    input_pos = close + 1;
    return result;
}

// パターンのマッチング
std::optional<std::vector<Token>> MacroMatcher::match_pattern(
    const std::vector<Token>& input,
    size_t& input_pos
) {
    // 簡単な実装：識別子またはリテラル
    return match_ident(input, input_pos)
        .or_else([&]() { return match_literal(input, input_pos); });
}

// アイテムのマッチング
std::optional<std::vector<Token>> MacroMatcher::match_item(
    const std::vector<Token>& input,
    size_t& input_pos
) {
    if (input_pos >= input.size()) {
        return std::nullopt;
    }

    // 関数、構造体などの定義全体
    // TODO: より詳細な実装が必要
    return match_stmt(input, input_pos);
}

// メタデータのマッチング
std::optional<std::vector<Token>> MacroMatcher::match_meta(
    const std::vector<Token>& input,
    size_t& input_pos
) {
    if (input_pos >= input.size() || input[input_pos].value != "#") {
        return std::nullopt;
    }

    std::vector<Token> result;
    result.push_back(input[input_pos++]);  // #

    if (input_pos < input.size() && input[input_pos].value == "[") {
        size_t close = find_matching_delimiter(input, input_pos);
        if (close != std::string::npos) {
            for (size_t i = input_pos; i <= close; ++i) {
                result.push_back(input[i]);
            }
            input_pos = close + 1;
            return result;
        }
    }

    return std::nullopt;
}

// トークンツリーのマッチング（任意のトークン）
std::optional<std::vector<Token>> MacroMatcher::match_tt(
    const std::vector<Token>& input,
    size_t& input_pos
) {
    if (input_pos >= input.size()) {
        return std::nullopt;
    }

    // デリミタの場合は対応する閉じデリミタまで
    if (input[input_pos].value == "(" ||
        input[input_pos].value == "[" ||
        input[input_pos].value == "{") {
        size_t close = find_matching_delimiter(input, input_pos);
        if (close != std::string::npos) {
            std::vector<Token> result;
            for (size_t i = input_pos; i <= close; ++i) {
                result.push_back(input[i]);
            }
            input_pos = close + 1;
            return result;
        }
    }

    // 単一トークン
    std::vector<Token> result{input[input_pos]};
    input_pos++;
    return result;
}

// デリミタのバランスを確認
size_t MacroMatcher::find_matching_delimiter(
    const std::vector<Token>& tokens,
    size_t start_pos
) const {
    if (start_pos >= tokens.size()) {
        return std::string::npos;
    }

    const std::string& open = tokens[start_pos].value;
    std::string close;

    if (open == "(") close = ")";
    else if (open == "[") close = "]";
    else if (open == "{") close = "}";
    else return std::string::npos;

    int depth = 1;
    for (size_t i = start_pos + 1; i < tokens.size(); ++i) {
        if (tokens[i].value == open) {
            depth++;
        } else if (tokens[i].value == close) {
            depth--;
            if (depth == 0) {
                return i;
            }
        }
    }

    return std::string::npos;
}

// エラーメッセージ生成
std::string MacroMatcher::generate_error(const MatchState& state) const {
    std::stringstream ss;
    ss << "Macro pattern matching failed";

    if (!state.error_messages.empty()) {
        ss << ":\n";
        for (const auto& msg : state.error_messages) {
            ss << "  - " << msg << "\n";
        }
    }

    ss << fmt::format("\nDeepest match position: {}", state.deepest_match_pos);

    return ss.str();
}

}  // namespace cm::macro