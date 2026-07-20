// ============================================================
// TypeChecker 実装 - 宣言の登録・チェック
// ============================================================

#include "internal/base/i18n.hpp"
#include "internal/base/mangle.hpp"
#include "internal/types/type_checker.hpp"
#include "match_hoist.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace cm {

TypeChecker::TypeChecker() {
    register_builtin_interfaces();
    register_builtin_types();  // Result<T, E>, Option<T> 組み込み型
}

bool TypeChecker::check(ast::Program& program) {
    debug::tc::log(debug::tc::Id::Start);

    // 組み込みprelude: Result<T, E> / Option<T> を実enum宣言としてプログラム先頭に注入する（TypeChecker内だけの疑似登録ではHIR/MIR/コード生成にenum定義が届かず、関数返却でのペイロード喪失や型IDの分裂を起こしていた）。
    // ユーザーが同名を定義している場合は注入しない
    {
        bool has_result = false;
        bool has_option = false;
        for (const auto& decl : program.declarations) {
            if (const auto* en = decl->as<ast::EnumDecl>()) {
                if (en->name == "Result") {
                    has_result = true;
                }
                if (en->name == "Option") {
                    has_option = true;
                }
            }
        }
        auto make_prelude_enum = [](const std::string& name, std::vector<std::string> params,
                                    std::vector<ast::EnumMember> members) {
            auto en = std::make_unique<ast::EnumDecl>(name, std::move(members));
            en->generic_params = std::move(params);
            // preludeマーカー: register_enumの組み込みメソッド消去（ユーザー再定義向け）を抑止する
            en->attributes.emplace_back("__prelude");
            return std::make_unique<ast::Decl>(std::move(en), Span{});
        };
        if (!has_option) {
            std::vector<ast::EnumMember> members;
            members.emplace_back("Some", std::vector<std::pair<std::string, ast::TypePtr>>{
                                             {"value", ast::make_named("T")}});
            members.emplace_back("None");
            program.declarations.insert(program.declarations.begin(),
                                        make_prelude_enum("Option", {"T"}, std::move(members)));
        }
        if (!has_result) {
            std::vector<ast::EnumMember> members;
            members.emplace_back("Ok", std::vector<std::pair<std::string, ast::TypePtr>>{
                                           {"value", ast::make_named("T")}});
            members.emplace_back("Err", std::vector<std::pair<std::string, ast::TypePtr>>{
                                            {"error", ast::make_named("E")}});
            program.declarations.insert(
                program.declarations.begin(),
                make_prelude_enum("Result", {"T", "E"}, std::move(members)));
        }
    }

    // 式形式matchの呼び出しscrutineeを一時変数へ退避するASTプリパス（HIRの三項演算子脱糖でのクローン多重評価を避け、単一評価を保証する）
    hoist_match_call_scrutinees(program);

    // Pass 1: 関数シグネチャを登録
    for (auto& decl : program.declarations) {
        register_declaration(*decl);
    }

    // Pass 2: 関数本体をチェック
    for (auto& decl : program.declarations) {
        check_declaration(*decl);
    }

    // 命名規則チェック（check/lint --strict時のみ有効）
    check_naming_conventions(program);

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

    // 名前空間内の非修飾名は「現在の名前空間::名前」として解決する
    if (auto qualified = resolve_in_namespace(name)) {
        auto ns_it = struct_defs_.find(*qualified);
        if (ns_it != struct_defs_.end()) {
            return ns_it->second;
        }
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

    // 名前空間内の非修飾型名解決のため現在の名前空間を設定（ネストは再帰で退避/復元）
    std::string saved_namespace = current_namespace_;
    current_namespace_ = full_namespace;

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
        } else if (auto* gvar = inner_decl->as<ast::GlobalVarDecl>()) {
            // グローバル変数も修飾名で登録し、import元の同名グローバルとの衝突を防ぐ（非修飾参照は infer_ident の名前空間フォールバックで解決される）
            std::string original_name = gvar->name;
            gvar->name = full_namespace + "::" + original_name;
            register_declaration(*inner_decl);
            gvar->name = original_name;
        } else {
            register_declaration(*inner_decl);
        }
    }

    current_namespace_ = saved_namespace;
}

void TypeChecker::check_namespace(ast::ModuleDecl& mod, const std::string& parent_namespace) {
    std::string namespace_name = mod.path.segments.empty() ? "" : mod.path.segments[0];
    std::string full_namespace =
        parent_namespace.empty() ? namespace_name : parent_namespace + "::" + namespace_name;

    debug::tc::log(debug::tc::Id::Resolved, "Checking namespace: " + full_namespace,
                   debug::Level::Debug);

    // 名前空間内の非修飾型名解決のため現在の名前空間を設定（ネストは再帰で退避/復元）
    std::string saved_namespace = current_namespace_;
    current_namespace_ = full_namespace;

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

    current_namespace_ = saved_namespace;
}

// マングル名をシンボルテーブルへ登録し、別由来・別シグネチャの同名があればエラーを発行する（C16）
// モジュールflatten等による同一定義の再出現（由来・シグネチャが完全一致）は許容する
void TypeChecker::register_mangled_symbol(const std::string& name, const std::string& origin,
                                          const std::string& sig, Span span) {
    auto [it, inserted] = mangled_symbols_.emplace(name, MangledSymbolInfo{origin, sig, span});
    if (inserted) {
        return;
    }
    if (it->second.origin == origin && it->second.sig == sig) {
        return;
    }
    error(span,
          i18n::msgf(i18n::MsgId::TypeMangledSymbolCollision, name, it->second.origin, origin));
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
            // L8: 本体が要求する演算子能力（< 等）に対して境界が全く宣言されていない場合を検出する
            check_generic_operator_bounds(*func);
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

        // 本体を持つ同名関数の重複定義を検出する。
        // 自由関数のオーバーロードは未対応であり、従来は診断なしで
        // LLVMの検証エラー（不正なコード生成）まで到達していた。
        // モジュールのフラット化により同一定義が複数回現れることがあるため、シグネチャ（引数型・戻り値型・本体文数）が完全一致する重複は許容し、異なるシグネチャの同名定義のみをエラーとする
        if (!func->body.empty() && !func->is_extern && func->generic_params.empty()) {
            auto type_sig = [](const ast::TypePtr& t) -> std::string {
                if (!t) {
                    return "?";
                }
                return std::to_string(static_cast<int>(t->kind)) + t->name;
            };
            std::string sig = type_sig(func->return_type) + "(";
            for (const auto& p : func->params) {
                sig += type_sig(p.type) + ",";
            }
            sig += ")#" + std::to_string(func->body.size());

            auto [it, inserted] = defined_function_sigs_.emplace(func->name, sig);
            if (!inserted && it->second != sig) {
                Span name_pos = func->name_span.is_empty() ? decl.span : func->name_span;
                error(name_pos,
                      i18n::msgf(i18n::MsgId::TypeFunctionIsAlreadyDefinedWith, func->name));
            }

            // C16: 自由関数のマングル名（モジュール修飾は::→__へフラット化）を単一シンボル
            // テーブルへ登録し、メソッド等の別由来と同名へ縮退する場合をハードエラー化する
            Span name_pos = func->name_span.is_empty() ? decl.span : func->name_span;
            register_mangled_symbol(mangle::flatten_qualified(func->name),
                                    "function '" + func->name + "'", sig, name_pos);
        }

        // L100: 関数名はsnake_caseであるべき
        // 関数名の命名規則チェックは check_naming_conventions（L001 --strict）へ一本化
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

        // with / #[derive] を合わせた同一interfaceの重複指定はエラー（記述ミス検出）
        std::set<std::string> seen_auto_impls;
        for (const auto& iface_name : st->auto_impls) {
            if (!seen_auto_impls.insert(iface_name).second) {
                Span name_pos = st->name_span.is_empty() ? decl.span : st->name_span;
                error(name_pos, "Interface '" + iface_name +
                                    "' is specified more than once in 'with' / #[derive] for "
                                    "struct '" +
                                    st->name + "'");
                continue;
            }
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
        if (gv->type && !is_valid_type(gv->type)) {
            error(decl.span, "Undefined type: '" + ast::type_to_string(*gv->type) +
                                 "' for global variable '" + gv->name + "'");
        }
        ast::TypePtr var_type = gv->type ? resolve_typedef(gv->type) : init_type;
        if (var_type) {
            scopes_.global().define(gv->name, var_type, gv->is_const, false, decl.span,
                                    const_int_value);
            // グローバル変数/定数は宣言時点で初期化済みとみなす（未初期化使用の警告はローカル変数のフローのみを対象にする）
            mark_variable_initialized(gv->name);
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
            if (macro->type && !is_valid_type(macro->type)) {
                error(decl.span, "Undefined type: '" + ast::type_to_string(*macro->type) +
                                     "' for macro '" + macro->name + "'");
            }
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

        // ジェネリック型パラメータをコンテキストに登録
        generic_context_.clear();
        if (!st->generic_params.empty()) {
            for (const auto& param : st->generic_params) {
                generic_context_.add_type_param(param);
            }
        }

        // 構造体の全フィールドの型が有効かチェック
        for (const auto& field : st->fields) {
            if (field.type && !is_valid_type(field.type)) {
                error(decl.span, "Undefined type: '" + ast::type_to_string(*field.type) +
                                     "' for field '" + field.name + "' in struct '" + st->name +
                                     "'");
            }
        }

        generic_context_.clear();

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
    } else if (auto* en = decl.as<ast::EnumDecl>()) {
        current_span_ = decl.span;

        // ジェネリック型パラメータをコンテキストに登録
        generic_context_.clear();
        if (!en->generic_params.empty()) {
            for (const auto& param : en->generic_params) {
                generic_context_.add_type_param(param);
            }
        }

        for (const auto& member : en->members) {
            if (member.has_data()) {
                for (const auto& [field_name, field_type] : member.fields) {
                    if (field_type && !is_valid_type(field_type)) {
                        error(decl.span, "Undefined type: '" + ast::type_to_string(*field_type) +
                                             "' for field '" + field_name + "' in enum variant '" +
                                             en->name + "::" + member.name + "'");
                    }
                }
            }
        }

        generic_context_.clear();
    } else if (auto* td = decl.as<ast::TypedefDecl>()) {
        current_span_ = decl.span;
        if (td->type && !is_valid_type(td->type)) {
            error(decl.span, "Undefined type: '" + ast::type_to_string(*td->type) +
                                 "' in typedef '" + td->name + "'");
        }
    } else if (auto* iface = decl.as<ast::InterfaceDecl>()) {
        current_span_ = decl.span;
        generic_context_.clear();
        if (!iface->generic_params.empty()) {
            for (const auto& param : iface->generic_params) {
                generic_context_.add_type_param(param);
            }
        }
        for (const auto& method : iface->methods) {
            if (method.return_type && !is_valid_type(method.return_type)) {
                error(decl.span,
                      "Undefined return type: '" + ast::type_to_string(*method.return_type) +
                          "' in interface method '" + iface->name + "::" + method.name + "'");
            }
            for (const auto& param : method.params) {
                if (param.type && !is_valid_type(param.type)) {
                    error(decl.span, "Undefined parameter type: '" +
                                         ast::type_to_string(*param.type) + "' for parameter '" +
                                         param.name + "' in interface method '" + iface->name +
                                         "::" + method.name + "'");
                }
            }
        }
        generic_context_.clear();
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
            std::string mangled_name =
                mangle::ctor_name(type_name, ctor->is_overload, ctor->params.size());
            std::vector<ast::TypePtr> param_types;
            param_types.push_back(impl.target_type);
            for (const auto& param : ctor->params) {
                param_types.push_back(param.type);
            }
            register_mangled_symbol(mangled_name, "constructor of '" + type_name + "'",
                                    std::to_string(ctor->params.size()), ctor->name_span);
            scopes_.global().define_function(mangled_name, std::move(param_types),
                                             ast::make_void());
        }
        if (impl.destructor) {
            std::string mangled_name = mangle::dtor_name(type_name);
            std::vector<ast::TypePtr> param_types;
            param_types.push_back(impl.target_type);
            register_mangled_symbol(mangled_name, "destructor of '" + type_name + "'", "",
                                    impl.destructor->name_span);
            scopes_.global().define_function(mangled_name, std::move(param_types),
                                             ast::make_void());
        }
        // 早期リターンを削除: メソッドも登録を続行
    }

    if (!impl.interface_name.empty()) {
        // インターフェースの存在チェック
        if (interface_names_.find(impl.interface_name) == interface_names_.end()) {
            throw std::runtime_error("'" + impl.interface_name + "' is not a declared interface");
        }
        if (impl_interfaces_[type_name].count(impl.interface_name) > 0) {
            throw std::runtime_error("Duplicate impl: " + type_name + " already implements " +
                                     impl.interface_name);
        }
        impl_interfaces_[type_name].insert(impl.interface_name);
        debug::tc::log(debug::tc::Id::Resolved, type_name + " implements " + impl.interface_name,
                       debug::Level::Debug);
    }

    // operator定義からインターフェース名を自動登録
    // impl T { operator ... } の場合、for InterfaceNameがなくてもimpl_interfaces_に登録
    for (const auto& op : impl.operators) {
        std::string iface_name;
        switch (op->op) {
            case ast::OperatorKind::Eq:
            case ast::OperatorKind::Ne:
                iface_name = "Eq";
                break;
            case ast::OperatorKind::Lt:
            case ast::OperatorKind::Gt:
            case ast::OperatorKind::Le:
            case ast::OperatorKind::Ge:
                iface_name = "Ord";
                break;
            case ast::OperatorKind::Add:
                iface_name = "Add";
                break;
            case ast::OperatorKind::Sub:
                iface_name = "Sub";
                break;
            case ast::OperatorKind::Mul:
                iface_name = "Mul";
                break;
            case ast::OperatorKind::Div:
                iface_name = "Div";
                break;
            case ast::OperatorKind::Mod:
                iface_name = "Mod";
                break;
            case ast::OperatorKind::BitAnd:
                iface_name = "BitAnd";
                break;
            case ast::OperatorKind::BitOr:
                iface_name = "BitOr";
                break;
            case ast::OperatorKind::BitXor:
                iface_name = "BitXor";
                break;
            case ast::OperatorKind::Shl:
                iface_name = "Shl";
                break;
            case ast::OperatorKind::Shr:
                iface_name = "Shr";
                break;
            default:
                break;
        }
        if (!iface_name.empty()) {
            impl_interfaces_[type_name].insert(iface_name);
            debug::tc::log(debug::tc::Id::Resolved,
                           type_name + " implements " + iface_name + " (via operator)",
                           debug::Level::Debug);
        }
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
        info.required_params = 0;
        for (const auto& param : method->params) {
            info.param_types.push_back(param.type);
            // デフォルト値のない引数のみ必須として数える（デフォルト引数の省略を許可）
            if (!param.default_value) {
                info.required_params++;
            }
        }
        type_methods_[type_name][method->name] = std::move(info);

        std::string mangled_name = mangle::method_name(type_name, method->name);
        std::vector<ast::TypePtr> all_param_types;
        all_param_types.push_back(impl.target_type);
        for (const auto& param : method->params) {
            all_param_types.push_back(param.type);
        }
        // C16: メソッドのマングル名を単一シンボルテーブルへ登録し、
        // 同名へ縮退する自由関数等があればハードエラー化する
        register_mangled_symbol(mangled_name, "method '" + type_name + "." + method->name + "'", "",
                                method->name_span);
        scopes_.global().define_function(mangled_name, std::move(all_param_types),
                                         method->return_type);
    }
}

void TypeChecker::check_impl(ast::ImplDecl& impl) {
    if (!impl.target_type)
        return;

    generic_context_.clear();
    if (!impl.generic_params.empty()) {
        for (const auto& param : impl.generic_params) {
            generic_context_.add_type_param(param);
        }
    }

    std::string type_name = ast::type_to_string(*impl.target_type);

    if (!impl.interface_name.empty()) {
        // インターフェースの存在チェック
        if (interface_names_.find(impl.interface_name) == interface_names_.end()) {
            throw std::runtime_error("'" + impl.interface_name + "' is not a declared interface");
        }
        impl_interfaces_[type_name].insert(impl.interface_name);
        debug::tc::log(debug::tc::Id::Resolved, type_name + " implements " + impl.interface_name,
                       debug::Level::Debug);
    }

    // operator定義からインターフェース名を自動登録
    for (const auto& op : impl.operators) {
        std::string iface_name;
        switch (op->op) {
            case ast::OperatorKind::Eq:
            case ast::OperatorKind::Ne:
                iface_name = "Eq";
                break;
            case ast::OperatorKind::Lt:
            case ast::OperatorKind::Gt:
            case ast::OperatorKind::Le:
            case ast::OperatorKind::Ge:
                iface_name = "Ord";
                break;
            case ast::OperatorKind::Add:
                iface_name = "Add";
                break;
            case ast::OperatorKind::Sub:
                iface_name = "Sub";
                break;
            case ast::OperatorKind::Mul:
                iface_name = "Mul";
                break;
            case ast::OperatorKind::Div:
                iface_name = "Div";
                break;
            case ast::OperatorKind::Mod:
                iface_name = "Mod";
                break;
            case ast::OperatorKind::BitAnd:
                iface_name = "BitAnd";
                break;
            case ast::OperatorKind::BitOr:
                iface_name = "BitOr";
                break;
            case ast::OperatorKind::BitXor:
                iface_name = "BitXor";
                break;
            case ast::OperatorKind::Shl:
                iface_name = "Shl";
                break;
            case ast::OperatorKind::Shr:
                iface_name = "Shr";
                break;
            default:
                break;
        }
        if (!iface_name.empty()) {
            impl_interfaces_[type_name].insert(iface_name);
        }
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
    generic_context_.clear();
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

    // ユーザー定義のResult/Optionは組み込み型を上書きするため
    // type_methods_をクリアする（組み込みメソッドをユーザー実装で上書き可能に）。
    // prelude注入された組み込み宣言（__prelude属性）は対象外
    if (en.name == "Result" || en.name == "Option") {
        bool is_prelude = false;
        for (const auto& attr : en.attributes) {
            if (attr.name == "__prelude") {
                is_prelude = true;
                break;
            }
        }
        if (!is_prelude) {
            type_methods_.erase(en.name);
        }
    }

    int64_t variant_index = 0;
    for (const auto& member : en.members) {
        std::string full_name = en.name + "::" + member.name;

        if (member.has_data()) {
            // ジェネリックenumの場合でもenum_values_に登録
            // Tagged Union用のタグ値として使用
            enum_values_[full_name] = variant_index;

            // ジェネリックenumの場合、variantは通常の関数として登録しない（infer_call内のenum constructor処理で処理する）
            if (!en.generic_params.empty()) {
                debug::tc::log(debug::tc::Id::Resolved,
                               "  " + full_name + "(...) -> " + en.name +
                                   " [generic variant constructor - deferred, tag=" +
                                   std::to_string(variant_index) + "]",
                               debug::Level::Debug);
                variant_index++;
                continue;
            }

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
    // #[test] 関数は「引数なし・戻り値void」に限定する（SVテストベンチ/JITテストランナーの両方が前提とするシグネチャ）
    for (const auto& attr : func.attributes) {
        if (attr.name == "test") {
            if (!func.params.empty()) {
                error(func.name_span,
                      i18n::msgf(i18n::MsgId::TypeTestFunctionCannotTakeParameters, func.name));
            }
            if (!func.return_type || func.return_type->kind != ast::TypeKind::Void) {
                error(func.name_span,
                      i18n::msgf(i18n::MsgId::TypeTestFunctionMustReturnVoid, func.name));
            }
            break;
        }
    }

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
    if (!is_valid_type(func.return_type)) {
        error(func.name_span, "Undefined return type: '" + ast::type_to_string(*func.return_type) +
                                  "' in function '" + func.name + "'");
    }
    if (generic_context_.has_type_param(ast::type_to_string(*func.return_type))) {
        current_return_type_ = func.return_type;
    }

    for (const auto& param : func.params) {
        if (!is_valid_type(param.type)) {
            error(func.name_span, "Undefined parameter type: '" + ast::type_to_string(*param.type) +
                                      "' for parameter '" + param.name + "' in function '" +
                                      func.name + "'");
        }
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
