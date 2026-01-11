#include "expander.hpp"

#include <fmt/format.h>
#include <iostream>
#include <sstream>

namespace cm::macro {

// MacroExpansionError のエラーメッセージフォーマット
std::string MacroExpansionError::format_error(Kind kind, const std::string& message,
                                              const SourceLocation& location) {
    std::string kind_str;
    switch (kind) {
        case Kind::UNDEFINED_MACRO:
            kind_str = "Undefined macro";
            break;
        case Kind::NO_MATCHING_PATTERN:
            kind_str = "No matching pattern";
            break;
        case Kind::RECURSION_LIMIT:
            kind_str = "Recursion limit exceeded";
            break;
        case Kind::EXPANSION_OVERFLOW:
            kind_str = "Expansion overflow";
            break;
        case Kind::UNBOUND_METAVAR:
            kind_str = "Unbound metavariable";
            break;
        case Kind::INVALID_REPETITION:
            kind_str = "Invalid repetition";
            break;
    }
    return fmt::format("[MACRO] {}: {} at {}:{}", kind_str, message, location.line,
                       location.column);
}

// CacheKey の比較演算子
bool MacroExpander::CacheKey::operator<(const CacheKey& other) const {
    if (macro_name != other.macro_name) {
        return macro_name < other.macro_name;
    }
    if (args.size() != other.args.size()) {
        return args.size() < other.args.size();
    }
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i].value != other.args[i].value) {
            return args[i].value < other.args[i].value;
        }
    }
    return false;
}

// コンストラクタ
MacroExpander::MacroExpander(const ExpansionConfig& config) : config_(config) {}

// マクロ定義を登録
void MacroExpander::register_macro(std::unique_ptr<MacroDefinition> definition) {
    if (!definition) {
        return;
    }

    std::string name = definition->name;
    macros_[name] = std::move(definition);

    if (config_.trace_expansions) {
        trace(fmt::format("Registered macro: {}", name));
    }
}

// マクロ定義を削除
void MacroExpander::unregister_macro(const std::string& name) {
    macros_.erase(name);

    if (config_.trace_expansions) {
        trace(fmt::format("Unregistered macro: {}", name));
    }
}

// マクロが定義されているか確認
bool MacroExpander::has_macro(const std::string& name) const {
    return macros_.find(name) != macros_.end();
}

// マクロ呼び出しを展開
std::vector<Token> MacroExpander::expand(const MacroCall& call) {
    // マクロが定義されているか確認
    auto it = macros_.find(call.name);
    if (it == macros_.end()) {
        throw MacroExpansionError(MacroExpansionError::Kind::UNDEFINED_MACRO,
                                  fmt::format("Macro '{}' is not defined", call.name),
                                  call.location);
    }

    // 統計情報を更新
    stats_.total_expansions++;
    stats_.macro_call_counts[call.name]++;

    // キャッシュをチェック
    if (config_.enable_caching) {
        CacheKey key{call.name, call.args};
        auto cache_it = expansion_cache_.find(key);
        if (cache_it != expansion_cache_.end()) {
            if (config_.trace_expansions) {
                trace(fmt::format("Using cached expansion for {}", call.name));
            }
            return cache_it->second;
        }
    }

    // 再帰深度ガード
    RecursionGuard guard(*this, call.name);

    // 展開を実行
    auto result = expand_single(*it->second, call.args, call.location);

    // キャッシュに保存
    if (config_.enable_caching) {
        CacheKey key{call.name, call.args};
        expansion_cache_[key] = result;
    }

    // トレース出力
    if (config_.trace_expansions) {
        trace_expansion(call, result);
    }

    return result;
}

// トークンストリーム中のマクロをすべて展開
std::vector<Token> MacroExpander::expand_all(const std::vector<Token>& tokens) {
    std::vector<Token> result;
    size_t pos = 0;

    while (pos < tokens.size()) {
        // マクロ呼び出しを検出
        auto macro_call = detect_macro_call(tokens, pos);

        if (macro_call) {
            // マクロを展開
            auto expanded = expand(*macro_call);

            // 再帰的に展開（ネストしたマクロのため）
            if (current_recursion_depth_ < config_.max_recursion_depth) {
                expanded = expand_all(expanded);
            }

            // 結果に追加
            result.insert(result.end(), expanded.begin(), expanded.end());
        } else {
            // 通常のトークンをコピー
            result.push_back(tokens[pos]);
            pos++;
        }
    }

    return result;
}

// 単一のマクロを展開
std::vector<Token> MacroExpander::expand_single(const MacroDefinition& definition,
                                                const std::vector<Token>& args,
                                                const SourceLocation& call_site) {
    // 各ルールを順番に試す
    for (const auto& rule : definition.rules) {
        // パターンマッチング
        auto match_result = matcher_.match(args, rule.pattern);

        if (match_result.success) {
            // 構文コンテキストを作成
            auto context = hygiene_.create_context(definition.name, call_site, nullptr);

            // トランスクライバーを展開
            auto result = transcribe(rule.transcriber, match_result.bindings, context);

            // 展開サイズをチェック
            check_expansion_size(result.size());

            // 統計情報を更新
            stats_.total_tokens_generated += result.size();

            return result;
        }
    }

    // マッチするパターンがない
    throw MacroExpansionError(MacroExpansionError::Kind::NO_MATCHING_PATTERN,
                              fmt::format("No matching pattern for macro '{}'", definition.name),
                              call_site);
}

// トランスクライバーを展開
std::vector<Token> MacroExpander::transcribe(const MacroTranscriber& transcriber,
                                             const MatchBindings& bindings,
                                             const SyntaxContext& context) {
    std::vector<Token> result;

    // 衛生性ガード
    HygieneGuard hygiene_guard(hygiene_, context);

    // 各トークンツリーを展開
    for (const auto& tree : transcriber.tokens) {
        auto tokens = transcribe_tree(tree, bindings, context);
        result.insert(result.end(), tokens.begin(), tokens.end());
    }

    // 衛生性を適用
    if (config_.enable_hygiene) {
        result = apply_hygiene(result, context);
    }

    return result;
}

// トークンツリーを展開
std::vector<Token> MacroExpander::transcribe_tree(const TokenTree& tree,
                                                  const MatchBindings& bindings,
                                                  const SyntaxContext& context) {
    std::vector<Token> result;

    switch (tree.kind) {
        case TokenTree::Kind::TOKEN:
            if (auto* token = tree.get_token()) {
                result.push_back(*token);
            }
            break;

        case TokenTree::Kind::DELIMITED:
            if (auto* delimited = tree.get_delimited()) {
                // 開きデリミタ
                Token open_delim;
                switch (delimited->delimiter) {
                    case DelimiterKind::PAREN:
                        open_delim = Token{TokenType::Symbol, "(", SourceLocation{}};
                        break;
                    case DelimiterKind::BRACKET:
                        open_delim = Token{TokenType::Symbol, "[", SourceLocation{}};
                        break;
                    case DelimiterKind::BRACE:
                        open_delim = Token{TokenType::Symbol, "{", SourceLocation{}};
                        break;
                }
                result.push_back(open_delim);

                // 内部トークン
                for (const auto& inner : delimited->tokens) {
                    auto inner_tokens = transcribe_tree(inner, bindings, context);
                    result.insert(result.end(), inner_tokens.begin(), inner_tokens.end());
                }

                // 閉じデリミタ
                Token close_delim;
                switch (delimited->delimiter) {
                    case DelimiterKind::PAREN:
                        close_delim = Token{TokenType::Symbol, ")", SourceLocation{}};
                        break;
                    case DelimiterKind::BRACKET:
                        close_delim = Token{TokenType::Symbol, "]", SourceLocation{}};
                        break;
                    case DelimiterKind::BRACE:
                        close_delim = Token{TokenType::Symbol, "}", SourceLocation{}};
                        break;
                }
                result.push_back(close_delim);
            }
            break;

        case TokenTree::Kind::METAVAR:
            if (auto* metavar = tree.get_metavar()) {
                auto tokens = transcribe_metavar(*metavar, bindings, context);
                result.insert(result.end(), tokens.begin(), tokens.end());
            }
            break;

        case TokenTree::Kind::REPETITION:
            if (auto* repetition = tree.get_repetition()) {
                auto tokens = transcribe_repetition(*repetition, bindings, context);
                result.insert(result.end(), tokens.begin(), tokens.end());
            }
            break;
    }

    return result;
}

// メタ変数を展開
std::vector<Token> MacroExpander::transcribe_metavar(const MetaVariable& metavar,
                                                     const MatchBindings& bindings,
                                                     const SyntaxContext& context) {
    // バインディングからメタ変数の値を取得
    auto it = bindings.find(metavar.name);
    if (it == bindings.end()) {
        throw MacroExpansionError(MacroExpansionError::Kind::UNBOUND_METAVAR,
                                  fmt::format("Metavariable '${}' is not bound", metavar.name),
                                  SourceLocation{});
    }

    const MatchedFragment& fragment = it->second;
    std::vector<Token> result;

    // フラグメントの種類に応じて展開
    if (fragment.is_token()) {
        if (auto* token = fragment.get_token()) {
            result.push_back(*token);
        }
    } else if (fragment.is_token_seq()) {
        if (auto* tokens = fragment.get_token_seq()) {
            result = *tokens;
        }
    } else if (fragment.is_repetition()) {
        // 繰り返しの場合はエラー（ここでは処理しない）
        throw MacroExpansionError(
            MacroExpansionError::Kind::INVALID_REPETITION,
            fmt::format("Cannot expand repetition metavariable '${}' directly", metavar.name),
            SourceLocation{});
    }

    return result;
}

// 繰り返しを展開
std::vector<Token> MacroExpander::transcribe_repetition(const RepetitionNode& repetition,
                                                        const MatchBindings& bindings,
                                                        const SyntaxContext& context) {
    std::vector<Token> result;

    // TODO: 繰り返しの展開実装
    // この実装は複雑なため、簡略化

    return result;
}

// トークンストリームからマクロ呼び出しを検出
std::optional<MacroCall> MacroExpander::detect_macro_call(const std::vector<Token>& tokens,
                                                          size_t& pos) {
    if (pos >= tokens.size()) {
        return std::nullopt;
    }

    // identifier! パターンを検出
    if (tokens[pos].type == TokenType::Identifier && pos + 1 < tokens.size() &&
        tokens[pos + 1].value == "!") {
        MacroCall call;
        call.name = tokens[pos].value;
        call.location = tokens[pos].location;

        pos += 2;  // identifier と ! をスキップ

        // 引数を解析
        call.args = parse_macro_args(tokens, pos);

        return call;
    }

    return std::nullopt;
}

// マクロ呼び出しの引数を解析
std::vector<Token> MacroExpander::parse_macro_args(const std::vector<Token>& tokens, size_t& pos) {
    std::vector<Token> args;

    if (pos >= tokens.size()) {
        return args;
    }

    // 開きデリミタを確認
    const Token& open = tokens[pos];
    if (open.value != "(" && open.value != "[" && open.value != "{") {
        return args;
    }

    // 対応する閉じデリミタまでトークンを収集
    int depth = 1;
    pos++;  // 開きデリミタをスキップ

    while (pos < tokens.size() && depth > 0) {
        const Token& token = tokens[pos];

        if (token.value == "(" || token.value == "[" || token.value == "{") {
            depth++;
        } else if (token.value == ")" || token.value == "]" || token.value == "}") {
            depth--;
            if (depth == 0) {
                break;  // 対応する閉じデリミタ
            }
        }

        args.push_back(token);
        pos++;
    }

    if (depth == 0) {
        pos++;  // 閉じデリミタをスキップ
    }

    return args;
}

// 展開結果のトークンを衛生的にする
std::vector<Token> MacroExpander::apply_hygiene(const std::vector<Token>& tokens,
                                                const SyntaxContext& context) {
    std::vector<Token> result;

    for (const auto& token : tokens) {
        result.push_back(hygiene_.make_hygienic_token(token, context));
    }

    return result;
}

// 再帰深度のチェック
void MacroExpander::check_recursion_depth(const std::string& macro_name) {
    if (current_recursion_depth_ > config_.max_recursion_depth) {
        throw MacroExpansionError(MacroExpansionError::Kind::RECURSION_LIMIT,
                                  fmt::format("Macro '{}' exceeded recursion limit of {}",
                                              macro_name, config_.max_recursion_depth),
                                  SourceLocation{});
    }

    // 統計情報を更新
    if (current_recursion_depth_ > stats_.max_recursion_depth) {
        stats_.max_recursion_depth = current_recursion_depth_;
    }
}

// 展開サイズのチェック
void MacroExpander::check_expansion_size(size_t size) {
    if (size > config_.max_expansion_size) {
        throw MacroExpansionError(
            MacroExpansionError::Kind::EXPANSION_OVERFLOW,
            fmt::format("Expansion size {} exceeds limit of {}", size, config_.max_expansion_size),
            SourceLocation{});
    }
}

// トレース出力
void MacroExpander::trace(const std::string& message) {
    if (config_.trace_expansions) {
        std::cout << "[MACRO_TRACE] " << message << std::endl;
    }
}

// 展開のトレース出力
void MacroExpander::trace_expansion(const MacroCall& call, const std::vector<Token>& result) {
    if (!config_.trace_expansions) {
        return;
    }

    std::stringstream ss;
    ss << "Expanded " << call.name << "!(";

    // 引数を出力
    for (size_t i = 0; i < call.args.size(); ++i) {
        if (i > 0)
            ss << " ";
        ss << call.args[i].value;
    }
    ss << ") => ";

    // 結果を出力
    for (size_t i = 0; i < result.size(); ++i) {
        if (i > 0)
            ss << " ";
        ss << result[i].value;
    }

    trace(ss.str());
}

}  // namespace cm::macro