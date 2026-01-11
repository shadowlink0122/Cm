#include "token_tree.hpp"

namespace cm::macro {

// TokenTreeのコピーコンストラクタ（深いコピー）
TokenTree::TokenTree(const TokenTree& other) : kind(other.kind) {
    switch (kind) {
        case Kind::TOKEN:
            data = std::get<Token>(other.data);
            break;

        case Kind::DELIMITED:
            if (auto* delim = std::get_if<std::unique_ptr<DelimitedTokens>>(&other.data)) {
                auto new_delim = std::make_unique<DelimitedTokens>();
                new_delim->delimiter = (*delim)->delimiter;
                // 各トークンツリーを深くコピー
                for (const auto& token : (*delim)->tokens) {
                    new_delim->tokens.push_back(
                        token);  // TokenTreeのコピーコンストラクタを再帰的に呼び出し
                }
                data = std::move(new_delim);
            }
            break;

        case Kind::METAVAR:
            data = std::get<MetaVariable>(other.data);
            break;

        case Kind::REPETITION:
            if (auto* rep = std::get_if<std::unique_ptr<RepetitionNode>>(&other.data)) {
                auto new_rep = std::make_unique<RepetitionNode>();
                // パターンを深くコピー
                for (const auto& token : (*rep)->pattern) {
                    new_rep->pattern.push_back(
                        token);  // TokenTreeのコピーコンストラクタを再帰的に呼び出し
                }
                new_rep->op = (*rep)->op;
                new_rep->separator = (*rep)->separator;
                data = std::move(new_rep);
            }
            break;
    }
}

// TokenTreeの代入演算子
TokenTree& TokenTree::operator=(const TokenTree& other) {
    if (this != &other) {
        // 一時オブジェクトを作成してからswap（例外安全）
        TokenTree temp(other);
        std::swap(kind, temp.kind);
        std::swap(data, temp.data);
    }
    return *this;
}

}  // namespace cm::macro