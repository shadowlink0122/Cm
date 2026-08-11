// ============================================================
// TypeChecker 実装 - 宣言の登録（Pass 1）と検査（Pass 2）の種別ディスパッチ
// ============================================================

#include "internal/base/i18n.hpp"
#include "internal/base/mangle.hpp"
#include "internal/types/type_checker.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace cm {

// R10: constジェネリックパラメータは宣言のみ受理され値引数での実体化が未実装のため、宣言時に専用診断で拒否する（従来は無警告で通り、使用箇所でExpected type等の誤誘導エラーになっていた）
void TypeChecker::reject_const_generic_params(const std::vector<ast::GenericParam>& params,
                                              Span span) {
    for (const auto& p : params) {
        if (p.is_const()) {
            error(span, i18n::msgf(i18n::MsgId::TcConstGenericUnsupported, p.name));
        }
    }
}

void TypeChecker::register_declaration(ast::Decl& decl) {
    if (auto* mod = decl.as<ast::ModuleDecl>()) {
        register_namespace(*mod, "");
        return;
    }

    if (auto* func = decl.as<ast::FunctionDecl>()) {
        reject_const_generic_params(func->generic_params_v2, decl.span);
        // R11: constexpr関数はコンパイル時評価が未実装で通常関数として扱われることを明示する（従来は無警告で黙殺され、配列サイズ等の定数文脈で使えず混乱を招いていた）
        if (func->is_constexpr) {
            warning(decl.span,
                    i18n::msgf(i18n::MsgId::TcConstexprFunctionTreatedAsRegular, func->name));
        }
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
        for (auto& p : func->params) {
            // typeof(リテラル) 仮引数型を具体型へ解決する（局所処理調査B3）。
            // リテラル被演算式はスコープ非依存で登録時に安全に解決できる（変数被演算式はスコープが要るため据え置き）。
            // シグネチャ登録が本体検査に先行するため、ここで解決すれば呼び出し側の型照合も本体も具体型を見る
            if (p.type && p.type->kind == ast::TypeKind::Inferred && p.type->name == "__typeof__" &&
                p.type->typeof_operand && p.type->typeof_operand->as<ast::LiteralExpr>()) {
                p.type = resolve_typeof(p.type);
            }
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
        reject_const_generic_params(st->generic_params_v2, decl.span);
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
                error(name_pos, i18n::msgf(i18n::MsgId::TcInterfaceSpecifiedMoreThanOnce,
                                           iface_name, st->name));
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
        current_span_ = decl.span;
        register_impl(*impl);
    } else if (auto* gv = decl.as<ast::GlobalVarDecl>()) {
        // グローバル変数/定数の登録（const強化）
        current_span_ = decl.span;
        // SVプラットフォーム: #[input]ポートを記録する（プロセス内からの代入を診断するため。R16）
        if (sv_platform_) {
            for (const auto& attr : gv->attributes) {
                if (attr.name == "input") {
                    sv_input_ports_.insert(gv->name);
                }
            }
        }
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

        // 初期化式の型チェック（宣言型を期待型として渡す。無名構造体リテラル等の型決定に必要。
        // typed-hir-single-source 第2段）
        ast::TypePtr init_type;
        if (gv->init_expr) {
            init_type = infer_type_expecting(*gv->init_expr,
                                             gv->type ? resolve_typedef(gv->type) : nullptr);
        }

        // 型を決定
        if (gv->type && !is_valid_type(gv->type)) {
            error(decl.span, i18n::msgf(i18n::MsgId::TcUndefinedTypeGlobalVariable,
                                        ast::type_to_string(*gv->type), gv->name));
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
                error(decl.span, i18n::msgf(i18n::MsgId::TcUndefinedTypeMacro,
                                            ast::type_to_string(*macro->type), macro->name));
            }
            ast::TypePtr var_type = macro->type ? resolve_typedef(macro->type) : init_type;
            // R10: 宣言型と初期化子型を照合する（従来は未照合で、型不一致マクロがLLVM検証エラー/js黙殺実行へ落ちていた）
            if (macro->type && var_type && init_type && !types_compatible(var_type, init_type)) {
                error(decl.span,
                      i18n::msgf(i18n::MsgId::TcMacroInitTypeMismatch, macro->name,
                                 ast::type_to_string(*var_type), ast::type_to_string(*init_type)));
            }
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
        for (auto& field : st->fields) {
            if (field.type && !is_valid_type(field.type)) {
                error(decl.span,
                      i18n::msgf(i18n::MsgId::TcUndefinedTypeFieldStruct,
                                 ast::type_to_string(*field.type), field.name, st->name));
            }
            // フィールドのconstパラメータ配列サイズ（int[N]）を具体サイズへ解決する。
            // 従来はローカル宣言（check_let）でのみ解決され、フィールドは未解決のままでnative/jitのレイアウトが壊れ無出力になっていた。
            // ベストエフォート解決: bit[WIDTH]のように同struct内の#[sv::param]フィールドを幅に使うSV構造体は
            // 記号名のまま残し（SVバックエンドが[WIDTH-1:0]で出力）、未解決名で診断を出さない
            if (field.type) {
                resolve_array_size(field.type, /*best_effort=*/true);
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
                        error(current_span_, i18n::msgf(i18n::MsgId::TcNestedCssFieldRequiresType,
                                                        field.name, type_name));
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
                // 設計上ペイロードは1値のみ（decl.hppのEnumMember設計コメント参照）。
                // 従来は複数値宣言を黙って受理し、構築時に2値目以降が消失・matchで束縛不能だった
                if (member.fields.size() > 1) {
                    error(decl.span, i18n::msgf(i18n::MsgId::TcEnumVariantHasMultiplePayload,
                                                en->name, member.name, member.name, member.name));
                }
                for (const auto& [field_name, field_type] : member.fields) {
                    if (field_type && !is_valid_type(field_type)) {
                        error(decl.span, i18n::msgf(i18n::MsgId::TcUndefinedTypeFieldEnumVariant,
                                                    ast::type_to_string(*field_type), field_name,
                                                    en->name, member.name));
                    }
                }
            }
        }

        generic_context_.clear();
    } else if (auto* td = decl.as<ast::TypedefDecl>()) {
        current_span_ = decl.span;
        if (td->type && !is_valid_type(td->type)) {
            error(decl.span, i18n::msgf(i18n::MsgId::TcUndefinedTypeTypedef,
                                        ast::type_to_string(*td->type), td->name));
        }
    } else if (auto* iface = decl.as<ast::InterfaceDecl>()) {
        current_span_ = decl.span;
        reject_const_generic_params(iface->generic_params_v2, decl.span);
        generic_context_.clear();
        if (!iface->generic_params.empty()) {
            for (const auto& param : iface->generic_params) {
                generic_context_.add_type_param(param);
            }
        }
        for (const auto& method : iface->methods) {
            if (method.return_type && !is_valid_type(method.return_type)) {
                error(decl.span, i18n::msgf(i18n::MsgId::TcUndefinedReturnTypeInterfaceMethod,
                                            ast::type_to_string(*method.return_type), iface->name,
                                            method.name));
            }
            for (const auto& param : method.params) {
                if (param.type && !is_valid_type(param.type)) {
                    error(decl.span,
                          i18n::msgf(i18n::MsgId::TcUndefinedParameterTypeParameterInterface,
                                     ast::type_to_string(*param.type), param.name, iface->name,
                                     method.name));
                }
            }
        }
        generic_context_.clear();
    } else if (auto* import = decl.as<ast::ImportDecl>()) {
        check_import(*import);
    } else if (auto* impl = decl.as<ast::ImplDecl>()) {
        current_span_ = decl.span;
        check_impl(*impl);
    }
}

}  // namespace cm
