#pragma once

#include "../lexer/token.hpp"

#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace cm::macro {

// 展開情報
struct ExpansionInfo {
    std::string macro_name;    // 展開されたマクロの名前
    SourceLocation call_site;  // マクロ呼び出し位置
    size_t expansion_depth;    // 展開の深さ（再帰レベル）
};

// 構文コンテキスト
// マクロ展開時の識別子のスコープを管理
struct SyntaxContext {
    uint32_t id;                             // ユニークなコンテキストID
    ExpansionInfo expansion;                 // 展開情報
    std::set<std::string> introduced_names;  // このコンテキストで導入された名前
    std::shared_ptr<SyntaxContext> parent;  // 親コンテキスト（ネストしたマクロ用）

    // コンテキストが同じか判定
    bool is_same_context(const SyntaxContext& other) const { return id == other.id; }

    // 親コンテキストまで含めて同じか判定
    bool is_related_context(const SyntaxContext& other) const {
        // 自身か祖先のコンテキストと一致すればtrue
        const SyntaxContext* current = this;
        while (current) {
            if (current->id == other.id) {
                return true;
            }
            current = current->parent.get();
        }
        return false;
    }
};

// 衛生的な識別子
// コンテキスト情報を保持する識別子
struct HygienicIdent {
    std::string name;       // 識別子の名前
    SyntaxContext context;  // 構文コンテキスト

    // 同じ識別子か判定（名前とコンテキストの両方が一致）
    bool operator==(const HygienicIdent& other) const {
        return name == other.name && context.is_same_context(other.context);
    }

    bool operator!=(const HygienicIdent& other) const { return !(*this == other); }

    // ハッシュ関数用
    struct Hash {
        size_t operator()(const HygienicIdent& ident) const {
            return std::hash<std::string>{}(ident.name) ^
                   (std::hash<uint32_t>{}(ident.context.id) << 1);
        }
    };
};

// 衛生性コンテキスト管理
class HygieneContext {
   public:
    HygieneContext();
    ~HygieneContext() = default;

    // 新しい構文コンテキストを作成
    SyntaxContext create_context(const std::string& macro_name, const SourceLocation& call_site,
                                 const std::shared_ptr<SyntaxContext>& parent = nullptr);

    // ユニークなシンボル生成（gensym）
    std::string gensym(const std::string& base = "__gensym");

    // 識別子を衛生的にする
    HygienicIdent make_hygienic(const std::string& name, const SyntaxContext& context);

    // トークンを衛生的にする
    Token make_hygienic_token(const Token& token, const SyntaxContext& context);

    // 識別子の解決
    // 異なるコンテキストの同名識別子を区別する
    std::string resolve_ident(const HygienicIdent& ident);

    // スコープの管理
    void enter_scope(const SyntaxContext& context);
    void exit_scope();
    SyntaxContext current_context() const;

    // 名前の衝突チェック
    bool has_name_conflict(const std::string& name, const SyntaxContext& context) const;

    // デバッグ用
    void dump_contexts() const;
    std::string describe_context(const SyntaxContext& context) const;

   private:
    // コンテキストIDジェネレータ
    std::atomic<uint32_t> next_context_id_;

    // gensymカウンタ
    std::atomic<uint32_t> gensym_counter_;

    // 現在の展開深度
    size_t current_expansion_depth_;

    // スコープスタック
    std::vector<SyntaxContext> scope_stack_;

    // 名前解決マップ（衛生的な識別子 → 実際の名前）
    std::map<HygienicIdent, std::string, decltype(&hygiene_ident_less)> name_map_;

    // 名前の競合を避けるための比較関数
    static bool hygiene_ident_less(const HygienicIdent& a, const HygienicIdent& b) {
        if (a.name != b.name) {
            return a.name < b.name;
        }
        return a.context.id < b.context.id;
    }

    // ユニークな名前を生成（内部用）
    std::string generate_unique_name(const std::string& base);
};

// マクロ展開時の衛生性を保証するヘルパークラス
class HygieneGuard {
   public:
    HygieneGuard(HygieneContext& hygiene, const SyntaxContext& context) : hygiene_(hygiene) {
        hygiene_.enter_scope(context);
    }

    ~HygieneGuard() { hygiene_.exit_scope(); }

    // コピー・ムーブ禁止
    HygieneGuard(const HygieneGuard&) = delete;
    HygieneGuard& operator=(const HygieneGuard&) = delete;
    HygieneGuard(HygieneGuard&&) = delete;
    HygieneGuard& operator=(HygieneGuard&&) = delete;

   private:
    HygieneContext& hygiene_;
};

}  // namespace cm::macro