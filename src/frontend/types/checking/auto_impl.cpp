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

    debug::tc::log(debug::tc::Id::Resolved,
                   "Registered builtin interfaces: Eq, Ord, Copy, Clone, Hash",
                   debug::Level::Debug);
}

}  // namespace cm
