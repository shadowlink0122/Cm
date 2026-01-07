// ============================================================
// TypeChecker 実装 - ユーティリティ関数
// ============================================================

#include "../type_checker.hpp"

#include <algorithm>
#include <regex>

namespace cm {

ast::TypePtr TypeChecker::resolve_typedef(ast::TypePtr type) {
    if (!type)
        return type;

    // 名前付き型（Struct/Interface/Generic）の場合
    if (type->kind == ast::TypeKind::Struct || type->kind == ast::TypeKind::Interface ||
        type->kind == ast::TypeKind::Generic) {
        // enum名の場合はint型として解決
        if (enum_names_.count(type->name)) {
            return ast::make_int();
        }

        // typedefに登録されていれば解決
        auto it = typedef_defs_.find(type->name);
        if (it != typedef_defs_.end()) {
            return it->second;
        }
    }

    return type;
}

bool TypeChecker::types_compatible(ast::TypePtr a, ast::TypePtr b) {
    if (!a || !b)
        return false;
    if (a->kind == ast::TypeKind::Error || b->kind == ast::TypeKind::Error)
        return true;

    // 再帰ガード：相互参照型による無限再帰を防止
    // 型ペアを正規化してセットで管理
    static thread_local std::set<std::pair<std::string, std::string>> visited_pairs;
    std::string a_str = ast::type_to_string(*a);
    std::string b_str = ast::type_to_string(*b);
    // 順序を正規化（a < b）
    auto key = a_str < b_str ? std::make_pair(a_str, b_str) : std::make_pair(b_str, a_str);

    if (visited_pairs.count(key) > 0) {
        // 既に比較中のペア → 無限再帰を回避、同じと見なす
        return true;
    }

    // RAIIガード
    struct RecursionGuard {
        std::set<std::pair<std::string, std::string>>& set;
        std::pair<std::string, std::string> k;
        RecursionGuard(std::set<std::pair<std::string, std::string>>& s,
                       std::pair<std::string, std::string> key)
            : set(s), k(key) {
            set.insert(k);
        }
        ~RecursionGuard() { set.erase(k); }
    } guard(visited_pairs, key);

    // ジェネリック型パラメータのチェック
    std::string a_name = ast::type_to_string(*a);
    std::string b_name = ast::type_to_string(*b);
    if (generic_context_.has_type_param(a_name) || generic_context_.has_type_param(b_name)) {
        return a_name == b_name;
    }

    // typedefを展開（名前付き型の場合）
    a = resolve_typedef(a);
    b = resolve_typedef(b);

    // インターフェース互換性チェック
    if (a->kind == ast::TypeKind::Struct && interface_names_.count(a->name)) {
        if (b->kind == ast::TypeKind::Struct && !interface_names_.count(b->name)) {
            auto it = impl_interfaces_.find(b->name);
            if (it != impl_interfaces_.end()) {
                if (it->second.count(a->name)) {
                    return true;
                }
            }
        }
    }

    // 同じ型
    if (a->kind == b->kind) {
        if (a->kind == ast::TypeKind::Struct) {
            return a->name == b->name;
        }
        if (a->kind == ast::TypeKind::Interface) {
            return a->name == b->name;
        }
        // ポインタ型の互換性チェック（借用安全性）
        if (a->kind == ast::TypeKind::Pointer) {
            // void* → T* の暗黙変換を許可（FFI用）
            if (b->element_type && b->element_type->kind == ast::TypeKind::Void) {
                return true;
            }
            // T* → void* の暗黙変換を許可（FFI用）
            if (a->element_type && a->element_type->kind == ast::TypeKind::Void) {
                return true;
            }
            // const T* → T* は禁止（constを外せない）
            // b（代入元）がconstでa（代入先）が非constの場合は禁止
            if (b->qualifiers.is_const && !a->qualifiers.is_const) {
                return false;
            }
            // 要素型のconst外しも禁止
            if (b->element_type && b->element_type->qualifiers.is_const && a->element_type &&
                !a->element_type->qualifiers.is_const) {
                return false;
            }
            // 要素型の互換性をチェック
            return types_compatible(a->element_type, b->element_type);
        }
        // 関数ポインタ型の互換性チェック
        if (a->kind == ast::TypeKind::Function) {
            if (!types_compatible(a->return_type, b->return_type)) {
                return false;
            }
            if (a->param_types.size() != b->param_types.size()) {
                return false;
            }
            for (size_t i = 0; i < a->param_types.size(); ++i) {
                if (!types_compatible(a->param_types[i], b->param_types[i])) {
                    return false;
                }
            }
            return true;
        }
        return true;
    }

    // 数値型間の暗黙変換
    if (a->is_numeric() && b->is_numeric()) {
        return true;
    }

    // 構造体のdefaultメンバとの互換性チェック
    if (a->kind == ast::TypeKind::Struct) {
        auto default_type = get_default_member_type(a->name);
        if (default_type && types_compatible(default_type, b)) {
            return true;
        }
    }
    if (b->kind == ast::TypeKind::Struct) {
        auto default_type = get_default_member_type(b->name);
        if (default_type && types_compatible(a, default_type)) {
            return true;
        }
    }

    // 配列→ポインタ暗黙変換 (array decay)
    if (a->kind == ast::TypeKind::Pointer && b->kind == ast::TypeKind::Array) {
        if (a->element_type && b->element_type) {
            return types_compatible(a->element_type, b->element_type);
        }
    }

    // string → *char 暗黙変換 (FFI用)
    if (a->kind == ast::TypeKind::Pointer && b->kind == ast::TypeKind::String) {
        if (a->element_type && a->element_type->kind == ast::TypeKind::Char) {
            return true;
        }
    }

    // cstring ↔ string 暗黙変換 (FFI用)
    if ((a->kind == ast::TypeKind::CString && b->kind == ast::TypeKind::String) ||
        (a->kind == ast::TypeKind::String && b->kind == ast::TypeKind::CString)) {
        return true;
    }

    // cstring ↔ *char 暗黙変換 (FFI用)
    if (a->kind == ast::TypeKind::CString && b->kind == ast::TypeKind::Pointer) {
        if (b->element_type && b->element_type->kind == ast::TypeKind::Char) {
            return true;
        }
    }
    if (b->kind == ast::TypeKind::CString && a->kind == ast::TypeKind::Pointer) {
        if (a->element_type && a->element_type->kind == ast::TypeKind::Char) {
            return true;
        }
    }

    return false;
}

ast::TypePtr TypeChecker::common_type(ast::TypePtr a, ast::TypePtr b) {
    if (a->kind == b->kind)
        return a;

    // float > int
    if (a->is_floating() || b->is_floating()) {
        return a->kind == ast::TypeKind::Double || b->kind == ast::TypeKind::Double
                   ? ast::make_double()
                   : ast::make_float();
    }

    // より大きい整数型
    auto a_info = a->info();
    auto b_info = b->info();
    return a_info.size >= b_info.size ? a : b;
}

std::vector<std::string> TypeChecker::extract_format_variables(const std::string& format_str) {
    std::vector<std::string> variables;
    std::regex placeholder_regex(R"(\{([a-zA-Z_][a-zA-Z0-9_]*)\})");
    std::smatch match;
    std::string::const_iterator search_start(format_str.cbegin());

    while (std::regex_search(search_start, format_str.cend(), match, placeholder_regex)) {
        std::string var_name = match[1];
        if (std::find(variables.begin(), variables.end(), var_name) == variables.end()) {
            variables.push_back(var_name);
        }
        search_start = match.suffix().first;
    }

    return variables;
}

void TypeChecker::error(Span span, const std::string& msg) {
    debug::tc::log(debug::tc::Id::TypeError, msg, debug::Level::Error);
    diagnostics_.emplace_back(DiagKind::Error, span, msg);
}

void TypeChecker::warning(Span span, const std::string& msg) {
    debug::tc::log(debug::tc::Id::TypeError, msg, debug::Level::Warn);
    diagnostics_.emplace_back(DiagKind::Warning, span, msg);
}

// 変数が変更されたことをマーク
void TypeChecker::mark_variable_modified(const std::string& name) {
    modified_variables_.insert(name);
}

// const推奨警告をチェック（関数終了時に呼び出す）
void TypeChecker::check_const_recommendations() {
    for (const auto& [name, span] : non_const_variable_spans_) {
        // 変更されていない変数に対してconst推奨警告
        if (modified_variables_.count(name) == 0) {
            warning(span, "Variable '" + name + "' is never modified, consider using 'const'");
        }
    }
    // 次の関数用にクリア
    modified_variables_.clear();
    non_const_variable_spans_.clear();
}

bool TypeChecker::type_implements_interface(const std::string& type_name,
                                            const std::string& interface_name) {
    // プリミティブ型の組み込みインターフェース
    // Ord: 比較可能な型（数値型、文字型）
    if (interface_name == "Ord") {
        if (type_name == "int" || type_name == "uint" || type_name == "tiny" ||
            type_name == "utiny" || type_name == "short" || type_name == "ushort" ||
            type_name == "long" || type_name == "ulong" || type_name == "float" ||
            type_name == "double" || type_name == "char") {
            return true;
        }
    }

    // Eq: 等価比較可能な型
    if (interface_name == "Eq") {
        if (type_name == "int" || type_name == "uint" || type_name == "tiny" ||
            type_name == "utiny" || type_name == "short" || type_name == "ushort" ||
            type_name == "long" || type_name == "ulong" || type_name == "float" ||
            type_name == "double" || type_name == "char" || type_name == "bool" ||
            type_name == "string") {
            return true;
        }
    }

    // Clone: コピー可能な型
    if (interface_name == "Clone") {
        if (type_name == "int" || type_name == "uint" || type_name == "tiny" ||
            type_name == "utiny" || type_name == "short" || type_name == "ushort" ||
            type_name == "long" || type_name == "ulong" || type_name == "float" ||
            type_name == "double" || type_name == "char" || type_name == "bool" ||
            type_name == "string") {
            return true;
        }
    }

    // 明示的なimpl実装をチェック
    auto it = impl_interfaces_.find(type_name);
    if (it != impl_interfaces_.end()) {
        if (it->second.count(interface_name)) {
            return true;
        }
    }

    // with による自動実装をチェック
    if (has_auto_impl(type_name, interface_name)) {
        return true;
    }

    return false;
}

bool TypeChecker::check_type_constraints(const std::string& type_name,
                                         const std::vector<std::string>& constraints) {
    for (const auto& constraint : constraints) {
        if (!type_implements_interface(type_name, constraint)) {
            return false;
        }
    }
    return true;
}

void TypeChecker::mark_variable_initialized(const std::string& name) {
    initialized_variables_.insert(name);
}

void TypeChecker::check_uninitialized_use(const std::string& name, Span span) {
    // 初期化されていない変数の使用を検出
    // 注: 関数パラメータは常に初期化されているとみなす
    auto sym = scopes_.current().lookup(name);
    if (!sym) {
        return;  // 変数が見つからない場合は他のエラー処理に任せる
    }

    // initialized_variables_に含まれていない場合は警告
    if (initialized_variables_.count(name) == 0) {
        warning(span, "Variable '" + name + "' may be used before initialization");
    }
}

}  // namespace cm
