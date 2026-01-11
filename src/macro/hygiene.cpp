#include "hygiene.hpp"

#include <fmt/format.h>
#include <iostream>

namespace cm::macro {

// コンストラクタ
HygieneContext::HygieneContext()
    : next_context_id_(1)
    , gensym_counter_(0)
    , current_expansion_depth_(0)
    , name_map_(hygiene_ident_less) {
}

// 新しい構文コンテキストを作成
SyntaxContext HygieneContext::create_context(
    const std::string& macro_name,
    const SourceLocation& call_site,
    const std::shared_ptr<SyntaxContext>& parent
) {
    SyntaxContext context;
    context.id = next_context_id_.fetch_add(1);
    context.expansion.macro_name = macro_name;
    context.expansion.call_site = call_site;
    context.expansion.expansion_depth = parent ? parent->expansion.expansion_depth + 1 : 0;
    context.parent = parent;

    return context;
}

// ユニークなシンボル生成
std::string HygieneContext::gensym(const std::string& base) {
    uint32_t id = gensym_counter_.fetch_add(1);
    return fmt::format("{}_{}", base, id);
}

// 識別子を衛生的にする
HygienicIdent HygieneContext::make_hygienic(
    const std::string& name,
    const SyntaxContext& context
) {
    HygienicIdent ident;
    ident.name = name;
    ident.context = context;
    return ident;
}

// トークンを衛生的にする
Token HygieneContext::make_hygienic_token(
    const Token& token,
    const SyntaxContext& context
) {
    // 識別子トークンの場合のみ衛生的な処理を適用
    if (token.type == TokenType::Identifier) {
        auto hygienic_ident = make_hygienic(token.value, context);
        std::string resolved_name = resolve_ident(hygienic_ident);

        Token new_token = token;
        new_token.value = resolved_name;
        return new_token;
    }

    // 識別子以外はそのまま返す
    return token;
}

// 識別子の解決
std::string HygieneContext::resolve_ident(const HygienicIdent& ident) {
    // 既に解決済みの名前があればそれを返す
    auto it = name_map_.find(ident);
    if (it != name_map_.end()) {
        return it->second;
    }

    // 名前の衝突をチェック
    if (has_name_conflict(ident.name, ident.context)) {
        // 衝突がある場合はユニークな名前を生成
        std::string unique_name = generate_unique_name(ident.name);
        name_map_[ident] = unique_name;
        return unique_name;
    }

    // 衝突がない場合は元の名前を使用
    name_map_[ident] = ident.name;
    return ident.name;
}

// スコープに入る
void HygieneContext::enter_scope(const SyntaxContext& context) {
    scope_stack_.push_back(context);
    current_expansion_depth_ = context.expansion.expansion_depth;
}

// スコープから出る
void HygieneContext::exit_scope() {
    if (!scope_stack_.empty()) {
        scope_stack_.pop_back();
        if (!scope_stack_.empty()) {
            current_expansion_depth_ = scope_stack_.back().expansion.expansion_depth;
        } else {
            current_expansion_depth_ = 0;
        }
    }
}

// 現在のコンテキストを取得
SyntaxContext HygieneContext::current_context() const {
    if (!scope_stack_.empty()) {
        return scope_stack_.back();
    }

    // デフォルトコンテキストを返す
    SyntaxContext default_ctx;
    default_ctx.id = 0;
    default_ctx.expansion.expansion_depth = 0;
    return default_ctx;
}

// 名前の衝突チェック
bool HygieneContext::has_name_conflict(
    const std::string& name,
    const SyntaxContext& context
) const {
    // 同じ名前で異なるコンテキストの識別子が既に存在するかチェック
    for (const auto& [ident, resolved_name] : name_map_) {
        if (ident.name == name && !context.is_same_context(ident.context)) {
            // 親子関係にあるコンテキストは衝突としない
            if (!context.is_related_context(ident.context) &&
                !ident.context.is_related_context(context)) {
                return true;
            }
        }
    }
    return false;
}

// デバッグ用：コンテキスト情報をダンプ
void HygieneContext::dump_contexts() const {
    std::cout << "[HYGIENE] Context Stack:" << std::endl;
    for (size_t i = 0; i < scope_stack_.size(); ++i) {
        const auto& ctx = scope_stack_[i];
        std::cout << fmt::format("  [{}] {}", i, describe_context(ctx)) << std::endl;
    }

    std::cout << "[HYGIENE] Name Map:" << std::endl;
    for (const auto& [ident, resolved] : name_map_) {
        std::cout << fmt::format("  {} (ctx:{}) -> {}",
            ident.name, ident.context.id, resolved) << std::endl;
    }
}

// コンテキストの説明文を生成
std::string HygieneContext::describe_context(const SyntaxContext& context) const {
    return fmt::format("Context(id:{}, macro:{}, depth:{}, location:{}:{})",
        context.id,
        context.expansion.macro_name,
        context.expansion.expansion_depth,
        context.expansion.call_site.line,
        context.expansion.call_site.column);
}

// ユニークな名前を生成（内部用）
std::string HygieneContext::generate_unique_name(const std::string& base) {
    // コンテキストIDを含むユニークな名前を生成
    return fmt::format("{}_ctx{}_{}",
        base,
        current_context().id,
        gensym_counter_.fetch_add(1));
}

}  // namespace cm::macro