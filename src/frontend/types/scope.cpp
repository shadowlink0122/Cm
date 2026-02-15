// スコープ管理の実装
#include "scope.hpp"

namespace cm {

// シンボル登録
bool Scope::define(const std::string& name, ast::TypePtr type, bool is_const, bool is_static,
                   Span span, std::optional<int64_t> const_int_value) {
    if (symbols_.count(name))
        return false;  // 既存
    Symbol sym;
    sym.name = name;
    sym.type = std::move(type);
    sym.is_const = is_const;
    sym.is_static = is_static;
    sym.scope_level = level_;
    sym.span = span;
    sym.const_int_value = const_int_value;
    symbols_[name] = std::move(sym);
    return true;
}

// 関数登録
bool Scope::define_function(const std::string& name, std::vector<ast::TypePtr> params,
                            ast::TypePtr ret, size_t required_params, bool is_variadic) {
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
    sym.required_params = (required_params == SIZE_MAX) ? sym.param_types.size() : required_params;
    symbols_[name] = std::move(sym);
    return true;
}

// シンボル検索（親スコープも検索）
std::optional<Symbol> Scope::lookup(const std::string& name) const {
    auto it = symbols_.find(name);
    if (it != symbols_.end()) {
        return it->second;
    }
    if (parent_) {
        return parent_->lookup(name);
    }
    return std::nullopt;
}

// 変数を使用済みとしてマーク（未使用検出用）
bool Scope::mark_used(const std::string& name) {
    auto it = symbols_.find(name);
    if (it != symbols_.end()) {
        it->second.use_count++;
        return true;
    }
    if (parent_) {
        return parent_->mark_used(name);
    }
    return false;
}

// 未使用シンボル取得（現スコープのみ）
std::vector<Symbol> Scope::get_unused_symbols() const {
    std::vector<Symbol> unused;
    for (const auto& [name, sym] : symbols_) {
        // 関数は除外、変数のみチェック
        // 空のspanを持つシンボル（ビルトイン）も除外
        if (!sym.is_function && sym.use_count == 0 && !sym.span.is_empty()) {
            unused.push_back(sym);
        }
    }
    return unused;
}

// 変数を移動済みとしてマーク（Move Semantics）
bool Scope::mark_moved(const std::string& name) {
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
bool Scope::unmark_moved(const std::string& name) {
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
bool Scope::add_borrow(const std::string& name) {
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
bool Scope::remove_borrow(const std::string& name) {
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
bool Scope::is_borrowed(const std::string& name) const {
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
bool Scope::is_moved(const std::string& name) const {
    auto it = symbols_.find(name);
    if (it != symbols_.end()) {
        return it->second.is_moved;
    }
    if (parent_) {
        return parent_->is_moved(name);
    }
    return false;
}

// 変数のスコープレベルを取得
int Scope::get_scope_level(const std::string& name) const {
    auto it = symbols_.find(name);
    if (it != symbols_.end()) {
        return it->second.scope_level;
    }
    if (parent_) {
        return parent_->get_scope_level(name);
    }
    return 0;  // 見つからない場合はグローバルとみなす
}

// スコープスタック: push
void ScopeStack::push() {
    int new_level = static_cast<int>(scopes_.size());
    if (scopes_.empty()) {
        scopes_.push_back(std::make_unique<Scope>(nullptr, new_level));
    } else {
        scopes_.push_back(std::make_unique<Scope>(scopes_.back().get(), new_level));
    }
}

// スコープスタック: pop
void ScopeStack::pop() {
    if (scopes_.size() > 1) {
        scopes_.pop_back();
    }
}

}  // namespace cm
