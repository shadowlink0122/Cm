// ============================================================
// TypeChecker 実装 - ユーティリティ関数
// ============================================================

#include "../type_checker.hpp"

#include <algorithm>
#include <cctype>
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
    // Lint警告が無効な場合はスキップ（クリアのみ実行）
    if (!enable_lint_warnings_) {
        modified_variables_.clear();
        non_const_variable_spans_.clear();
        return;
    }

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

// 未使用変数チェック (W001)
void TypeChecker::check_unused_variables() {
    // 現在のスコープから未使用シンボルを取得
    auto unused_symbols = scopes_.current().get_unused_symbols();

    for (const auto& sym : unused_symbols) {
        // パラメータ名がアンダースコアで始まる場合は警告しない（意図的な未使用）
        if (!sym.name.empty() && sym.name[0] == '_') {
            continue;
        }
        // self は常に警告しない
        if (sym.name == "self") {
            continue;
        }

        warning(sym.span, "Variable '" + sym.name + "' is never used [W001]");
    }
}

// ============================================================
// 命名規則チェック (L100-L103)
// ============================================================

// snake_case判定: 小文字、数字、アンダースコアのみ、先頭は小文字またはアンダースコア
bool TypeChecker::is_snake_case(const std::string& name) {
    if (name.empty())
        return true;

    // 先頭は小文字またはアンダースコア
    if (!std::islower(name[0]) && name[0] != '_') {
        return false;
    }

    for (char c : name) {
        if (!std::islower(c) && !std::isdigit(c) && c != '_') {
            return false;
        }
    }
    return true;
}

// PascalCase判定: 先頭が大文字、アンダースコアなし
bool TypeChecker::is_pascal_case(const std::string& name) {
    if (name.empty())
        return true;

    // 先頭は大文字
    if (!std::isupper(name[0])) {
        return false;
    }

    // アンダースコアは禁止
    for (char c : name) {
        if (c == '_') {
            return false;
        }
    }
    return true;
}

// UPPER_SNAKE_CASE判定: 大文字、数字、アンダースコアのみ
bool TypeChecker::is_upper_snake_case(const std::string& name) {
    if (name.empty())
        return true;

    for (char c : name) {
        if (!std::isupper(c) && !std::isdigit(c) && c != '_') {
            return false;
        }
    }
    return true;
}

// 命名規則をチェック（関数終了時に呼び出す）
void TypeChecker::check_naming_conventions() {
    // 現在のスコープの変数をチェック
    // Note: 関数名や構造体名のチェックはregister_declaration時に行う
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
    // Lint警告が無効な場合はスキップ
    if (!enable_lint_warnings_) {
        return;
    }

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

// ============================================================
// コンパイル時定数評価（const強化）
// ============================================================
std::optional<int64_t> TypeChecker::evaluate_const_expr(ast::Expr& expr) {
    // リテラル: 整数値を直接返す
    if (auto* lit = expr.as<ast::LiteralExpr>()) {
        if (std::holds_alternative<int64_t>(lit->value)) {
            return std::get<int64_t>(lit->value);
        }
        // bool値もint64_tに変換可能
        if (std::holds_alternative<bool>(lit->value)) {
            return std::get<bool>(lit->value) ? 1 : 0;
        }
        // double/charは整数として評価不可
        return std::nullopt;
    }

    // 識別子: const変数を検索
    if (auto* ident = expr.as<ast::IdentExpr>()) {
        auto sym = scopes_.current().lookup(ident->name);
        if (sym && sym->is_const && sym->const_int_value.has_value()) {
            return sym->const_int_value;
        }
        return std::nullopt;
    }

    // 単項演算子
    if (auto* unary = expr.as<ast::UnaryExpr>()) {
        auto val = evaluate_const_expr(*unary->operand);
        if (!val)
            return std::nullopt;

        switch (unary->op) {
            case ast::UnaryOp::Neg:
                return -(*val);
            case ast::UnaryOp::Not:
                return (*val) ? 0 : 1;
            case ast::UnaryOp::BitNot:
                return ~(*val);
            default:
                return std::nullopt;
        }
    }

    // 二項演算子
    if (auto* binary = expr.as<ast::BinaryExpr>()) {
        auto left_val = evaluate_const_expr(*binary->left);
        auto right_val = evaluate_const_expr(*binary->right);
        if (!left_val || !right_val)
            return std::nullopt;

        switch (binary->op) {
            case ast::BinaryOp::Add:
                return *left_val + *right_val;
            case ast::BinaryOp::Sub:
                return *left_val - *right_val;
            case ast::BinaryOp::Mul:
                return *left_val * *right_val;
            case ast::BinaryOp::Div:
                if (*right_val == 0)
                    return std::nullopt;  // ゼロ除算
                return *left_val / *right_val;
            case ast::BinaryOp::Mod:
                if (*right_val == 0)
                    return std::nullopt;
                return *left_val % *right_val;
            case ast::BinaryOp::BitAnd:
                return *left_val & *right_val;
            case ast::BinaryOp::BitOr:
                return *left_val | *right_val;
            case ast::BinaryOp::BitXor:
                return *left_val ^ *right_val;
            case ast::BinaryOp::Shl:
                return *left_val << *right_val;
            case ast::BinaryOp::Shr:
                return *left_val >> *right_val;
            case ast::BinaryOp::Lt:
                return (*left_val < *right_val) ? 1 : 0;
            case ast::BinaryOp::Le:
                return (*left_val <= *right_val) ? 1 : 0;
            case ast::BinaryOp::Gt:
                return (*left_val > *right_val) ? 1 : 0;
            case ast::BinaryOp::Ge:
                return (*left_val >= *right_val) ? 1 : 0;
            case ast::BinaryOp::Eq:
                return (*left_val == *right_val) ? 1 : 0;
            case ast::BinaryOp::Ne:
                return (*left_val != *right_val) ? 1 : 0;
            case ast::BinaryOp::And:
                return (*left_val && *right_val) ? 1 : 0;
            case ast::BinaryOp::Or:
                return (*left_val || *right_val) ? 1 : 0;
            default:
                return std::nullopt;
        }
    }

    // 三項演算子
    if (auto* ternary = expr.as<ast::TernaryExpr>()) {
        auto cond = evaluate_const_expr(*ternary->condition);
        if (!cond)
            return std::nullopt;
        return *cond ? evaluate_const_expr(*ternary->then_expr)
                     : evaluate_const_expr(*ternary->else_expr);
    }

    // その他の式はコンパイル時評価不可
    return std::nullopt;
}

// ============================================================
// 配列サイズのsize_param_name解決（const強化）
// ============================================================
void TypeChecker::resolve_array_size(ast::TypePtr& type) {
    if (!type)
        return;

    // 配列型でsize_param_nameが設定されている場合
    if (type->kind == ast::TypeKind::Array && !type->size_param_name.empty()) {
        // const変数を検索
        auto sym = scopes_.current().lookup(type->size_param_name);
        if (sym && sym->is_const && sym->const_int_value.has_value()) {
            int64_t size = *sym->const_int_value;
            if (size > 0 && size <= INT32_MAX) {
                type->array_size = static_cast<uint32_t>(size);
                type->size_param_name.clear();  // 解決済みなのでクリア

                debug::tc::log(debug::tc::Id::TypeInfer,
                               "Resolved array size: " + sym->name + " = " + std::to_string(size),
                               debug::Level::Debug);
            } else {
                error(current_span_, "Array size must be a positive integer, got " +
                                         std::to_string(size) + " for '" + type->size_param_name +
                                         "'");
            }
        } else if (sym && !sym->is_const) {
            error(current_span_, "Array size must be a const variable, but '" +
                                     type->size_param_name + "' is not const");
        } else if (!sym) {
            error(current_span_,
                  "Undefined variable '" + type->size_param_name + "' used as array size");
        } else {
            error(current_span_, "Const variable '" + type->size_param_name +
                                     "' does not have a compile-time integer value");
        }
    }

    // 要素型も再帰的に解決
    if (type->element_type) {
        resolve_array_size(type->element_type);
    }
}

}  // namespace cm
