// ============================================================
// TypeChecker 実装 - ユーティリティ関数
// ============================================================

#include "../type_checker.hpp"

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

// type_implements_interface: 型が指定されたインターフェースを実装しているかチェック
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

}  // namespace cm
