// ============================================================
// TypeChecker 実装 - ジェネリクス処理
// ============================================================

#include "../type_checker.hpp"

#include <set>

namespace cm {

ast::TypePtr TypeChecker::infer_generic_call(ast::CallExpr& call, const std::string& func_name,
                                             const std::vector<std::string>& type_params) {
    // 引数の型を推論
    std::vector<ast::TypePtr> arg_types;
    for (auto& arg : call.args) {
        arg_types.push_back(infer_type(*arg));
    }

    // シンボルテーブルから関数情報を取得
    auto sym = scopes_.current().lookup(func_name);
    if (!sym || !sym->is_function) {
        error(current_span_, "'" + func_name + "' is not a function");
        return ast::make_error();
    }

    // 引数の数チェック（デフォルト引数を考慮）
    size_t arg_count = call.args.size();
    size_t param_count = sym->param_types.size();
    size_t required_count = sym->required_params;

    if (arg_count < required_count || arg_count > param_count) {
        if (required_count == param_count) {
            error(current_span_, "Generic function '" + func_name + "' expects " +
                                     std::to_string(param_count) + " arguments, got " +
                                     std::to_string(arg_count));
        } else {
            error(current_span_, "Generic function '" + func_name + "' expects " +
                                     std::to_string(required_count) + " to " +
                                     std::to_string(param_count) + " arguments, got " +
                                     std::to_string(arg_count));
        }
        return ast::make_error();
    }

    // 型パラメータをセットに変換
    std::set<std::string> type_param_set(type_params.begin(), type_params.end());

    // 型パラメータの推論
    std::unordered_map<std::string, ast::TypePtr> inferred_types;
    for (size_t i = 0; i < arg_types.size(); ++i) {
        auto& param_type = sym->param_types[i];
        auto& arg_type = arg_types[i];

        if (param_type && arg_type) {
            std::string param_str = ast::type_to_string(*param_type);
            // パラメータ型が型パラメータの場合
            if (type_param_set.count(param_str)) {
                auto it = inferred_types.find(param_str);
                if (it == inferred_types.end()) {
                    inferred_types[param_str] = arg_type;
                    debug::tc::log(debug::tc::Id::Resolved,
                                   "Inferred " + param_str + " = " + ast::type_to_string(*arg_type),
                                   debug::Level::Debug);
                }
            }
            // パラメータがジェネリック構造体の場合（例：Box<T>）
            else if (param_type->kind == ast::TypeKind::Struct && !param_type->type_args.empty() &&
                     arg_type->kind == ast::TypeKind::Struct &&
                     param_type->name == arg_type->name) {
                // 型引数を一致させて推論
                for (size_t j = 0;
                     j < param_type->type_args.size() && j < arg_type->type_args.size(); ++j) {
                    std::string type_arg_str = ast::type_to_string(*param_type->type_args[j]);
                    if (type_param_set.count(type_arg_str)) {
                        auto it = inferred_types.find(type_arg_str);
                        if (it == inferred_types.end()) {
                            inferred_types[type_arg_str] = arg_type->type_args[j];
                            debug::tc::log(debug::tc::Id::Resolved,
                                           "Inferred " + type_arg_str + " = " +
                                               ast::type_to_string(*arg_type->type_args[j]),
                                           debug::Level::Debug);
                        }
                    }
                }
            }
            // パラメータがポインタ型で内部がジェネリック構造体の場合（例：Node<T>*）
            else if (param_type->kind == ast::TypeKind::Pointer && param_type->element_type &&
                     arg_type->kind == ast::TypeKind::Pointer && arg_type->element_type) {
                auto inner_param = param_type->element_type;
                auto inner_arg = arg_type->element_type;

                if (inner_param->kind == ast::TypeKind::Struct && !inner_param->type_args.empty() &&
                    inner_arg->kind == ast::TypeKind::Struct &&
                    inner_param->name == inner_arg->name) {
                    // ポインタを剥がしてジェネリック構造体から型引数を推論
                    for (size_t j = 0;
                         j < inner_param->type_args.size() && j < inner_arg->type_args.size();
                         ++j) {
                        std::string type_arg_str = ast::type_to_string(*inner_param->type_args[j]);
                        if (type_param_set.count(type_arg_str)) {
                            auto it = inferred_types.find(type_arg_str);
                            if (it == inferred_types.end()) {
                                inferred_types[type_arg_str] = inner_arg->type_args[j];
                                debug::tc::log(debug::tc::Id::Resolved,
                                               "Inferred (from pointer) " + type_arg_str + " = " +
                                                   ast::type_to_string(*inner_arg->type_args[j]),
                                               debug::Level::Debug);
                            }
                        }
                    }
                }
            }
        }
    }

    // 推論された型情報を保存（後でモノモーフィゼーションで使用）
    if (!inferred_types.empty()) {
        call.inferred_type_args = inferred_types;

        // 順序付き型引数を設定
        for (const auto& param_name : type_params) {
            auto it = inferred_types.find(param_name);
            if (it != inferred_types.end()) {
                call.ordered_type_args.push_back(it->second);
            }
        }
    }

    // 型制約チェック
    auto constraint_it = generic_function_constraints_.find(func_name);
    if (constraint_it != generic_function_constraints_.end()) {
        for (const auto& generic_param : constraint_it->second) {
            if (!generic_param.constraints.empty()) {
                auto inferred_it = inferred_types.find(generic_param.name);
                if (inferred_it != inferred_types.end()) {
                    std::string actual_type = ast::type_to_string(*inferred_it->second);
                    if (!check_type_constraints(actual_type, generic_param.constraints)) {
                        std::string constraints_str;
                        for (size_t i = 0; i < generic_param.constraints.size(); ++i) {
                            if (i > 0)
                                constraints_str += " + ";
                            constraints_str += generic_param.constraints[i];
                        }
                        error(current_span_,
                              "Type '" + actual_type + "' does not satisfy constraint '" +
                                  constraints_str + "' for type parameter '" + generic_param.name +
                                  "' in function '" + func_name + "'");
                    }
                }
            }
        }
    }

    // 戻り値の型を置換
    std::string return_type_str = ast::type_to_string(*sym->return_type);
    auto it = inferred_types.find(return_type_str);
    if (it != inferred_types.end()) {
        debug::tc::log(debug::tc::Id::Resolved,
                       "Generic call " + func_name + " returns " + ast::type_to_string(*it->second),
                       debug::Level::Debug);
        return it->second;
    }

    // 戻り値型がジェネリック構造体の場合
    if (sym->return_type && sym->return_type->kind == ast::TypeKind::Struct &&
        !sym->return_type->type_args.empty()) {
        bool needs_substitution = false;
        std::vector<ast::TypePtr> new_type_args;

        for (const auto& type_arg : sym->return_type->type_args) {
            if (type_arg) {
                std::string arg_str = ast::type_to_string(*type_arg);
                auto subst_it = inferred_types.find(arg_str);
                if (subst_it != inferred_types.end()) {
                    new_type_args.push_back(subst_it->second);
                    needs_substitution = true;
                } else {
                    new_type_args.push_back(type_arg);
                }
            }
        }

        if (needs_substitution) {
            auto new_return_type = std::make_shared<ast::Type>(ast::TypeKind::Struct);
            new_return_type->name = sym->return_type->name;
            new_return_type->type_args = std::move(new_type_args);

            debug::tc::log(
                debug::tc::Id::Resolved,
                "Generic call " + func_name + " returns " + ast::type_to_string(*new_return_type),
                debug::Level::Debug);

            return new_return_type;
        }
    }

    return sym->return_type;
}

ast::TypePtr TypeChecker::substitute_generic_type(ast::TypePtr type,
                                                  const std::vector<std::string>& generic_params,
                                                  const std::vector<ast::TypePtr>& type_args) {
    if (!type)
        return type;

    // 型パラメータ名に一致するか確認
    std::string type_name = ast::type_to_string(*type);
    for (size_t i = 0; i < generic_params.size() && i < type_args.size(); ++i) {
        if (type_name == generic_params[i]) {
            return type_args[i];
        }
    }

    // 複合型の場合は再帰的に処理
    if (type->kind == ast::TypeKind::Pointer || type->kind == ast::TypeKind::Reference) {
        auto new_type = std::make_shared<ast::Type>(type->kind);
        new_type->element_type =
            substitute_generic_type(type->element_type, generic_params, type_args);
        return new_type;
    }

    if (type->kind == ast::TypeKind::Array) {
        auto new_type = std::make_shared<ast::Type>(ast::TypeKind::Array);
        new_type->element_type =
            substitute_generic_type(type->element_type, generic_params, type_args);
        new_type->array_size = type->array_size;
        return new_type;
    }

    // ジェネリック構造体の場合
    if (type->kind == ast::TypeKind::Struct && !type->type_args.empty()) {
        auto new_type = std::make_shared<ast::Type>(ast::TypeKind::Struct);
        new_type->name = type->name;
        for (const auto& arg : type->type_args) {
            new_type->type_args.push_back(substitute_generic_type(arg, generic_params, type_args));
        }
        return new_type;
    }

    return type;
}

bool TypeChecker::check_constraint(const std::string& /* type_param */,
                                   const ast::TypePtr& arg_type,
                                   const ast::GenericParam& constraint) {
    if (constraint.constraints.empty()) {
        return true;
    }

    std::string actual_type = ast::type_to_string(*arg_type);
    return check_type_constraints(actual_type, constraint.constraints);
}

// type_implements_interface と check_type_constraints は private メンバ関数として
// utils.cpp に実装

}  // namespace cm
