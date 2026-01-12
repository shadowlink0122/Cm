#pragma once

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
    int scope_level = 0;       // スコープレベル（ライフタイム追跡）

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
                bool is_static = false) {
        if (symbols_.count(name))
            return false;  // 既存
        symbols_[name] = Symbol{name, std::move(type), is_const, false,   false, false, is_static,
                                0,    level_,          {},       nullptr, 0};
        return true;
    }

    // 関数登録
    bool define_function(const std::string& name, std::vector<ast::TypePtr> params,
                         ast::TypePtr ret, size_t required_params = SIZE_MAX,
                         bool is_variadic = false) {
        if (symbols_.count(name))
            return false;
        Symbol sym;
        sym.name = name;
        sym.is_function = true;
        sym.is_variadic = is_variadic;
        sym.param_types = params;  // コピーを保持
        sym.return_type = ret;
        // 関数ポインタ型を設定（関数名を参照したときにこの型を返す）
        sym.type = ast::make_function_ptr(ret, std::move(params));
        // required_paramsがSIZE_MAXの場合は全引数が必須
        sym.required_params =
            (required_params == SIZE_MAX) ? sym.param_types.size() : required_params;
        symbols_[name] = std::move(sym);
        return true;
    }

    // シンボル検索（親スコープも検索）
    std::optional<Symbol> lookup(const std::string& name) const {
        auto it = symbols_.find(name);
        if (it != symbols_.end()) {
            return it->second;
        }
        if (parent_) {
            return parent_->lookup(name);
        }
        return std::nullopt;
    }

    // 現スコープのみ検索
    bool has_local(const std::string& name) const { return symbols_.count(name) > 0; }

    // 変数を移動済みとしてマーク（Move Semantics）
    bool mark_moved(const std::string& name) {
        auto it = symbols_.find(name);
        if (it != symbols_.end()) {
            it->second.is_moved = true;
            return true;
        }
        // 親スコープも検索してマーク
        if (parent_) {
            return parent_->mark_moved(name);
        }
        return false;
    }

    // 変数の移動状態をクリア（再代入時）
    bool unmark_moved(const std::string& name) {
        auto it = symbols_.find(name);
        if (it != symbols_.end()) {
            it->second.is_moved = false;
            return true;
        }
        // 親スコープも検索してクリア
        if (parent_) {
            return parent_->unmark_moved(name);
        }
        return false;
    }

    // 変数を借用する（借用カウント増加）
    bool add_borrow(const std::string& name) {
        auto it = symbols_.find(name);
        if (it != symbols_.end()) {
            it->second.borrow_count++;
            return true;
        }
        if (parent_) {
            return parent_->add_borrow(name);
        }
        return false;
    }

    // 借用を解除する（借用カウント減少）
    bool remove_borrow(const std::string& name) {
        auto it = symbols_.find(name);
        if (it != symbols_.end()) {
            if (it->second.borrow_count > 0) {
                it->second.borrow_count--;
            }
            return true;
        }
        if (parent_) {
            return parent_->remove_borrow(name);
        }
        return false;
    }

    // 変数が借用中かチェック
    bool is_borrowed(const std::string& name) const {
        auto it = symbols_.find(name);
        if (it != symbols_.end()) {
            return it->second.borrow_count > 0;
        }
        if (parent_) {
            return parent_->is_borrowed(name);
        }
        return false;
    }

    // 変数がmove済みかチェック
    bool is_moved(const std::string& name) const {
        auto it = symbols_.find(name);
        if (it != symbols_.end()) {
            return it->second.is_moved;
        }
        if (parent_) {
            return parent_->is_moved(name);
        }
        return false;
    }

    Scope* parent() const { return parent_; }

    // 変数のスコープレベルを取得
    int get_scope_level(const std::string& name) const {
        auto it = symbols_.find(name);
        if (it != symbols_.end()) {
            return it->second.scope_level;
        }
        if (parent_) {
            return parent_->get_scope_level(name);
        }
        return 0;  // 見つからない場合はグローバルとみなす
    }

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

    void push() {
        int new_level = static_cast<int>(scopes_.size());
        if (scopes_.empty()) {
            scopes_.push_back(std::make_unique<Scope>(nullptr, new_level));
        } else {
            scopes_.push_back(std::make_unique<Scope>(scopes_.back().get(), new_level));
        }
    }

    void pop() {
        if (scopes_.size() > 1) {
            scopes_.pop_back();
        }
    }

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
