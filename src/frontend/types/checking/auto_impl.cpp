// ============================================================
// TypeChecker 実装 - 自動実装 (with キーワード)
// ============================================================

#include "../type_checker.hpp"

namespace cm {

void TypeChecker::register_auto_impl(const ast::StructDecl& st, const std::string& iface_name) {
    if (interface_names_.find(iface_name) == interface_names_.end()) {
        error(current_span_, "Unknown interface '" + iface_name + "' in 'with' clause");
        return;
    }

    std::string struct_name = st.name;

    impl_interfaces_[struct_name].insert(iface_name);

    debug::tc::log(debug::tc::Id::Resolved,
                   "Auto-implementing " + iface_name + " for " + struct_name, debug::Level::Debug);

    // Eq: 全フィールドの == 比較
    if (iface_name == "Eq") {
        register_auto_eq_impl(st);
    }
    // Ord: 全フィールドの < 比較（辞書順）
    else if (iface_name == "Ord") {
        register_auto_ord_impl(st);
    }
    // Copy: マーカー（特に処理なし）
    else if (iface_name == "Copy") {
        // マーカーインターフェース、コード生成なし
    }
    // Clone: clone() メソッド
    else if (iface_name == "Clone") {
        register_auto_clone_impl(st);
    }
    // Hash: hash() メソッド
    else if (iface_name == "Hash") {
        register_auto_hash_impl(st);
    }
    // Debug: debug() メソッド
    else if (iface_name == "Debug") {
        register_auto_debug_impl(st);
    }
    // Display: toString() メソッド
    else if (iface_name == "Display") {
        register_auto_display_impl(st);
    }
    // Css: css()/isCss() メソッド
    else if (iface_name == "Css") {
        register_auto_css_impl(st);
    }
}

void TypeChecker::register_auto_eq_impl(const ast::StructDecl& st) {
    std::string struct_name = st.name;
    auto struct_type = ast::make_named(struct_name);

    // 演算子情報を登録
    MethodInfo eq_op;
    eq_op.name = "==";
    eq_op.return_type = std::make_shared<ast::Type>(ast::TypeKind::Bool);
    eq_op.param_types.push_back(struct_type);
    type_methods_[struct_name]["operator=="] = eq_op;

    // 自動導出される != も登録
    MethodInfo ne_op;
    ne_op.name = "!=";
    ne_op.return_type = std::make_shared<ast::Type>(ast::TypeKind::Bool);
    ne_op.param_types.push_back(struct_type);
    type_methods_[struct_name]["operator!="] = ne_op;

    // 実装情報を保存（HIR/MIR生成で使用）
    auto_impl_info_[struct_name]["Eq"] = true;

    debug::tc::log(debug::tc::Id::Resolved,
                   "  Generated operator== and operator!= for " + struct_name, debug::Level::Debug);
}

void TypeChecker::register_auto_ord_impl(const ast::StructDecl& st) {
    std::string struct_name = st.name;
    auto struct_type = ast::make_named(struct_name);

    // < 演算子を登録
    MethodInfo lt_op;
    lt_op.name = "<";
    lt_op.return_type = std::make_shared<ast::Type>(ast::TypeKind::Bool);
    lt_op.param_types.push_back(struct_type);
    type_methods_[struct_name]["operator<"] = lt_op;

    // 自動導出される >, <=, >= も登録
    for (const char* op_name : {"operator>", "operator<=", "operator>="}) {
        MethodInfo derived_op;
        derived_op.name = op_name;
        derived_op.return_type = std::make_shared<ast::Type>(ast::TypeKind::Bool);
        derived_op.param_types.push_back(struct_type);
        type_methods_[struct_name][op_name] = derived_op;
    }

    auto_impl_info_[struct_name]["Ord"] = true;

    debug::tc::log(debug::tc::Id::Resolved, "  Generated operator<, >, <=, >= for " + struct_name,
                   debug::Level::Debug);
}

void TypeChecker::register_auto_clone_impl(const ast::StructDecl& st) {
    std::string struct_name = st.name;
    auto struct_type = ast::make_named(struct_name);

    MethodInfo clone_method;
    clone_method.name = "clone";
    clone_method.return_type = struct_type;
    type_methods_[struct_name]["clone"] = clone_method;

    // グローバル関数としても登録
    std::string mangled_name = struct_name + "__clone";
    std::vector<ast::TypePtr> param_types;
    param_types.push_back(struct_type);  // self
    scopes_.global().define_function(mangled_name, std::move(param_types), struct_type);

    auto_impl_info_[struct_name]["Clone"] = true;

    debug::tc::log(debug::tc::Id::Resolved, "  Generated clone() for " + struct_name,
                   debug::Level::Debug);
}

void TypeChecker::register_auto_hash_impl(const ast::StructDecl& st) {
    std::string struct_name = st.name;
    auto struct_type = ast::make_named(struct_name);

    MethodInfo hash_method;
    hash_method.name = "hash";
    hash_method.return_type = ast::make_int();
    type_methods_[struct_name]["hash"] = hash_method;

    // グローバル関数としても登録
    std::string mangled_name = struct_name + "__hash";
    std::vector<ast::TypePtr> param_types;
    param_types.push_back(struct_type);  // self
    scopes_.global().define_function(mangled_name, std::move(param_types), ast::make_int());

    auto_impl_info_[struct_name]["Hash"] = true;

    debug::tc::log(debug::tc::Id::Resolved, "  Generated hash() for " + struct_name,
                   debug::Level::Debug);
}

void TypeChecker::register_builtin_interfaces() {
    // Eq<T> - 等価比較
    {
        interface_names_.insert("Eq");
        builtin_interface_generic_params_["Eq"] = {"T"};

        MethodInfo eq_op;
        eq_op.name = "==";
        eq_op.return_type = std::make_shared<ast::Type>(ast::TypeKind::Bool);
        eq_op.param_types.push_back(ast::make_generic_param("T"));
        interface_methods_["Eq"]["=="] = eq_op;

        builtin_derived_operators_["Eq"]["!="] = "==";
    }

    // Ord<T> - 順序比較
    {
        interface_names_.insert("Ord");
        builtin_interface_generic_params_["Ord"] = {"T"};

        MethodInfo lt_op;
        lt_op.name = "<";
        lt_op.return_type = std::make_shared<ast::Type>(ast::TypeKind::Bool);
        lt_op.param_types.push_back(ast::make_generic_param("T"));
        interface_methods_["Ord"]["<"] = lt_op;

        builtin_derived_operators_["Ord"][">"] = "<";
        builtin_derived_operators_["Ord"]["<="] = "<";
        builtin_derived_operators_["Ord"][">="] = "<";
    }

    // Copy - コピー可能（マーカーインターフェース）
    { interface_names_.insert("Copy"); }

    // Clone<T> - 明示的クローン
    {
        interface_names_.insert("Clone");
        builtin_interface_generic_params_["Clone"] = {"T"};

        MethodInfo clone_method;
        clone_method.name = "clone";
        clone_method.return_type = ast::make_generic_param("T");
        interface_methods_["Clone"]["clone"] = clone_method;
    }

    // Hash - ハッシュ値計算
    {
        interface_names_.insert("Hash");

        MethodInfo hash_method;
        hash_method.name = "hash";
        hash_method.return_type = ast::make_int();
        interface_methods_["Hash"]["hash"] = hash_method;
    }

    // Debug - デバッグ出力
    {
        interface_names_.insert("Debug");

        MethodInfo debug_method;
        debug_method.name = "debug";
        debug_method.return_type = ast::make_string();
        interface_methods_["Debug"]["debug"] = debug_method;
    }

    // Display - 文字列化
    {
        interface_names_.insert("Display");

        MethodInfo tostring_method;
        tostring_method.name = "toString";
        tostring_method.return_type = ast::make_string();
        interface_methods_["Display"]["toString"] = tostring_method;
    }

    // Css - CSS文字列化
    {
        interface_names_.insert("Css");

        MethodInfo css_method;
        css_method.name = "css";
        css_method.return_type = ast::make_string();
        interface_methods_["Css"]["css"] = css_method;

        // to_css() は css() のエイリアス
        MethodInfo to_css_method;
        to_css_method.name = "to_css";
        to_css_method.return_type = ast::make_string();
        interface_methods_["Css"]["to_css"] = to_css_method;

        MethodInfo is_css_method;
        is_css_method.name = "isCss";
        is_css_method.return_type = ast::make_bool();
        interface_methods_["Css"]["isCss"] = is_css_method;
    }

    debug::tc::log(debug::tc::Id::Resolved,
                   "Registered builtin interfaces: Eq, Ord, Copy, Clone, Hash, Debug, Display, Css",
                   debug::Level::Debug);
}

void TypeChecker::register_auto_debug_impl(const ast::StructDecl& st) {
    std::string struct_name = st.name;
    auto struct_type = ast::make_named(struct_name);

    MethodInfo debug_method;
    debug_method.name = "debug";
    debug_method.return_type = ast::make_string();
    type_methods_[struct_name]["debug"] = debug_method;

    // グローバル関数としても登録
    std::string mangled_name = struct_name + "__debug";
    std::vector<ast::TypePtr> param_types;
    param_types.push_back(struct_type);  // self
    scopes_.global().define_function(mangled_name, std::move(param_types), ast::make_string());

    auto_impl_info_[struct_name]["Debug"] = true;

    debug::tc::log(debug::tc::Id::Resolved, "  Generated debug() for " + struct_name,
                   debug::Level::Debug);
}

void TypeChecker::register_auto_display_impl(const ast::StructDecl& st) {
    std::string struct_name = st.name;
    auto struct_type = ast::make_named(struct_name);

    MethodInfo tostring_method;
    tostring_method.name = "toString";
    tostring_method.return_type = ast::make_string();
    type_methods_[struct_name]["toString"] = tostring_method;

    // グローバル関数としても登録
    std::string mangled_name = struct_name + "__toString";
    std::vector<ast::TypePtr> param_types;
    param_types.push_back(struct_type);  // self
    scopes_.global().define_function(mangled_name, std::move(param_types), ast::make_string());

    auto_impl_info_[struct_name]["Display"] = true;

    debug::tc::log(debug::tc::Id::Resolved, "  Generated toString() for " + struct_name,
                   debug::Level::Debug);
}

void TypeChecker::register_auto_css_impl(const ast::StructDecl& st) {
    std::string struct_name = st.name;
    auto struct_type = ast::make_named(struct_name);

    MethodInfo css_method;
    css_method.name = "css";
    css_method.return_type = ast::make_string();
    type_methods_[struct_name]["css"] = css_method;

    // to_css() は css() のエイリアス（同じマングル名 __css を使用）
    MethodInfo to_css_method;
    to_css_method.name = "to_css";
    to_css_method.return_type = ast::make_string();
    type_methods_[struct_name]["to_css"] = to_css_method;

    MethodInfo is_css_method;
    is_css_method.name = "isCss";
    is_css_method.return_type = ast::make_bool();
    type_methods_[struct_name]["isCss"] = is_css_method;

    {
        std::string mangled_name = struct_name + "__css";
        std::vector<ast::TypePtr> param_types;
        param_types.push_back(struct_type);  // self
        scopes_.global().define_function(mangled_name, std::move(param_types), ast::make_string());
    }
    {
        // to_css も同じ __css にマングルされる
        std::string mangled_name = struct_name + "__to_css";
        std::vector<ast::TypePtr> param_types;
        param_types.push_back(struct_type);  // self
        scopes_.global().define_function(mangled_name, std::move(param_types), ast::make_string());
    }
    {
        std::string mangled_name = struct_name + "__isCss";
        std::vector<ast::TypePtr> param_types;
        param_types.push_back(struct_type);  // self
        scopes_.global().define_function(mangled_name, std::move(param_types), ast::make_bool());
    }

    auto_impl_info_[struct_name]["Css"] = true;

    debug::tc::log(debug::tc::Id::Resolved, "  Generated css()/to_css()/isCss() for " + struct_name,
                   debug::Level::Debug);
}

void TypeChecker::register_builtin_types() {
    // ============================================================
    // Result<T, E> - 成功/失敗を表すenum
    // ============================================================
    {
        // ジェネリックenumとして登録
        generic_enums_["Result"] = {"T", "E"};
        enum_names_.insert("Result");

        // バリアントを登録: Ok(T), Err(E)
        enum_values_["Result::Ok"] = 0;
        enum_values_["Result::Err"] = 1;

        // メソッドを登録
        // is_ok() -> bool
        MethodInfo is_ok;
        is_ok.name = "is_ok";
        is_ok.return_type = ast::make_bool();
        type_methods_["Result"]["is_ok"] = is_ok;

        // is_err() -> bool
        MethodInfo is_err;
        is_err.name = "is_err";
        is_err.return_type = ast::make_bool();
        type_methods_["Result"]["is_err"] = is_err;

        // unwrap() -> T
        MethodInfo unwrap;
        unwrap.name = "unwrap";
        unwrap.return_type = ast::make_generic_param("T");
        type_methods_["Result"]["unwrap"] = unwrap;

        // unwrap_or(default: T) -> T
        MethodInfo unwrap_or;
        unwrap_or.name = "unwrap_or";
        unwrap_or.return_type = ast::make_generic_param("T");
        unwrap_or.param_types.push_back(ast::make_generic_param("T"));
        type_methods_["Result"]["unwrap_or"] = unwrap_or;

        // unwrap_err() -> E
        MethodInfo unwrap_err;
        unwrap_err.name = "unwrap_err";
        unwrap_err.return_type = ast::make_generic_param("E");
        type_methods_["Result"]["unwrap_err"] = unwrap_err;
    }

    // ============================================================
    // Option<T> - 値の有無を表すenum
    // ============================================================
    {
        // ジェネリックenumとして登録
        generic_enums_["Option"] = {"T"};
        enum_names_.insert("Option");

        // バリアントを登録: Some(T), None
        enum_values_["Option::Some"] = 0;
        enum_values_["Option::None"] = 1;

        // メソッドを登録
        // is_some() -> bool
        MethodInfo is_some;
        is_some.name = "is_some";
        is_some.return_type = ast::make_bool();
        type_methods_["Option"]["is_some"] = is_some;

        // is_none() -> bool
        MethodInfo is_none;
        is_none.name = "is_none";
        is_none.return_type = ast::make_bool();
        type_methods_["Option"]["is_none"] = is_none;

        // unwrap() -> T
        MethodInfo unwrap;
        unwrap.name = "unwrap";
        unwrap.return_type = ast::make_generic_param("T");
        type_methods_["Option"]["unwrap"] = unwrap;

        // unwrap_or(default: T) -> T
        MethodInfo unwrap_or;
        unwrap_or.name = "unwrap_or";
        unwrap_or.return_type = ast::make_generic_param("T");
        unwrap_or.param_types.push_back(ast::make_generic_param("T"));
        type_methods_["Option"]["unwrap_or"] = unwrap_or;
    }

    debug::tc::log(debug::tc::Id::Resolved, "Registered builtin types: Result<T, E>, Option<T>",
                   debug::Level::Debug);
}

}  // namespace cm
