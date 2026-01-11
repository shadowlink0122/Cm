#pragma once

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "../lexer/token.hpp"

namespace cm::macro {

// フラグメント指定子（マクロパターンの型）
enum class FragmentSpecifier {
    EXPR,      // 式
    STMT,      // 文
    PAT,       // パターン
    TY,        // 型
    IDENT,     // 識別子
    PATH,      // パス（std::vector など）
    LITERAL,   // リテラル
    BLOCK,     // ブロック { ... }
    ITEM,      // アイテム（関数、構造体定義など）
    META,      // メタデータ（アトリビュート）
    TT,        // トークンツリー（任意のトークン）
};

// 繰り返し演算子
enum class RepetitionOp {
    ZERO_OR_MORE,  // *
    ONE_OR_MORE,   // +
    ZERO_OR_ONE,   // ?
};

// 前方宣言
struct TokenTree;
struct DelimitedTokens;
struct MetaVariable;
struct RepetitionNode;

// 区切り文字の種類
enum class DelimiterKind {
    PAREN,    // ()
    BRACKET,  // []
    BRACE,    // {}
};

// 区切られたトークン群
struct DelimitedTokens {
    DelimiterKind delimiter;
    std::vector<TokenTree> tokens;
};

// メタ変数 ($name:fragment_spec)
struct MetaVariable {
    std::string name;
    FragmentSpecifier specifier;
};

// 繰り返しノード $(...)*
struct RepetitionNode {
    std::vector<TokenTree> pattern;
    RepetitionOp op;
    std::optional<Token> separator;  // 区切りトークン（,など）
};

// トークンツリー（マクロの基本単位）
struct TokenTree {
    enum class Kind {
        TOKEN,       // 単一トークン
        DELIMITED,   // 括弧で囲まれたトークン群
        METAVAR,     // メタ変数 $name:spec
        REPETITION,  // 繰り返し $(...)*
    };

    Kind kind;
    std::variant<
        Token,
        std::unique_ptr<DelimitedTokens>,
        MetaVariable,
        std::unique_ptr<RepetitionNode>
    > data;

    // コンストラクタ
    explicit TokenTree(Token token)
        : kind(Kind::TOKEN), data(std::move(token)) {}

    explicit TokenTree(std::unique_ptr<DelimitedTokens> delimited)
        : kind(Kind::DELIMITED), data(std::move(delimited)) {}

    explicit TokenTree(MetaVariable metavar)
        : kind(Kind::METAVAR), data(std::move(metavar)) {}

    explicit TokenTree(std::unique_ptr<RepetitionNode> repetition)
        : kind(Kind::REPETITION), data(std::move(repetition)) {}

    // コピーコンストラクタ（深いコピー）
    TokenTree(const TokenTree& other);

    // ムーブコンストラクタ
    TokenTree(TokenTree&& other) noexcept = default;

    // デストラクタ
    ~TokenTree() = default;

    // 代入演算子
    TokenTree& operator=(const TokenTree& other);
    TokenTree& operator=(TokenTree&& other) noexcept = default;

    // ヘルパー関数
    bool is_token() const { return kind == Kind::TOKEN; }
    bool is_delimited() const { return kind == Kind::DELIMITED; }
    bool is_metavar() const { return kind == Kind::METAVAR; }
    bool is_repetition() const { return kind == Kind::REPETITION; }

    // トークンを取得（TOKEN型の場合のみ）
    const Token* get_token() const {
        if (kind == Kind::TOKEN) {
            return std::get_if<Token>(&data);
        }
        return nullptr;
    }

    // デリミタを取得（DELIMITED型の場合のみ）
    const DelimitedTokens* get_delimited() const {
        if (kind == Kind::DELIMITED) {
            if (auto ptr = std::get_if<std::unique_ptr<DelimitedTokens>>(&data)) {
                return ptr->get();
            }
        }
        return nullptr;
    }

    // メタ変数を取得（METAVAR型の場合のみ）
    const MetaVariable* get_metavar() const {
        if (kind == Kind::METAVAR) {
            return std::get_if<MetaVariable>(&data);
        }
        return nullptr;
    }

    // 繰り返しを取得（REPETITION型の場合のみ）
    const RepetitionNode* get_repetition() const {
        if (kind == Kind::REPETITION) {
            if (auto ptr = std::get_if<std::unique_ptr<RepetitionNode>>(&data)) {
                return ptr->get();
            }
        }
        return nullptr;
    }
};

// マクロパターン（マッチング対象）
struct MacroPattern {
    std::vector<TokenTree> tokens;
};

// マクロトランスクライバー（展開結果）
struct MacroTranscriber {
    std::vector<TokenTree> tokens;
};

// マクロルール（パターン → トランスクライバー）
struct MacroRule {
    MacroPattern pattern;
    MacroTranscriber transcriber;
};

// マクロ定義
struct MacroDefinition {
    std::string name;
    std::vector<MacroRule> rules;
    SourceLocation location;
};

// マクロ呼び出し
struct MacroCall {
    std::string name;
    std::vector<Token> args;  // 呼び出し時の実引数トークン
    SourceLocation location;
};

// フラグメント指定子の文字列変換
inline const char* fragment_spec_to_string(FragmentSpecifier spec) {
    switch (spec) {
        case FragmentSpecifier::EXPR: return "expr";
        case FragmentSpecifier::STMT: return "stmt";
        case FragmentSpecifier::PAT: return "pat";
        case FragmentSpecifier::TY: return "ty";
        case FragmentSpecifier::IDENT: return "ident";
        case FragmentSpecifier::PATH: return "path";
        case FragmentSpecifier::LITERAL: return "literal";
        case FragmentSpecifier::BLOCK: return "block";
        case FragmentSpecifier::ITEM: return "item";
        case FragmentSpecifier::META: return "meta";
        case FragmentSpecifier::TT: return "tt";
        default: return "unknown";
    }
}

// 文字列からフラグメント指定子への変換
inline std::optional<FragmentSpecifier> string_to_fragment_spec(const std::string& str) {
    if (str == "expr") return FragmentSpecifier::EXPR;
    if (str == "stmt") return FragmentSpecifier::STMT;
    if (str == "pat") return FragmentSpecifier::PAT;
    if (str == "ty") return FragmentSpecifier::TY;
    if (str == "ident") return FragmentSpecifier::IDENT;
    if (str == "path") return FragmentSpecifier::PATH;
    if (str == "literal") return FragmentSpecifier::LITERAL;
    if (str == "block") return FragmentSpecifier::BLOCK;
    if (str == "item") return FragmentSpecifier::ITEM;
    if (str == "meta") return FragmentSpecifier::META;
    if (str == "tt") return FragmentSpecifier::TT;
    return std::nullopt;
}

}  // namespace cm::macro