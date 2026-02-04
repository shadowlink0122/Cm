// ============================================================
// TypeChecker 実装 - 宣言の登録・チェック
// ============================================================

#include "../type_checker.hpp"

#include <algorithm>

namespace cm {

TypeChecker::TypeChecker() {
    register_builtin_interfaces();
}

bool TypeChecker::check(ast::Program& program) {
    debug::tc::log(debug::tc::Id::Start);

    // Pass 1: 関数シグネチャを登録
    for (auto& decl : program.declarations) {
        register_declaration(*decl);
    }

    // Pass 2: 関数本体をチェック
    for (auto& decl : program.declarations) {
        check_declaration(*decl);
    }

    debug::tc::log(debug::tc::Id::End, std::to_string(diagnostics_.size()) + " issues");
    return !has_errors();
}

bool TypeChecker::has_errors() const {
    for (const auto& d : diagnostics_) {
        if (d.severity == DiagKind::Error)
            return true;
    }
    return false;
}

void TypeChecker::register_struct(const std::string& name, const ast::StructDecl& decl) {
    struct_defs_[name] = &decl;
}

const ast::StructDecl* TypeChecker::get_struct(const std::string& name) const {
    auto it = struct_defs_.find(name);
    if (it != struct_defs_.end()) {
        return it->second;
    }

    auto td_it = typedef_defs_.find(name);
    if (td_it != typedef_defs_.end() && td_it->second) {
        std::string actual_name = td_it->second->name;
        auto struct_it = struct_defs_.find(actual_name);
        if (struct_it != struct_defs_.end()) {
            return struct_it->second;
        }
    }

    return nullptr;
}

ast::TypePtr TypeChecker::get_default_member_type(const std::string& struct_name) const {
    const ast::StructDecl* decl = get_struct(struct_name);
    if (!decl)
        return nullptr;
    for (const auto& field : decl->fields) {
        if (field.is_default) {
            return field.type;
        }
    }
    return nullptr;
}

std::string TypeChecker::get_default_member_name(const std::string& struct_name) const {
    const ast::StructDecl* decl = get_struct(struct_name);
    if (!decl)
        return "";
    for (const auto& field : decl->fields) {
        if (field.is_default) {
            return field.name;
        }
    }
    return "";
}

bool TypeChecker::has_auto_impl(const std::string& struct_name,
                                const std::string& iface_name) const {
    auto it = auto_impl_info_.find(struct_name);
    if (it != auto_impl_info_.end()) {
        auto iface_it = it->second.find(iface_name);
        if (iface_it != it->second.end()) {
            return iface_it->second;
        }
    }
    return false;
}

void TypeChecker::register_namespace(ast::ModuleDecl& mod, const std::string& parent_namespace) {
    std::string namespace_name = mod.path.segments.empty() ? "" : mod.path.segments[0];
    std::string full_namespace =
        parent_namespace.empty() ? namespace_name : parent_namespace + "::" + namespace_name;

    debug::tc::log(debug::tc::Id::Resolved, "Processing namespace: " + full_namespace,
                   debug::Level::Debug);

    for (auto& inner_decl : mod.declarations) {
        if (auto* nested_mod = inner_decl->as<ast::ModuleDecl>()) {
            register_namespace(*nested_mod, full_namespace);
        } else if (auto* func = inner_decl->as<ast::FunctionDecl>()) {
            std::string original_name = func->name;
            func->name = full_namespace + "::" + original_name;
            register_declaration(*inner_decl);
            func->name = original_name;
        } else if (auto* st = inner_decl->as<ast::StructDecl>()) {
            std::string original_name = st->name;
            st->name = full_namespace + "::" + original_name;
            register_declaration(*inner_decl);
            st->name = original_name;
        } else {
            register_declaration(*inner_decl);
        }
    }
}

void TypeChecker::check_namespace(ast::ModuleDecl& mod, const std::string& parent_namespace) {
    std::string namespace_name = mod.path.segments.empty() ? "" : mod.path.segments[0];
    std::string full_namespace =
        parent_namespace.empty() ? namespace_name : parent_namespace + "::" + namespace_name;

    debug::tc::log(debug::tc::Id::Resolved, "Checking namespace: " + full_namespace,
                   debug::Level::Debug);

    for (auto& inner_decl : mod.declarations) {
        if (auto* nested_mod = inner_decl->as<ast::ModuleDecl>()) {
            check_namespace(*nested_mod, full_namespace);
        } else if (auto* func = inner_decl->as<ast::FunctionDecl>()) {
            std::string original_name = func->name;
            func->name = full_namespace + "::" + original_name;
            check_declaration(*inner_decl);
            func->name = original_name;
        } else if (auto* st = inner_decl->as<ast::StructDecl>()) {
            std::string original_name = st->name;
            st->name = full_namespace + "::" + original_name;
            check_declaration(*inner_decl);
            st->name = original_name;
        } else {
            check_declaration(*inner_decl);
        }
    }
}

void TypeChecker::register_declaration(ast::Decl& decl) {
    if (auto* mod = decl.as<ast::ModuleDecl>()) {
        register_namespace(*mod, "");
        return;
    }

    if (auto* func = decl.as<ast::FunctionDecl>()) {
        if (!func->generic_params.empty()) {
            generic_functions_[func->name] = func->generic_params;
            generic_function_constraints_[func->name] = func->generic_params_v2;
            debug::tc::log(debug::tc::Id::Resolved,
                           "Generic function: " + func->name + " with " +
                               std::to_string(func->generic_params.size()) + " type params",
                           debug::Level::Debug);
        }

        std::vector<ast::TypePtr> param_types;
        size_t required_params = 0;
        for (const auto& p : func->params) {
            param_types.push_back(p.type);
            if (!p.default_value) {
                required_params++;
            }
        }
        scopes_.global().define_function(func->name, std::move(param_types), func->return_type,
                                         required_params);

        // L100: 関数名はsnake_caseであるべき
        // main関数とネームスペース付き関数は除外
        if (enable_lint_warnings_ && func->name != "main" &&
            func->name.find("::") == std::string::npos) {
            if (!is_snake_case(func->name)) {
                // name_spanが設定されていればそれを使用、なければdecl.span
                Span name_pos = func->name_span.is_empty() ? decl.span : func->name_span;
                warning(name_pos, "Function name '" + func->name + "' should be snake_case [L100]");
            }
        }
    } else if (auto* st = decl.as<ast::StructDecl>()) {
        if (!st->generic_params.empty()) {
            generic_structs_[st->name] = st->generic_params;
            debug::tc::log(debug::tc::Id::Resolved,
                           "Generic struct: " + st->name + " with " +
                               std::to_string(st->generic_params.size()) + " type params",
                           debug::Level::Debug);
        }

        scopes_.global().define(st->name, ast::make_named(st->name));
        register_struct(st->name, *st);

        // L103: 型名はPascalCaseであるべき
        if (enable_lint_warnings_ && !is_pascal_case(st->name)) {
            // name_spanが設定されていればそれを使用、なければdecl.span
            Span name_pos = st->name_span.is_empty() ? decl.span : st->name_span;
            warning(name_pos, "Type name '" + st->name + "' should be PascalCase [L103]");
        }

        for (const auto& iface_name : st->auto_impls) {
            register_auto_impl(*st, iface_name);
        }
    } else if (auto* iface = decl.as<ast::InterfaceDecl>()) {
        interface_names_.insert(iface->name);
        scopes_.global().define(iface->name, ast::make_named(iface->name));

        for (const auto& method : iface->methods) {
            MethodInfo info;
            info.return_type = method.return_type;
            for (const auto& param : method.params) {
                info.param_types.push_back(param.type);
            }
            interface_methods_[iface->name][method.name] = info;
        }

        debug::tc::log(debug::tc::Id::Resolved,
                       "Registering interface: " + iface->name + " with " +
                           std::to_string(iface->methods.size()) + " methods",
                       debug::Level::Debug);
    } else if (auto* en = decl.as<ast::EnumDecl>()) {
        register_enum(*en);
    } else if (auto* td = decl.as<ast::TypedefDecl>()) {
        register_typedef(*td);
    } else if (auto* impl = decl.as<ast::ImplDecl>()) {
        register_impl(*impl);
    } else if (auto* gv = decl.as<ast::GlobalVarDecl>()) {
        // グローバル変数/定数の登録（const強化）
        current_span_ = decl.span;
        std::optional<int64_t> const_int_value = std::nullopt;

        // const変数の値を評価
        if (gv->is_const && gv->init_expr) {
            const_int_value = evaluate_const_expr(*gv->init_expr);
            if (const_int_value) {
                debug::tc::log(
                    debug::tc::Id::TypeInfer,
                    "Global const: " + gv->name + " = " + std::to_string(*const_int_value),
                    debug::Level::Debug);
            }
        }

        // 初期化式の型チェック
        ast::TypePtr init_type;
        if (gv->init_expr) {
            init_type = infer_type(*gv->init_expr);
        }

        // 型を決定
        ast::TypePtr var_type = gv->type ? resolve_typedef(gv->type) : init_type;
        if (var_type) {
            scopes_.global().define(gv->name, var_type, gv->is_const, false, decl.span,
                                    const_int_value);
            debug::tc::log(debug::tc::Id::Resolved,
                           "Global " + std::string(gv->is_const ? "const" : "var") + ": " +
                               gv->name + " : " + ast::type_to_string(*var_type),
                           debug::Level::Debug);
        }
    } else if (auto* macro = decl.as<ast::MacroDecl>()) {
        // v0.13.0: 型付きマクロを処理
        if (macro->kind == ast::MacroDecl::Kind::Constant) {
            current_span_ = decl.span;

            // v0.13.0: ラムダ式マクロの場合は関数として登録
            if (macro->value) {
                if (auto* lambda = macro->value->as<ast::LambdaExpr>()) {
                    // パラメータ型を収集
                    std::vector<ast::TypePtr> param_types;
                    for (const auto& param : lambda->params) {
                        param_types.push_back(param.type);
                    }

                    // 戻り値型を決定
                    ast::TypePtr return_type = lambda->return_type;
                    if (!return_type) {
                        // 式本体の場合、式の型を推論するためにスコープを作成
                        if (lambda->is_expr_body()) {
                            // 一時的なスコープを作成してパラメータを登録
                            scopes_.push();
                            for (const auto& param : lambda->params) {
                                scopes_.current().define(param.name, param.type, false, false,
                                                         decl.span, std::nullopt);
                            }
                            return_type = infer_type(*std::get<ast::ExprPtr>(lambda->body));
                            scopes_.pop();
                        } else {
                            return_type = ast::make_void();
                        }
                    }

                    // 関数として登録
                    scopes_.global().define_function(macro->name, std::move(param_types),
                                                     return_type);
                    debug::tc::log(debug::tc::Id::Resolved,
                                   "Macro function: " + macro->name + " -> " +
                                       ast::type_to_string(*return_type),
                                   debug::Level::Debug);
                    return;  // 処理完了
                }
            }

            // リテラル定数マクロの場合
            std::optional<int64_t> const_int_value = std::nullopt;

            // マクロの値を評価
            if (macro->value) {
                const_int_value = evaluate_const_expr(*macro->value);
                if (const_int_value) {
                    debug::tc::log(
                        debug::tc::Id::TypeInfer,
                        "Macro const: " + macro->name + " = " + std::to_string(*const_int_value),
                        debug::Level::Debug);
                }
            }

            // 初期化式の型チェック
            ast::TypePtr init_type;
            if (macro->value) {
                init_type = infer_type(*macro->value);
            }

            // 型を決定
            ast::TypePtr var_type = macro->type ? resolve_typedef(macro->type) : init_type;
            if (var_type) {
                scopes_.global().define(macro->name, var_type, true /* is_const */, false,
                                        decl.span, const_int_value);
                debug::tc::log(
                    debug::tc::Id::Resolved,
                    "Macro const: " + macro->name + " : " + ast::type_to_string(*var_type),
                    debug::Level::Debug);
            }
        }
    } else if (auto* extern_block = decl.as<ast::ExternBlockDecl>()) {
        for (const auto& func : extern_block->declarations) {
            std::vector<ast::TypePtr> param_types;
            for (const auto& p : func->params) {
                param_types.push_back(p.type);
            }
            scopes_.global().define_function(func->name, std::move(param_types), func->return_type);
        }
    } else if (auto* use_decl = decl.as<ast::UseDecl>()) {
        // FFI use宣言を処理
        if (use_decl->kind == ast::UseDecl::FFIUse) {
            for (const auto& ffi_func : use_decl->ffi_funcs) {
                std::vector<ast::TypePtr> param_types;
                for (const auto& [name, type] : ffi_func.params) {
                    param_types.push_back(type);
                }
                // 可変長引数フラグを設定してFFI関数を登録
                scopes_.global().define_function(ffi_func.name, std::move(param_types),
                                                 ffi_func.return_type, SIZE_MAX,
                                                 ffi_func.is_variadic);
            }
        }
    } else if (auto* import = decl.as<ast::ImportDecl>()) {
        // Pass 1でimportも処理してprintlnを登録
        check_import(*import);
    }
}

void TypeChecker::check_declaration(ast::Decl& decl) {
    debug::tc::log(debug::tc::Id::CheckDecl, "", debug::Level::Trace);

    if (auto* mod = decl.as<ast::ModuleDecl>()) {
        check_namespace(*mod, "");
        return;
    }

    if (auto* func = decl.as<ast::FunctionDecl>()) {
        check_function(*func);
    } else if (auto* st = decl.as<ast::StructDecl>()) {
        current_span_ = decl.span;
        bool is_css_struct =
            std::find(st->auto_impls.begin(), st->auto_impls.end(), "Css") != st->auto_impls.end();
        if (is_css_struct) {
            for (const auto& field : st->fields) {
                if (!field.type) {
                    continue;
                }
                auto resolved_type = resolve_typedef(field.type);
                if (!resolved_type) {
                    continue;
                }
                if (resolved_type->kind == ast::TypeKind::Struct) {
                    const std::string& type_name = resolved_type->name;
                    if (!type_implements_interface(type_name, "Css") &&
                        !has_auto_impl(type_name, "Css")) {
                        error(current_span_, "Nested css field '" + field.name +
                                                 "' requires type '" + type_name +
                                                 "' to implement Css");
                    }
                }
            }
        }
    } else if (auto* import = decl.as<ast::ImportDecl>()) {
        check_import(*import);
    } else if (auto* impl = decl.as<ast::ImplDecl>()) {
        check_impl(*impl);
    }
}

void TypeChecker::register_impl(ast::ImplDecl& impl) {
    if (!impl.target_type)
        return;

    std::string type_name = ast::type_to_string(*impl.target_type);

    // コンストラクタ/デストラクタの登録（is_ctor_implの場合）
    if (impl.is_ctor_impl) {
        for (const auto& ctor : impl.constructors) {
            std::string mangled_name = type_name + "__ctor";
            if (ctor->is_overload) {
                mangled_name += "_" + std::to_string(ctor->params.size());
            }
            std::vector<ast::TypePtr> param_types;
            param_types.push_back(impl.target_type);
            for (const auto& param : ctor->params) {
                param_types.push_back(param.type);
            }
            scopes_.global().define_function(mangled_name, std::move(param_types),
                                             ast::make_void());
        }
        if (impl.destructor) {
            std::string mangled_name = type_name + "__dtor";
            std::vector<ast::TypePtr> param_types;
            param_types.push_back(impl.target_type);
            scopes_.global().define_function(mangled_name, std::move(param_types),
                                             ast::make_void());
        }
        // 早期リターンを削除: メソッドも登録を続行
    }

    if (!impl.interface_name.empty()) {
        if (impl_interfaces_[type_name].count(impl.interface_name) > 0) {
            throw std::runtime_error("Duplicate impl: " + type_name + " already implements " +
                                     impl.interface_name);
        }
        impl_interfaces_[type_name].insert(impl.interface_name);
        debug::tc::log(debug::tc::Id::Resolved, type_name + " implements " + impl.interface_name,
                       debug::Level::Debug);
    }

    for (const auto& method : impl.methods) {
        if (type_methods_[type_name].count(method->name) > 0) {
            throw std::runtime_error("Duplicate method: " + type_name + " already has method '" +
                                     method->name + "'");
        }

        MethodInfo info;
        info.name = method->name;
        info.return_type = method->return_type;
        info.visibility = method->visibility;
        info.is_static = method->is_static;  // 静的メソッドフラグを設定
        for (const auto& param : method->params) {
            info.param_types.push_back(param.type);
        }
        type_methods_[type_name][method->name] = std::move(info);

        std::string mangled_name = type_name + "__" + method->name;
        std::vector<ast::TypePtr> all_param_types;
        all_param_types.push_back(impl.target_type);
        for (const auto& param : method->params) {
            all_param_types.push_back(param.type);
        }
        scopes_.global().define_function(mangled_name, std::move(all_param_types),
                                         method->return_type);
    }
}

void TypeChecker::check_impl(ast::ImplDecl& impl) {
    if (!impl.target_type)
        return;

    std::string type_name = ast::type_to_string(*impl.target_type);

    if (!impl.interface_name.empty()) {
        impl_interfaces_[type_name].insert(impl.interface_name);
        debug::tc::log(debug::tc::Id::Resolved, type_name + " implements " + impl.interface_name,
                       debug::Level::Debug);
    }

    // コンストラクタ/デストラクタのチェック
    if (impl.is_ctor_impl) {
        for (auto& ctor : impl.constructors) {
            scopes_.push();
            current_return_type_ = ast::make_void();
            scopes_.current().define("self", impl.target_type, false);
            mark_variable_initialized("self");  // selfは常に初期化済み
            for (const auto& param : ctor->params) {
                scopes_.current().define(param.name, param.type, param.qualifiers.is_const);
                mark_variable_initialized(param.name);  // パラメータは常に初期化済み
            }
            for (auto& stmt : ctor->body) {
                check_statement(*stmt);
            }
            check_const_recommendations();
            initialized_variables_.clear();
            scopes_.pop();
        }

        if (impl.destructor) {
            scopes_.push();
            current_return_type_ = ast::make_void();
            scopes_.current().define("self", impl.target_type, false);
            mark_variable_initialized("self");  // selfは常に初期化済み
            for (auto& stmt : impl.destructor->body) {
                check_statement(*stmt);
            }
            check_const_recommendations();
            initialized_variables_.clear();
            scopes_.pop();
        }

        // 早期リターンを削除: メソッドのチェックも続行
    }

    current_impl_target_type_ = type_name;

    for (auto& method : impl.methods) {
        scopes_.push();
        current_return_type_ = method->return_type;
        scopes_.current().define("self", impl.target_type, false);
        mark_variable_initialized("self");  // selfは常に初期化済み
        for (const auto& param : method->params) {
            scopes_.current().define(param.name, param.type, param.qualifiers.is_const);
            mark_variable_initialized(param.name);  // パラメータは常に初期化済み
        }
        for (auto& stmt : method->body) {
            check_statement(*stmt);
        }
        check_const_recommendations();   // const推奨警告をチェック
        initialized_variables_.clear();  // 次のメソッド用にクリア
        scopes_.pop();
    }
    current_return_type_ = nullptr;
    current_impl_target_type_.clear();
}

void TypeChecker::register_enum(ast::EnumDecl& en) {
    debug::tc::log(debug::tc::Id::Resolved, "Registering enum: " + en.name, debug::Level::Debug);

    enum_names_.insert(en.name);

    // ジェネリックenumの場合は型パラメータを登録
    if (!en.generic_params.empty()) {
        generic_enums_[en.name] = en.generic_params;
        debug::tc::log(debug::tc::Id::Resolved,
                       "Generic enum: " + en.name + " with " +
                           std::to_string(en.generic_params.size()) + " type params",
                       debug::Level::Debug);
    }

    // 基本型として登録
    scopes_.global().define(en.name, ast::make_named(en.name));

    // Tagged Union情報を保存
    enum_defs_[en.name] = &en;

    for (const auto& member : en.members) {
        std::string full_name = en.name + "::" + member.name;

        if (member.has_data()) {
            // Associated dataを持つVariant: コンストラクタ関数として登録
            std::vector<ast::TypePtr> param_types;
            for (const auto& [field_name, field_type] : member.fields) {
                param_types.push_back(field_type);
            }

            // 戻り値型はenum型
            ast::TypePtr return_type = ast::make_named(en.name);

            scopes_.global().define_function(full_name, std::move(param_types), return_type);

            debug::tc::log(debug::tc::Id::Resolved,
                           "  " + full_name + "(...) -> " + en.name + " [variant constructor]",
                           debug::Level::Debug);
        } else {
            // シンプルなVariant: 整数定数として登録
            int64_t value = member.value.value_or(0);
            enum_values_[full_name] = value;
            scopes_.global().define(full_name, ast::make_int());

            debug::tc::log(debug::tc::Id::Resolved,
                           "  " + full_name + " = " + std::to_string(value), debug::Level::Debug);
        }
    }
}

void TypeChecker::register_typedef(ast::TypedefDecl& td) {
    debug::tc::log(debug::tc::Id::Resolved, "Registering typedef: " + td.name, debug::Level::Debug);
    scopes_.global().define(td.name, td.type);
    typedef_defs_[td.name] = td.type;
}

void TypeChecker::check_import(ast::ImportDecl& import) {
    std::string path_str = import.path.to_string();

    // std::io からのインポート
    if (path_str == "std::io") {
        for (const auto& item : import.items) {
            if (item.name == "println" || item.name.empty()) {
                register_println();
            }
            if (item.name == "print" || item.name.empty()) {
                register_print();
            }
        }
    } else if (import.path.segments.size() >= 3 && import.path.segments[0] == "std" &&
               import.path.segments[1] == "io") {
        // std::io::println / std::io::print
        if (import.path.segments[2] == "println") {
            register_println();
        } else if (import.path.segments[2] == "print") {
            register_print();
        }
    }
}

void TypeChecker::register_println() {
    // printlnは可変引数で、0個以上の引数を取る
    scopes_.global().define_function("println", {}, ast::make_void(), 0);
}

void TypeChecker::register_print() {
    // printは1個の引数を取る
    scopes_.global().define_function("print", {ast::make_void()}, ast::make_void(), 1);
}

void TypeChecker::check_function(ast::FunctionDecl& func) {
    scopes_.push();

    generic_context_.clear();
    if (!func.generic_params.empty()) {
        for (const auto& param : func.generic_params) {
            generic_context_.add_type_param(param);
            scopes_.current().define(param, ast::make_named(param));
            debug::tc::log(debug::tc::Id::Resolved, "Added generic type param: " + param,
                           debug::Level::Trace);
        }
    }

    current_return_type_ = resolve_typedef(func.return_type);
    if (generic_context_.has_type_param(ast::type_to_string(*func.return_type))) {
        current_return_type_ = func.return_type;
    }

    for (const auto& param : func.params) {
        auto resolved_type = resolve_typedef(param.type);
        if (generic_context_.has_type_param(ast::type_to_string(*param.type))) {
            resolved_type = param.type;
        }
        scopes_.current().define(param.name, resolved_type, param.qualifiers.is_const);
        // パラメータは初期化されているとみなす
        mark_variable_initialized(param.name);
    }

    for (auto& stmt : func.body) {
        check_statement(*stmt);
    }

    // Lint警告が有効な場合のみチェック
    if (enable_lint_warnings_) {
        // 関数終了時にconst推奨警告をチェック
        check_const_recommendations();

        // 未使用変数チェック (W001)
        check_unused_variables();
    }

    // 初期化追跡をクリア（次の関数用）
    initialized_variables_.clear();

    scopes_.pop();
    current_return_type_ = nullptr;
}

}  // namespace cm
