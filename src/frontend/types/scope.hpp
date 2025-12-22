#pragma once

#include "../ast/types.hpp"

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
    explicit Scope(Scope* parent = nullptr) : parent_(parent) {}

    // シンボル登録
    bool define(const std::string& name, ast::TypePtr type, bool is_const = false) {
        if (symbols_.count(name))
            return false;  // 既存
        symbols_[name] = Symbol{name, std::move(type), is_const, false, false, {}, nullptr, 0};
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

    Scope* parent() const { return parent_; }

   private:
    Scope* parent_;
    std::unordered_map<std::string, Symbol> symbols_;
};

// ============================================================
// スコープスタック管理
// ============================================================
class ScopeStack {
   public:
    ScopeStack() {
        push();  // グローバルスコープ
    }

    void push() {
        if (scopes_.empty()) {
            scopes_.push_back(std::make_unique<Scope>(nullptr));
        } else {
            scopes_.push_back(std::make_unique<Scope>(scopes_.back().get()));
        }
    }

    void pop() {
        if (scopes_.size() > 1) {
            scopes_.pop_back();
        }
    }

    Scope& current() { return *scopes_.back(); }
    const Scope& current() const { return *scopes_.back(); }

    // グローバルスコープ
    Scope& global() { return *scopes_.front(); }

   private:
    std::vector<std::unique_ptr<Scope>> scopes_;
};

}  // namespace cm
