#pragma once

#include "../../common/span.hpp"
#include "../ast/types.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace cm {

// ============================================================
// シンボル情報
// ============================================================
struct Symbol {
    std::string name;
    ast::TypePtr type;
    bool is_const = false;
    bool is_function = false;
    bool is_variadic = false;  // FFI関数の可変長引数（printf等）
    bool is_moved = false;     // 所有権が移動済みか（Move Semantics）
    bool is_static = false;    // static変数（プログラム全体のライフタイム）
    size_t borrow_count = 0;   // 借用回数（借用安全性）
    size_t use_count = 0;      // 使用回数（未使用変数検出用）
    int scope_level = 0;       // スコープレベル（ライフタイム追跡）
    Span span;                 // 宣言位置（警告の正確な行番号用）

    // コンパイル時定数の値（const強化：配列サイズ等で使用）
    std::optional<int64_t> const_int_value;

    // 関数の場合
    std::vector<ast::TypePtr> param_types;
    ast::TypePtr return_type;
    size_t required_params = 0;  // 必須引数の数（デフォルト値のない引数）
};

// ============================================================
// スコープ
// ============================================================
class Scope {
   public:
    explicit Scope(Scope* parent = nullptr, int level = 0) : parent_(parent), level_(level) {}

    // スコープレベル取得
    int level() const { return level_; }

    // シンボル登録
    bool define(const std::string& name, ast::TypePtr type, bool is_const = false,
                bool is_static = false, Span span = Span{},
                std::optional<int64_t> const_int_value = std::nullopt);

    // 関数登録
    bool define_function(const std::string& name, std::vector<ast::TypePtr> params,
                         ast::TypePtr ret, size_t required_params = SIZE_MAX,
                         bool is_variadic = false);

    // シンボル検索（親スコープも検索）
    std::optional<Symbol> lookup(const std::string& name) const;

    // 現スコープのみ検索
    bool has_local(const std::string& name) const { return symbols_.count(name) > 0; }

    // 変数を使用済みとしてマーク（未使用検出用）
    bool mark_used(const std::string& name);

    // 未使用シンボル取得（現スコープのみ）
    std::vector<Symbol> get_unused_symbols() const;

    // 変数を移動済みとしてマーク（Move Semantics）
    bool mark_moved(const std::string& name);

    // 変数の移動状態をクリア（再代入時）
    bool unmark_moved(const std::string& name);

    // 変数を借用する（借用カウント増加）
    bool add_borrow(const std::string& name);

    // 借用を解除する（借用カウント減少）
    bool remove_borrow(const std::string& name);

    // 変数が借用中かチェック
    bool is_borrowed(const std::string& name) const;

    // 変数がmove済みかチェック
    bool is_moved(const std::string& name) const;

    // 親スコープ取得
    Scope* parent() const { return parent_; }

    // 変数のスコープレベルを取得
    int get_scope_level(const std::string& name) const;

   private:
    Scope* parent_;
    int level_ = 0;
    std::unordered_map<std::string, Symbol> symbols_;
};

// ============================================================
// スコープスタック管理
// ============================================================
class ScopeStack {
   public:
    ScopeStack() {
        push();  // グローバルスコープ（レベル0）
    }

    // スコープのpush/pop
    void push();
    void pop();

    // 現在のスコープ
    Scope& current() { return *scopes_.back(); }
    const Scope& current() const { return *scopes_.back(); }

    // 現在のスコープレベル
    int current_level() const { return static_cast<int>(scopes_.size()) - 1; }

    // グローバルスコープ
    Scope& global() { return *scopes_.front(); }

   private:
    std::vector<std::unique_ptr<Scope>> scopes_;
};

}  // namespace cm
