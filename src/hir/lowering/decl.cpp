// lowering_decl.cpp - 宣言のlowering
#include "fwd.hpp"

namespace cm::hir {

// 宣言の変換
HirDeclPtr HirLowering::lower_decl(ast::Decl& decl) {
    if (auto* func = decl.as<ast::FunctionDecl>()) {
        return lower_function(*func);
    } else if (auto* st = decl.as<ast::StructDecl>()) {
        return lower_struct(*st);
    } else if (auto* iface = decl.as<ast::InterfaceDecl>()) {
        return lower_interface(*iface);
    } else if (auto* impl = decl.as<ast::ImplDecl>()) {
        return lower_impl(*impl);
    } else if (auto* imp = decl.as<ast::ImportDecl>()) {
        return lower_import(*imp);
    } else if (auto* use = decl.as<ast::UseDecl>()) {
        return lower_use(*use);
    } else if (auto* en = decl.as<ast::EnumDecl>()) {
        return lower_enum(*en);
    } else if (auto* td = decl.as<ast::TypedefDecl>()) {
        return lower_typedef(*td);
    } else if (auto* gv = decl.as<ast::GlobalVarDecl>()) {
        return lower_global_var(*gv);
    } else if (auto* mod = decl.as<ast::ModuleDecl>()) {
        return lower_module(*mod);
    } else if (auto* extern_block = decl.as<ast::ExternBlockDecl>()) {
        return lower_extern_block(*extern_block);
    }
    return nullptr;
}

// extern "C" ブロック
HirDeclPtr HirLowering::lower_extern_block(ast::ExternBlockDecl& extern_block) {
    auto hir_extern = std::make_unique<HirExternBlock>();
    hir_extern->language = extern_block.language;
    for (const auto& func : extern_block.declarations) {
        auto hir_func = std::make_unique<HirFunction>();
        hir_func->name = func->name;
        hir_func->return_type = func->return_type;
        hir_func->is_extern = true;
        for (const auto& p : func->params) {
            hir_func->params.push_back({p.name, p.type});
        }
        hir_extern->functions.push_back(std::move(hir_func));
    }
    return std::make_unique<HirDecl>(std::move(hir_extern));
}

// 関数
HirDeclPtr HirLowering::lower_function(ast::FunctionDecl& func) {
    debug::hir::log(debug::hir::Id::FunctionNode, "function " + func.name, debug::Level::Debug);
    debug::hir::log(debug::hir::Id::FunctionName, func.name, debug::Level::Trace);

    auto hir_func = std::make_unique<HirFunction>();
    hir_func->name = func.name;
    hir_func->return_type = func.return_type;
    hir_func->is_export = func.visibility == ast::Visibility::Export;
    hir_func->is_async = func.is_async;  // v0.13.0: asyncフラグを伝播

    // ジェネリックパラメータを処理
    for (const auto& param_name : func.generic_params) {
        HirGenericParam param;
        param.name = param_name;
        hir_func->generic_params.push_back(param);
    }

    debug::hir::log(debug::hir::Id::FunctionReturn,
                    func.return_type ? type_to_string(*func.return_type) : "void",
                    debug::Level::Trace);

    debug::hir::log(debug::hir::Id::FunctionParams, "count=" + std::to_string(func.params.size()),
                    debug::Level::Trace);
    for (const auto& param : func.params) {
        hir_func->params.push_back({param.name, param.type});
        debug::hir::dump_symbol(param.name, func.name,
                                param.type ? type_to_string(*param.type) : "auto");
    }

    debug::hir::log(debug::hir::Id::FunctionBody, "statements=" + std::to_string(func.body.size()),
                    debug::Level::Trace);
    for (auto& stmt : func.body) {
        if (auto hir_stmt = lower_stmt(*stmt)) {
            hir_func->body.push_back(std::move(hir_stmt));
        }
    }

    return std::make_unique<HirDecl>(std::move(hir_func));
}

// 構造体
HirDeclPtr HirLowering::lower_struct(ast::StructDecl& st) {
    debug::hir::log(debug::hir::Id::StructNode, "struct " + st.name, debug::Level::Debug);

    auto hir_st = std::make_unique<HirStruct>();
    hir_st->name = st.name;
    hir_st->is_export = st.visibility == ast::Visibility::Export;
    hir_st->auto_impls = st.auto_impls;
    for (const auto& iface_name : st.auto_impls) {
        if (iface_name == "Css") {
            hir_st->is_css = true;
            break;
        }
    }

    // ジェネリックパラメータを処理
    for (const auto& param_name : st.generic_params) {
        HirGenericParam param;
        param.name = param_name;
        hir_st->generic_params.push_back(param);
    }

    for (const auto& field : st.fields) {
        hir_st->fields.push_back({field.name, field.type});
        debug::hir::log(debug::hir::Id::StructField,
                        field.name + " : " + (field.type ? type_to_string(*field.type) : "auto"),
                        debug::Level::Trace);
    }

    return std::make_unique<HirDecl>(std::move(hir_st));
}

// インターフェース
HirDeclPtr HirLowering::lower_interface(ast::InterfaceDecl& iface) {
    debug::hir::log(debug::hir::Id::NodeCreate, "interface " + iface.name, debug::Level::Trace);

    auto hir_iface = std::make_unique<HirInterface>();
    hir_iface->name = iface.name;
    hir_iface->is_export = iface.visibility == ast::Visibility::Export;

    // ジェネリックパラメータを処理
    for (const auto& param_name : iface.generic_params) {
        HirGenericParam param;
        param.name = param_name;
        hir_iface->generic_params.push_back(param);
    }

    // 通常のメソッドシグネチャ
    for (const auto& method : iface.methods) {
        HirMethodSig sig;
        sig.name = method.name;
        sig.return_type = method.return_type;
        for (const auto& param : method.params) {
            sig.params.push_back({param.name, param.type});
        }
        hir_iface->methods.push_back(std::move(sig));
    }

    // 演算子シグネチャ
    for (const auto& op : iface.operators) {
        HirOperatorSig sig;
        sig.op = convert_operator_kind(op.op);
        sig.return_type = op.return_type;
        for (const auto& param : op.params) {
            sig.params.push_back({param.name, param.type});
        }
        hir_iface->operators.push_back(std::move(sig));
    }

    return std::make_unique<HirDecl>(std::move(hir_iface));
}

// impl
HirDeclPtr HirLowering::lower_impl(ast::ImplDecl& impl) {
    debug::hir::log(debug::hir::Id::NodeCreate, "impl " + impl.interface_name, debug::Level::Trace);

    auto hir_impl = std::make_unique<HirImpl>();
    hir_impl->interface_name = impl.interface_name;
    hir_impl->target_type = impl.target_type ? type_to_string(*impl.target_type) : "";
    hir_impl->is_ctor_impl = impl.is_ctor_impl;

    // ジェネリックパラメータを処理
    for (const auto& param_name : impl.generic_params) {
        HirGenericParam param;
        param.name = param_name;
        hir_impl->generic_params.push_back(param);
    }

    // where句を処理
    for (const auto& clause : impl.where_clauses) {
        HirWhereClause hir_clause;
        hir_clause.type_param = clause.type_param;
        hir_clause.constraint_type = clause.constraint_type;
        hir_impl->where_clauses.push_back(hir_clause);
    }

    // コンストラクタ専用implの場合、コンストラクタ/デストラクタをlower
    if (impl.is_ctor_impl) {
        for (auto& ctor : impl.constructors) {
            auto hir_func = std::make_unique<HirFunction>();
            std::string mangled_name = hir_impl->target_type + "__ctor";
            if (!ctor->params.empty()) {
                mangled_name += "_" + std::to_string(ctor->params.size());
            }
            hir_func->name = mangled_name;
            hir_func->return_type = ast::make_void();
            hir_func->is_constructor = true;

            // selfはポインタ型として定義（MIRで暗黙的にポインタとして扱う）
            hir_func->params.push_back({"self", ast::make_pointer(impl.target_type)});
            for (const auto& param : ctor->params) {
                hir_func->params.push_back({param.name, param.type});
            }

            for (auto& stmt : ctor->body) {
                if (auto hir_stmt = lower_stmt(*stmt)) {
                    hir_func->body.push_back(std::move(hir_stmt));
                }
            }

            hir_impl->methods.push_back(std::move(hir_func));
        }

        // デストラクタをlower
        if (impl.destructor) {
            auto hir_func = std::make_unique<HirFunction>();
            hir_func->name = hir_impl->target_type + "__dtor";
            hir_func->return_type = ast::make_void();
            hir_func->is_destructor = true;

            // selfはポインタ型として定義（MIRで暗黙的にポインタとして扱う）
            hir_func->params.push_back({"self", ast::make_pointer(impl.target_type)});

            for (auto& stmt : impl.destructor->body) {
                if (auto hir_stmt = lower_stmt(*stmt)) {
                    hir_func->body.push_back(std::move(hir_stmt));
                }
            }

            hir_impl->methods.push_back(std::move(hir_func));
        }

        // 早期リターンを削除: メソッドも処理を続行
    }

    // メソッド実装の場合（is_ctor_impl時もメソッドがあれば処理）
    for (auto& method : impl.methods) {
        auto hir_func = std::make_unique<HirFunction>();
        hir_func->name = method->name;
        hir_func->return_type = method->return_type;

        for (const auto& gp : hir_impl->generic_params) {
            hir_func->generic_params.push_back(gp);
        }

        if (impl.target_type) {
            // selfはポインタ型として定義（MIRで暗黙的にポインタとして扱う）
            hir_func->params.push_back({"self", ast::make_pointer(impl.target_type)});
        }

        for (const auto& param : method->params) {
            hir_func->params.push_back({param.name, param.type});
        }

        for (auto& stmt : method->body) {
            if (auto hir_stmt = lower_stmt(*stmt)) {
                hir_func->body.push_back(std::move(hir_stmt));
            }
        }

        hir_impl->methods.push_back(std::move(hir_func));
    }

    // 演算子実装
    for (auto& op : impl.operators) {
        auto hir_op = std::make_unique<HirOperatorImpl>();
        hir_op->op = convert_operator_kind(op->op);
        hir_op->return_type = op->return_type;

        for (const auto& param : op->params) {
            hir_op->params.push_back({param.name, param.type});
        }

        for (auto& stmt : op->body) {
            if (auto hir_stmt = lower_stmt(*stmt)) {
                hir_op->body.push_back(std::move(hir_stmt));
            }
        }

        hir_impl->operators.push_back(std::move(hir_op));
    }

    return std::make_unique<HirDecl>(std::move(hir_impl));
}

// インポート
HirDeclPtr HirLowering::lower_import(ast::ImportDecl& imp) {
    debug::hir::log(debug::hir::Id::NodeCreate, "import " + imp.path.to_string(),
                    debug::Level::Trace);

    auto hir_imp = std::make_unique<HirImport>();
    hir_imp->path = imp.path.segments;
    hir_imp->alias = "";

    // std::io::println -> __println__ へのエイリアス登録
    std::string path_str = imp.path.to_string();
    if (path_str == "std::io::println") {
        import_aliases_["println"] = "__println__";
    } else if (path_str == "std::io::print") {
        import_aliases_["print"] = "__print__";
    } else if (path_str == "std::io") {
        // ワイルドカードインポート時
        for (const auto& item : imp.items) {
            if (item.name == "println") {
                import_aliases_["println"] = "__println__";
            } else if (item.name == "print") {
                import_aliases_["print"] = "__print__";
            }
        }
    }

    return std::make_unique<HirDecl>(std::move(hir_imp));
}

// Use（FFI/モジュール）
HirDeclPtr HirLowering::lower_use(ast::UseDecl& use) {
    debug::hir::log(debug::hir::Id::NodeCreate, "use " + use.path.to_string(), debug::Level::Trace);

    // FFI useの場合はexternブロックとして処理
    if (use.kind == ast::UseDecl::FFIUse) {
        auto hir_extern = std::make_unique<HirExternBlock>();
        hir_extern->language = "C";                   // デフォルトはC ABI
        hir_extern->package_name = use.package_name;  // パッケージ名

        for (const auto& ffi_func : use.ffi_funcs) {
            auto hir_func = std::make_unique<HirFunction>();
            hir_func->name = ffi_func.name;
            hir_func->return_type = ffi_func.return_type;
            hir_func->is_extern = true;
            hir_func->is_variadic = ffi_func.is_variadic;  // 可変長引数フラグを伝播

            for (const auto& [param_name, param_type] : ffi_func.params) {
                hir_func->params.push_back({param_name, param_type});
            }

            // 名前空間エイリアスの場合、呼び出し時の名前をマップ
            if (use.alias) {
                std::string aliased_name = *use.alias + "::" + ffi_func.name;
                import_aliases_[aliased_name] = ffi_func.name;
            }

            hir_extern->functions.push_back(std::move(hir_func));
        }

        return std::make_unique<HirDecl>(std::move(hir_extern));
    }

    // 通常のモジュールuseの場合
    auto hir_imp = std::make_unique<HirImport>();
    hir_imp->path = use.path.segments;
    hir_imp->package_name = use.package_name;
    hir_imp->alias = use.alias.value_or("");

    return std::make_unique<HirDecl>(std::move(hir_imp));
}

// Enum
HirDeclPtr HirLowering::lower_enum(ast::EnumDecl& en) {
    debug::hir::log(debug::hir::Id::NodeCreate, "enum " + en.name, debug::Level::Debug);

    auto hir_enum = std::make_unique<HirEnum>();
    hir_enum->name = en.name;
    hir_enum->is_export = en.visibility == ast::Visibility::Export;

    // ジェネリック型パラメータ（v0.13.0）
    hir_enum->type_params = en.type_params;

    for (const auto& member : en.members) {
        HirEnumMember hir_member;
        hir_member.name = member.name;
        hir_member.value = member.value.value_or(0);

        // Associated Dataフィールド（v0.13.0）
        for (const auto& field : member.fields) {
            HirEnumField hir_field;
            hir_field.name = field.name;
            hir_field.type = field.type;
            hir_member.fields.push_back(std::move(hir_field));
        }

        hir_enum->members.push_back(std::move(hir_member));
    }

    return std::make_unique<HirDecl>(std::move(hir_enum));
}

// Typedef
HirDeclPtr HirLowering::lower_typedef(ast::TypedefDecl& td) {
    debug::hir::log(debug::hir::Id::NodeCreate, "typedef " + td.name, debug::Level::Debug);

    auto hir_typedef = std::make_unique<HirTypedef>();
    hir_typedef->name = td.name;
    hir_typedef->type = td.type;

    return std::make_unique<HirDecl>(std::move(hir_typedef));
}

// グローバル変数/定数
HirDeclPtr HirLowering::lower_global_var(ast::GlobalVarDecl& gv) {
    debug::hir::log(debug::hir::Id::NodeCreate,
                    std::string(gv.is_const ? "const " : "var ") + gv.name, debug::Level::Debug);

    auto hir_global = std::make_unique<HirGlobalVar>();
    hir_global->name = gv.name;
    hir_global->type = gv.type;
    hir_global->is_const = gv.is_const;
    hir_global->is_export = (gv.visibility == ast::Visibility::Export);

    if (gv.init_expr) {
        hir_global->init = lower_expr(*gv.init_expr);
    }

    return std::make_unique<HirDecl>(std::move(hir_global));
}

// Module/Namespace
HirDeclPtr HirLowering::lower_module(ast::ModuleDecl& mod) {
    std::string namespace_name = mod.path.segments.empty() ? "" : mod.path.segments[0];

    debug::hir::log(debug::hir::Id::NodeCreate, "namespace " + namespace_name, debug::Level::Debug);

    return nullptr;
}

}  // namespace cm::hir
