#pragma once

#include "../lexer/token.hpp"
#include "hygiene.hpp"
#include "matcher.hpp"
#include "token_tree.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace cm::macro {

// マクロ展開エラー
class MacroExpansionError : public std::runtime_error {
   public:
    enum class Kind {
        UNDEFINED_MACRO,
        NO_MATCHING_PATTERN,
        RECURSION_LIMIT,
        EXPANSION_OVERFLOW,
        UNBOUND_METAVAR,
        INVALID_REPETITION,
    };

    MacroExpansionError(Kind kind, const std::string& message, const SourceLocation& location)
        : std::runtime_error(format_error(kind, message, location)),
          kind_(kind),
          location_(location) {}

    Kind kind() const { return kind_; }
    const SourceLocation& location() const { return location_; }

   private:
    Kind kind_;
    SourceLocation location_;

    static std::string format_error(Kind kind, const std::string& message,
                                    const SourceLocation& location);
};

// 展開統計情報
struct ExpansionStats {
    size_t total_expansions = 0;
    size_t max_recursion_depth = 0;
    size_t total_tokens_generated = 0;
    std::map<std::string, size_t> macro_call_counts;
};

// マクロ展開設定
struct ExpansionConfig {
    size_t max_recursion_depth = 128;   // 最大再帰深度
    size_t max_expansion_size = 65536;  // 最大展開サイズ（トークン数）
    bool enable_hygiene = true;         // 衛生性を有効化
    bool enable_caching = true;         // 展開結果のキャッシュ
    bool trace_expansions = false;      // 展開のトレース出力
};

// マクロ展開器
class MacroExpander {
   public:
    explicit MacroExpander(const ExpansionConfig& config = ExpansionConfig{});
    ~MacroExpander() = default;

    // マクロ定義を登録
    void register_macro(std::unique_ptr<MacroDefinition> definition);

    // マクロ定義を削除
    void unregister_macro(const std::string& name);

    // マクロが定義されているか確認
    bool has_macro(const std::string& name) const;

    // マクロ呼び出しを展開
    std::vector<Token> expand(const MacroCall& call);

    // トークンストリーム中のマクロをすべて展開
    std::vector<Token> expand_all(const std::vector<Token>& tokens);

    // 統計情報を取得
    const ExpansionStats& stats() const { return stats_; }

    // 統計情報をリセット
    void reset_stats() { stats_ = ExpansionStats{}; }

    // デバッグ用：展開のトレースを有効化
    void set_trace_enabled(bool enabled) { config_.trace_expansions = enabled; }

   private:
    // マクロ定義の格納
    std::map<std::string, std::unique_ptr<MacroDefinition>> macros_;

    // 展開設定
    ExpansionConfig config_;

    // 衛生性コンテキスト
    HygieneContext hygiene_;

    // マッチャー
    MacroMatcher matcher_;

    // 展開キャッシュ（メモ化）
    struct CacheKey {
        std::string macro_name;
        std::vector<Token> args;

        bool operator<(const CacheKey& other) const;
    };
    std::map<CacheKey, std::vector<Token>> expansion_cache_;

    // 統計情報
    ExpansionStats stats_;

    // 現在の再帰深度
    size_t current_recursion_depth_ = 0;

    // 内部メソッド

    // 単一のマクロを展開
    std::vector<Token> expand_single(const MacroDefinition& definition,
                                     const std::vector<Token>& args,
                                     const SourceLocation& call_site);

    // トランスクライバーを展開
    std::vector<Token> transcribe(const MacroTranscriber& transcriber,
                                  const MatchBindings& bindings, const SyntaxContext& context);

    // トークンツリーを展開
    std::vector<Token> transcribe_tree(const TokenTree& tree, const MatchBindings& bindings,
                                       const SyntaxContext& context);

    // メタ変数を展開
    std::vector<Token> transcribe_metavar(const MetaVariable& metavar,
                                          const MatchBindings& bindings,
                                          const SyntaxContext& context);

    // 繰り返しを展開
    std::vector<Token> transcribe_repetition(const RepetitionNode& repetition,
                                             const MatchBindings& bindings,
                                             const SyntaxContext& context);

    // トークンストリームからマクロ呼び出しを検出
    std::optional<MacroCall> detect_macro_call(const std::vector<Token>& tokens, size_t& pos);

    // マクロ呼び出しの引数を解析
    std::vector<Token> parse_macro_args(const std::vector<Token>& tokens, size_t& pos);

    // 展開結果のトークンを衛生的にする
    std::vector<Token> apply_hygiene(const std::vector<Token>& tokens,
                                     const SyntaxContext& context);

    // 再帰深度のチェック
    void check_recursion_depth(const std::string& macro_name);

    // 展開サイズのチェック
    void check_expansion_size(size_t size);

    // トレース出力
    void trace(const std::string& message);
    void trace_expansion(const MacroCall& call, const std::vector<Token>& result);

    // 再帰深度管理用のRAIIガード
    class RecursionGuard {
       public:
        RecursionGuard(MacroExpander& expander, const std::string& macro_name)
            : expander_(expander), macro_name_(macro_name) {
            expander_.current_recursion_depth_++;
            expander_.check_recursion_depth(macro_name_);
        }

        ~RecursionGuard() { expander_.current_recursion_depth_--; }

        // コピー・ムーブ禁止
        RecursionGuard(const RecursionGuard&) = delete;
        RecursionGuard& operator=(const RecursionGuard&) = delete;

       private:
        MacroExpander& expander_;
        std::string macro_name_;
    };
};

}  // namespace cm::macro