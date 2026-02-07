// ============================================================
// TypeChecker 実装 - 関数/メソッド呼び出し
// ============================================================

#include "../type_checker.hpp"

namespace cm {

ast::TypePtr TypeChecker::infer_call(ast::CallExpr& call) {
    if (auto* ident = call.callee->as<ast::IdentExpr>()) {
        // __asm__ / __llvm__ intrinsic - インラインアセンブリ
        // __asm__: ネイティブアセンブリ（x86, ARM64等）- 推奨
        // __llvm__: 後方互換性のため残す（将来はLLVM IR対応予定）
        if (ident->name == "__asm__" || ident->name == "__llvm__") {
            if (call.args.size() != 1) {
                error(current_span_, ident->name + " requires exactly 1 argument (assembly code)");
                return ast::make_error();
            }
            // 引数が文字列リテラルであることを確認
            if (auto* lit = call.args[0]->as<ast::LiteralExpr>()) {
                if (!std::holds_alternative<std::string>(lit->value)) {
                    error(current_span_, ident->name + " argument must be a string literal");
                    return ast::make_error();
                }
            } else {
                error(current_span_, ident->name + " argument must be a string literal");
                return ast::make_error();
            }
            return ast::make_void();
        }

        // 組み込み関数の特別処理（printlnはstd::io::printlnからインポート推奨だが互換性のため残す）
        if (ident->name == "println" || ident->name == "print") {
            // println() は引数なしでも許可（空行出力）
            if (ident->name == "print" && call.args.empty()) {
                error(current_span_, "'" + ident->name + "' requires at least 1 argument");
                return ast::make_error();
            }
            if (call.args.size() > 1) {
                error(current_span_, "'" + ident->name + "' takes only 1 argument, got " +
                                         std::to_string(call.args.size()));
                return ast::make_error();
            }

            for (auto& arg : call.args) {
                infer_type(*arg);
            }

            return ast::make_void();
        }

        // ジェネリック関数かチェック
        auto gen_it = generic_functions_.find(ident->name);
        if (gen_it != generic_functions_.end()) {
            return infer_generic_call(call, ident->name, gen_it->second);
        }

        // 構造体のコンストラクタ呼び出しかチェック
        if (get_struct(ident->name) != nullptr) {
            for (auto& arg : call.args) {
                infer_type(*arg);
            }
            return ast::make_named(ident->name);
        }

        // 通常の関数はシンボルテーブルから検索
        auto sym = scopes_.current().lookup(ident->name);
        if (!sym) {
            // 静的メソッド呼び出しの可能性をチェック: Type::method
            size_t last_colon = ident->name.rfind("::");
            if (last_colon != std::string::npos) {
                std::string type_name = ident->name.substr(0, last_colon);
                std::string method_name = ident->name.substr(last_colon + 2);

                // 型名からジェネリック型パラメータを抽出（Vec<int>など）
                // まず直接検索を試みる
                auto it = type_methods_.find(type_name);
                if (it == type_methods_.end()) {
                    // ジェネリック型の場合: Vec<int> -> Vec<T> に変換して検索
                    size_t lt_pos = type_name.find('<');
                    if (lt_pos != std::string::npos) {
                        std::string base_name = type_name.substr(0, lt_pos);

                        // generic_structs_から型パラメータを取得
                        auto gen_it = generic_structs_.find(base_name);
                        if (gen_it != generic_structs_.end()) {
                            // 登録時の型名を構築: Vec<T>
                            std::string generic_type_name = base_name + "<";
                            for (size_t i = 0; i < gen_it->second.size(); ++i) {
                                if (i > 0)
                                    generic_type_name += ", ";
                                generic_type_name += gen_it->second[i];
                            }
                            generic_type_name += ">";

                            it = type_methods_.find(generic_type_name);
                        }
                    }
                }

                if (it != type_methods_.end()) {
                    auto method_it = it->second.find(method_name);
                    if (method_it != it->second.end()) {
                        const auto& method_info = method_it->second;

                        // 静的メソッドかチェック
                        if (!method_info.is_static) {
                            error(current_span_, "Method '" + method_name + "' of type '" +
                                                     type_name + "' is not a static method");
                            return ast::make_error();
                        }

                        // 引数の型チェック
                        if (call.args.size() != method_info.param_types.size()) {
                            error(current_span_,
                                  "Static method '" + ident->name + "' expects " +
                                      std::to_string(method_info.param_types.size()) +
                                      " arguments, got " + std::to_string(call.args.size()));
                        } else {
                            for (size_t i = 0; i < call.args.size(); ++i) {
                                auto arg_type = infer_type(*call.args[i]);
                                if (!types_compatible(method_info.param_types[i], arg_type)) {
                                    std::string expected =
                                        ast::type_to_string(*method_info.param_types[i]);
                                    std::string actual = ast::type_to_string(*arg_type);
                                    error(current_span_, "Argument type mismatch in call to '" +
                                                             ident->name + "': expected " +
                                                             expected + ", got " + actual);
                                }
                            }
                        }

                        // 戻り値型を返す（ジェネリック型パラメータを具体化する必要がある場合がある）
                        auto return_type = method_info.return_type;

                        // 戻り値型がジェネリック型の場合、具体的な型引数で置き換え
                        size_t lt_pos = type_name.find('<');
                        if (lt_pos != std::string::npos && return_type) {
                            std::string base_name = type_name.substr(0, lt_pos);
                            auto gen_it = generic_structs_.find(base_name);
                            if (gen_it != generic_structs_.end()) {
                                // type_nameから型引数を抽出: Vec<int> -> ["int"]
                                std::string type_args_str = type_name.substr(lt_pos + 1);
                                type_args_str = type_args_str.substr(
                                    0, type_args_str.size() - 1);  // 末尾の > を削除

                                // 型引数をvectorに変換
                                std::vector<ast::TypePtr> concrete_type_args;
                                // 簡易パース（カンマ区切り）
                                std::istringstream iss(type_args_str);
                                std::string type_arg_name;
                                while (std::getline(iss, type_arg_name, ',')) {
                                    // 空白をトリム
                                    size_t start = type_arg_name.find_first_not_of(" ");
                                    size_t end = type_arg_name.find_last_not_of(" ");
                                    if (start != std::string::npos && end != std::string::npos) {
                                        type_arg_name =
                                            type_arg_name.substr(start, end - start + 1);
                                    }
                                    // 型名を作成
                                    concrete_type_args.push_back(ast::make_named(type_arg_name));
                                }

                                // substitute_generic_typeで戻り値型を置換
                                return_type = substitute_generic_type(return_type, gen_it->second,
                                                                      concrete_type_args);
                            }
                        }

                        debug::tc::log(debug::tc::Id::Resolved,
                                       "Static method call: " + ident->name +
                                           "() : " + ast::type_to_string(*return_type),
                                       debug::Level::Debug);
                        return return_type;
                    }
                }

                // ============================================================
                // enum constructor呼び出しのチェック: Result::Ok(value)など
                // ============================================================

                auto enum_it = generic_enums_.find(type_name);
                if (enum_it != generic_enums_.end()) {
                    // ジェネリックenumのコンストラクタ呼び出し
                    auto enum_def_it = enum_defs_.find(type_name);
                    if (enum_def_it != enum_defs_.end() && enum_def_it->second) {
                        const ast::EnumDecl* enum_decl = enum_def_it->second;

                        // メンバ（バリアント）が存在するか確認
                        for (const auto& member : enum_decl->members) {
                            if (member.name == method_name) {
                                // current_return_type_から型引数を推論
                                ast::TypePtr result_type = nullptr;

                                // typedefを解決してから比較
                                ast::TypePtr resolved_return_type = nullptr;
                                if (current_return_type_) {
                                    resolved_return_type = resolve_typedef(current_return_type_);
                                }

                                if (resolved_return_type &&
                                    resolved_return_type->name == type_name &&
                                    !resolved_return_type->type_args.empty()) {
                                    // 戻り値型から型引数を取得
                                    result_type = resolved_return_type;

                                    // 引数の型チェック（バリアントにデータがある場合）
                                    if (member.has_data() && !call.args.empty()) {
                                        auto arg_type = infer_type(*call.args[0]);
                                        // バリアントのデータ型をジェネリックパラメータから解決
                                        auto& type_params = enum_it->second;
                                        auto& type_args = resolved_return_type->type_args;

                                        // member.fieldsからデータ型を取得（1フィールドのみサポート）
                                        if (!member.fields.empty() && member.fields[0].second) {
                                            auto expected_type = substitute_generic_type(
                                                member.fields[0].second, type_params, type_args);
                                            if (!types_compatible(expected_type, arg_type)) {
                                                error(
                                                    current_span_,
                                                    "Argument type mismatch in enum constructor '" +
                                                        ident->name + "': expected " +
                                                        ast::type_to_string(*expected_type) +
                                                        ", got " + ast::type_to_string(*arg_type));
                                            }
                                        }
                                    }
                                } else {
                                    // 引数から型を推論（Result::Ok(5)のように）
                                    if (!call.args.empty()) {
                                        infer_type(*call.args[0]);
                                    }
                                    // enum型を返す（型引数なし）
                                    result_type = ast::make_named(type_name);
                                }

                                debug::tc::log(debug::tc::Id::Resolved,
                                               "Enum constructor: " + ident->name + "() : " +
                                                   (result_type ? ast::type_to_string(*result_type)
                                                                : type_name),
                                               debug::Level::Debug);
                                return result_type;
                            }
                        }
                    } else {
                        // 組み込み型（Result, Option）: enum_defs_にはないがgeneric_enums_にある
                        // enum_values_でバリアントを確認
                        std::string full_variant = type_name + "::" + method_name;
                        if (enum_values_.count(full_variant) > 0) {
                            // current_return_type_から型引数を推論
                            ast::TypePtr result_type = nullptr;
                            ast::TypePtr resolved_return_type = nullptr;
                            if (current_return_type_) {
                                resolved_return_type = resolve_typedef(current_return_type_);
                            }

                            if (resolved_return_type && resolved_return_type->name == type_name &&
                                !resolved_return_type->type_args.empty()) {
                                result_type = resolved_return_type;

                                // 引数の型チェック
                                if (!call.args.empty()) {
                                    auto arg_type = infer_type(*call.args[0]);
                                    auto& type_params = enum_it->second;
                                    (void)type_params;  // 将来使用予定
                                    auto& type_args = resolved_return_type->type_args;

                                    // Ok(T) -> type_args[0], Err(E) -> type_args[1]
                                    // Some(T) -> type_args[0]
                                    size_t param_idx = 0;
                                    if (type_name == "Result" && method_name == "Err") {
                                        param_idx = 1;  // E is the second type param
                                    }
                                    if (param_idx < type_args.size()) {
                                        auto expected_type = type_args[param_idx];
                                        if (!types_compatible(expected_type, arg_type)) {
                                            error(current_span_,
                                                  "Argument type mismatch in '" + ident->name +
                                                      "': expected " +
                                                      ast::type_to_string(*expected_type) +
                                                      ", got " + ast::type_to_string(*arg_type));
                                        }
                                    }
                                }
                            } else {
                                // 引数から型を推論
                                if (!call.args.empty()) {
                                    infer_type(*call.args[0]);
                                }
                                result_type = ast::make_named(type_name);
                            }

                            debug::tc::log(
                                debug::tc::Id::Resolved,
                                "Builtin enum constructor: " + ident->name + "() : " +
                                    (result_type ? ast::type_to_string(*result_type) : type_name),
                                debug::Level::Debug);
                            return result_type;
                        }
                    }
                }

                // 非ジェネリックenumもチェック
                if (enum_names_.count(type_name)) {
                    auto enum_def_it = enum_defs_.find(type_name);
                    if (enum_def_it != enum_defs_.end() && enum_def_it->second) {
                        const ast::EnumDecl* enum_decl = enum_def_it->second;
                        for (const auto& member : enum_decl->members) {
                            if (member.name == method_name) {
                                // 引数チェック
                                if (member.has_data() && !call.args.empty()) {
                                    infer_type(*call.args[0]);
                                }

                                auto result_type = ast::make_named(type_name);
                                debug::tc::log(debug::tc::Id::Resolved,
                                               "Enum constructor: " + ident->name +
                                                   "() : " + ast::type_to_string(*result_type),
                                               debug::Level::Debug);
                                return result_type;
                            }
                        }
                    }
                }
            }

            error(current_span_, "'" + ident->name + "' is not a function");
            return ast::make_error();
        }

        // 関数ポインタ型の変数からの呼び出しをチェック
        if (!sym->is_function && sym->type && sym->type->kind == ast::TypeKind::Function) {
            auto fn_type = sym->type;
            size_t arg_count = call.args.size();
            size_t param_count = fn_type->param_types.size();

            if (arg_count != param_count) {
                error(current_span_, "Function pointer '" + ident->name + "' expects " +
                                         std::to_string(param_count) + " arguments, got " +
                                         std::to_string(arg_count));
            } else {
                for (size_t i = 0; i < arg_count; ++i) {
                    auto arg_type = infer_type(*call.args[i]);
                    if (!types_compatible(fn_type->param_types[i], arg_type)) {
                        std::string expected = ast::type_to_string(*fn_type->param_types[i]);
                        std::string actual = ast::type_to_string(*arg_type);
                        error(current_span_,
                              "Argument type mismatch in call to function pointer '" + ident->name +
                                  "': expected " + expected + ", got " + actual);
                    }
                }
            }

            return fn_type->return_type ? fn_type->return_type : ast::make_void();
        }

        if (!sym->is_function) {
            error(current_span_, "'" + ident->name + "' is not a function");
            return ast::make_error();
        }

        // 通常の関数の引数チェック（デフォルト引数と可変長引数を考慮）
        size_t arg_count = call.args.size();
        size_t param_count = sym->param_types.size();
        size_t required_count = sym->required_params;

        // 可変長引数の場合は最低限の引数数をチェック
        if (sym->is_variadic) {
            if (arg_count < param_count) {
                error(current_span_, "Variadic function '" + ident->name + "' requires at least " +
                                         std::to_string(param_count) + " arguments, got " +
                                         std::to_string(arg_count));
            } else {
                // 固定引数の型チェック
                for (size_t i = 0; i < param_count; ++i) {
                    auto arg_type = infer_type(*call.args[i]);
                    if (!types_compatible(sym->param_types[i], arg_type)) {
                        std::string expected = ast::type_to_string(*sym->param_types[i]);
                        std::string actual = ast::type_to_string(*arg_type);
                        error(current_span_, "Argument type mismatch in call to '" + ident->name +
                                                 "': expected " + expected + ", got " + actual);
                    }
                }
                // 可変長引数の型は推論のみ
                for (size_t i = param_count; i < arg_count; ++i) {
                    infer_type(*call.args[i]);
                }
            }
        } else if (arg_count < required_count || arg_count > param_count) {
            if (required_count == param_count) {
                error(current_span_, "Function '" + ident->name + "' expects " +
                                         std::to_string(param_count) + " arguments, got " +
                                         std::to_string(arg_count));
            } else {
                error(current_span_, "Function '" + ident->name + "' expects " +
                                         std::to_string(required_count) + " to " +
                                         std::to_string(param_count) + " arguments, got " +
                                         std::to_string(arg_count));
            }
        } else {
            for (size_t i = 0; i < arg_count; ++i) {
                auto arg_type = infer_type(*call.args[i]);
                if (!types_compatible(sym->param_types[i], arg_type)) {
                    std::string expected = ast::type_to_string(*sym->param_types[i]);
                    std::string actual = ast::type_to_string(*arg_type);
                    error(current_span_, "Argument type mismatch in call to '" + ident->name +
                                             "': expected " + expected + ", got " + actual);
                }
            }
        }

        return sym->return_type;
    }

    return ast::make_error();
}

ast::TypePtr TypeChecker::infer_member(ast::MemberExpr& member) {
    auto obj_type = infer_type(*member.object);
    if (!obj_type) {
        return ast::make_error();
    }

    std::string type_name = ast::type_to_string(*obj_type);

    // メソッド呼び出しの場合
    if (member.is_method_call) {
        // 配列型のビルトインメソッド
        if (obj_type->kind == ast::TypeKind::Array) {
            return infer_array_method(member, obj_type);
        }

        // 文字列型のビルトインメソッド
        if (obj_type->kind == ast::TypeKind::String) {
            return infer_string_method(member, obj_type);
        }

        // ポインタ型はビルトインメソッドを持たない
        if (obj_type->kind == ast::TypeKind::Pointer) {
            error(current_span_,
                  "Pointer type does not support method calls. Use (*ptr).method() instead.");
            return ast::make_error();
        }

        // メソッド検索用の型名リストを作成（名前空間付きと名前空間なし）
        std::vector<std::string> type_names_to_search = {type_name};
        size_t last_colon = type_name.rfind("::");
        if (last_colon != std::string::npos) {
            type_names_to_search.push_back(type_name.substr(last_colon + 2));
        }

        // 通常のメソッドを探す
        for (const auto& search_type : type_names_to_search) {
            auto it = type_methods_.find(search_type);
            if (it != type_methods_.end()) {
                auto method_it = it->second.find(member.member);
                if (method_it != it->second.end()) {
                    const auto& method_info = method_it->second;

                    // privateメソッドのアクセスチェック
                    if (method_info.visibility == ast::Visibility::Private) {
                        if (current_impl_target_type_.empty() ||
                            (current_impl_target_type_ != type_name &&
                             current_impl_target_type_ != search_type)) {
                            error(current_span_, "Cannot call private method '" + member.member +
                                                     "' from outside impl block of '" + type_name +
                                                     "'");
                            return ast::make_error();
                        }
                    }

                    // 引数の型チェック
                    if (member.args.size() != method_info.param_types.size()) {
                        error(current_span_, "Method '" + member.member + "' expects " +
                                                 std::to_string(method_info.param_types.size()) +
                                                 " arguments, got " +
                                                 std::to_string(member.args.size()));
                    } else {
                        for (size_t i = 0; i < member.args.size(); ++i) {
                            auto arg_type = infer_type(*member.args[i]);
                            if (!types_compatible(method_info.param_types[i], arg_type)) {
                                std::string expected =
                                    ast::type_to_string(*method_info.param_types[i]);
                                std::string actual = ast::type_to_string(*arg_type);
                                error(current_span_, "Argument type mismatch in method call '" +
                                                         member.member + "': expected " + expected +
                                                         ", got " + actual);
                            }
                        }
                    }

                    debug::tc::log(debug::tc::Id::Resolved,
                                   type_name + "." + member.member +
                                       "() : " + ast::type_to_string(*method_info.return_type),
                                   debug::Level::Debug);
                    return method_info.return_type;
                }
            }
        }

        // ジェネリック構造体の場合
        if (obj_type->kind == ast::TypeKind::Struct && !obj_type->type_args.empty()) {
            std::string generic_type_name = obj_type->name + "<";
            auto gen_it = generic_structs_.find(obj_type->name);
            if (gen_it != generic_structs_.end()) {
                for (size_t i = 0; i < gen_it->second.size(); ++i) {
                    if (i > 0)
                        generic_type_name += ", ";
                    generic_type_name += gen_it->second[i];
                }
                generic_type_name += ">";

                auto git = type_methods_.find(generic_type_name);
                if (git != type_methods_.end()) {
                    auto method_it = git->second.find(member.member);
                    if (method_it != git->second.end()) {
                        const auto& method_info = method_it->second;
                        ast::TypePtr return_type = method_info.return_type;
                        if (return_type && gen_it != generic_structs_.end()) {
                            return_type = substitute_generic_type(return_type, gen_it->second,
                                                                  obj_type->type_args);
                        }

                        debug::tc::log(debug::tc::Id::Resolved,
                                       "Generic method: " + type_name + "." + member.member +
                                           "() : " + ast::type_to_string(*return_type),
                                       debug::Level::Debug);
                        return return_type;
                    }
                }
            }
        }

        // インターフェース型の場合
        auto iface_it = interface_methods_.find(type_name);
        if (iface_it != interface_methods_.end()) {
            auto method_it = iface_it->second.find(member.member);
            if (method_it != iface_it->second.end()) {
                const auto& method_info = method_it->second;

                if (member.args.size() != method_info.param_types.size()) {
                    error(current_span_, "Method '" + member.member + "' expects " +
                                             std::to_string(method_info.param_types.size()) +
                                             " arguments, got " +
                                             std::to_string(member.args.size()));
                } else {
                    for (size_t i = 0; i < member.args.size(); ++i) {
                        auto arg_type = infer_type(*member.args[i]);
                        if (!types_compatible(method_info.param_types[i], arg_type)) {
                            std::string expected = ast::type_to_string(*method_info.param_types[i]);
                            std::string actual = ast::type_to_string(*arg_type);
                            error(current_span_, "Argument type mismatch in method call '" +
                                                     member.member + "': expected " + expected +
                                                     ", got " + actual);
                        }
                    }
                }

                debug::tc::log(debug::tc::Id::Resolved,
                               "Interface " + type_name + "." + member.member +
                                   "() : " + ast::type_to_string(*method_info.return_type),
                               debug::Level::Debug);
                return method_info.return_type;
            }
        }

        // ジェネリック型パラメータの場合
        if (generic_context_.has_type_param(type_name)) {
            debug::tc::log(debug::tc::Id::Resolved,
                           "Generic type param " + type_name + "." + member.member +
                               "() - assuming valid (constraint check deferred)",
                           debug::Level::Debug);
            return ast::make_void();
        }

        error(current_span_, "Unknown method '" + member.member + "' for type '" + type_name + "'");
        return ast::make_error();
    }

    // フィールドアクセスの場合（構造体のみ）
    if (obj_type->kind == ast::TypeKind::Struct) {
        std::string base_type_name = obj_type->name;
        const ast::StructDecl* struct_decl = get_struct(base_type_name);
        if (struct_decl) {
            for (const auto& field : struct_decl->fields) {
                if (field.name == member.member) {
                    auto resolved_field_type = resolve_typedef(field.type);

                    if (!obj_type->type_args.empty() && !struct_decl->generic_params.empty()) {
                        resolved_field_type = substitute_generic_type(
                            resolved_field_type, struct_decl->generic_params, obj_type->type_args);
                    }

                    debug::tc::log(debug::tc::Id::Resolved,
                                   type_name + "." + member.member + " : " +
                                       ast::type_to_string(*resolved_field_type),
                                   debug::Level::Trace);
                    return resolved_field_type;
                }
            }
            error(current_span_,
                  "Unknown field '" + member.member + "' in struct '" + type_name + "'");
        } else {
            error(current_span_, "Unknown struct type '" + type_name + "'");
        }
    } else {
        // ポインタ型の場合は、より具体的なエラーメッセージを表示
        if (obj_type->kind == ast::TypeKind::Pointer) {
            error(current_span_, "Cannot use '.' on pointer type '" + type_name +
                                     "'. Use '->' for field access through pointers.");
        } else {
            error(current_span_, "Field access on non-struct type '" + type_name + "'");
        }
    }

    return ast::make_error();
}

ast::TypePtr TypeChecker::infer_array_method(ast::MemberExpr& member, ast::TypePtr obj_type) {
    std::string type_name = ast::type_to_string(*obj_type);
    bool is_dynamic = !obj_type->array_size.has_value();

    if (member.member == "size" || member.member == "len" || member.member == "length") {
        if (!member.args.empty()) {
            error(current_span_, "Array " + member.member + "() takes no arguments");
        }
        debug::tc::log(debug::tc::Id::Resolved,
                       "Array builtin: " + type_name + "." + member.member + "() : uint",
                       debug::Level::Debug);
        return ast::make_uint();
    }

    // 動的配列（スライス）専用メソッド
    if (is_dynamic) {
        if (member.member == "cap" || member.member == "capacity") {
            if (!member.args.empty()) {
                error(current_span_, "Slice " + member.member + "() takes no arguments");
            }
            return ast::make_usize();
        }
        if (member.member == "push") {
            if (member.args.size() != 1) {
                error(current_span_, "Slice push() takes 1 argument");
            }
            if (!member.args.empty()) {
                infer_type(*member.args[0]);
            }
            return ast::make_void();
        }
        if (member.member == "pop") {
            if (!member.args.empty()) {
                error(current_span_, "Slice pop() takes no arguments");
            }
            return obj_type->element_type ? obj_type->element_type : ast::make_int();
        }
        if (member.member == "remove" || member.member == "delete") {
            if (member.args.size() != 1) {
                error(current_span_, "Slice " + member.member + "() takes 1 index argument");
            }
            if (!member.args.empty()) {
                infer_type(*member.args[0]);
            }
            return ast::make_void();
        }
        if (member.member == "clear") {
            if (!member.args.empty()) {
                error(current_span_, "Slice clear() takes no arguments");
            }
            return ast::make_void();
        }
    }

    if (member.member == "indexOf") {
        if (member.args.size() != 1) {
            error(current_span_, "Array indexOf() takes 1 argument");
        }
        if (!member.args.empty()) {
            infer_type(*member.args[0]);
        }
        return ast::make_int();
    }
    if (member.member == "includes" || member.member == "contains") {
        if (member.args.size() != 1) {
            error(current_span_, "Array " + member.member + "() takes 1 argument");
        }
        if (!member.args.empty()) {
            infer_type(*member.args[0]);
        }
        return ast::make_bool();
    }
    if (member.member == "some") {
        if (member.args.size() != 1) {
            error(current_span_, "Array some() takes 1 predicate function");
        }
        if (!member.args.empty()) {
            infer_type(*member.args[0]);
        }
        return ast::make_bool();
    }
    if (member.member == "every") {
        if (member.args.size() != 1) {
            error(current_span_, "Array every() takes 1 predicate function");
        }
        if (!member.args.empty()) {
            infer_type(*member.args[0]);
        }
        return ast::make_bool();
    }
    if (member.member == "findIndex") {
        if (member.args.size() != 1) {
            error(current_span_, "Array findIndex() takes 1 predicate function");
        }
        if (!member.args.empty()) {
            infer_type(*member.args[0]);
        }
        return ast::make_int();
    }
    if (member.member == "reduce") {
        if (member.args.size() < 1 || member.args.size() > 2) {
            error(current_span_, "Array reduce() takes 1-2 arguments (callback, [initial])");
        }
        for (auto& arg : member.args) {
            infer_type(*arg);
        }
        return ast::make_int();
    }
    if (member.member == "forEach") {
        if (member.args.size() != 1) {
            error(current_span_, "Array forEach() takes 1 callback function");
        }
        if (!member.args.empty()) {
            infer_type(*member.args[0]);
        }
        return ast::make_void();
    }
    if (member.member == "map") {
        if (member.args.size() != 1) {
            error(current_span_, "Array map() takes 1 callback function");
        }
        if (!member.args.empty()) {
            auto callback_type = infer_type(*member.args[0]);
            // コールバックの戻り値型から結果配列の要素型を推論
            if (callback_type && callback_type->kind == ast::TypeKind::Function &&
                callback_type->return_type) {
                // 結果は変換後の要素型の配列
                auto result_elem_type = callback_type->return_type;
                // 配列型を作成（サイズは元の配列と同じ）
                return ast::make_array(result_elem_type, obj_type->array_size);
            }
        }
        // フォールバック：元と同じ配列型を返す
        return ast::make_array(obj_type->element_type, obj_type->array_size);
    }
    if (member.member == "filter") {
        if (member.args.size() != 1) {
            error(current_span_, "Array filter() takes 1 predicate function");
        }
        if (!member.args.empty()) {
            infer_type(*member.args[0]);
        }
        // フィルター結果は同じ要素型の配列（サイズは動的）
        return ast::make_array(obj_type->element_type, 0);
    }
    if (member.member == "reverse") {
        if (!member.args.empty()) {
            error(current_span_, "Array reverse() takes no arguments");
        }
        // 逆順の動的配列を返す（サイズは動的）
        return ast::make_array(obj_type->element_type, std::nullopt);
    }
    if (member.member == "sort") {
        if (!member.args.empty()) {
            error(current_span_,
                  "Array sort() takes no arguments (use sortBy for custom comparator)");
        }
        // ソート済み動的配列を返す（サイズは動的）
        return ast::make_array(obj_type->element_type, std::nullopt);
    }
    if (member.member == "sortBy") {
        if (member.args.size() != 1) {
            error(current_span_, "Array sortBy() takes 1 comparator function");
        }
        if (!member.args.empty()) {
            infer_type(*member.args[0]);
        }
        // ソート済み配列を返す（同じ型）
        return ast::make_array(obj_type->element_type, obj_type->array_size);
    }
    if (member.member == "first") {
        if (!member.args.empty()) {
            error(current_span_, "Array first() takes no arguments");
        }
        // 最初の要素を返す
        return obj_type->element_type ? obj_type->element_type : ast::make_error();
    }
    if (member.member == "last") {
        if (!member.args.empty()) {
            error(current_span_, "Array last() takes no arguments");
        }
        // 最後の要素を返す
        return obj_type->element_type ? obj_type->element_type : ast::make_error();
    }
    if (member.member == "find") {
        if (member.args.size() != 1) {
            error(current_span_, "Array find() takes 1 predicate function");
        }
        if (!member.args.empty()) {
            infer_type(*member.args[0]);
        }
        // 見つかった要素を返す（オプショナル型が望ましいが、現時点では要素型を返す）
        return obj_type->element_type ? obj_type->element_type : ast::make_error();
    }
    if (member.member == "dim") {
        if (!member.args.empty()) {
            error(current_span_, "Array dim() takes no arguments");
        }
        // 次元数（整数）を返す
        return ast::make_int();
    }

    // ユーザー定義のimplメソッドを検索（impl int[] for Interface など）
    std::string type_key = ast::type_to_string(*obj_type);
    auto impl_it = type_methods_.find(type_key);
    if (impl_it != type_methods_.end()) {
        auto method_it = impl_it->second.find(member.member);
        if (method_it != impl_it->second.end()) {
            const auto& method_info = method_it->second;
            // 引数の型チェック
            if (member.args.size() != method_info.param_types.size()) {
                error(current_span_, "Method '" + member.member + "' expects " +
                                         std::to_string(method_info.param_types.size()) +
                                         " arguments, got " + std::to_string(member.args.size()));
            } else {
                for (size_t i = 0; i < member.args.size(); ++i) {
                    auto arg_type = infer_type(*member.args[i]);
                    if (!types_compatible(method_info.param_types[i], arg_type)) {
                        std::string expected = ast::type_to_string(*method_info.param_types[i]);
                        std::string actual = ast::type_to_string(*arg_type);
                        error(current_span_, "Argument type mismatch in method call '" +
                                                 member.member + "': expected " + expected +
                                                 ", got " + actual);
                    }
                }
            }
            debug::tc::log(debug::tc::Id::Resolved,
                           "Array impl method: " + type_key + "." + member.member +
                               "() : " + ast::type_to_string(*method_info.return_type),
                           debug::Level::Debug);
            return method_info.return_type;
        }
    }

    // 固定長配列の場合、スライス型（T[]）へのフォールバック検索
    if (obj_type->array_size.has_value() && obj_type->element_type) {
        std::string slice_key = ast::type_to_string(*obj_type->element_type) + "[]";
        auto slice_impl_it = type_methods_.find(slice_key);
        if (slice_impl_it != type_methods_.end()) {
            auto method_it = slice_impl_it->second.find(member.member);
            if (method_it != slice_impl_it->second.end()) {
                const auto& method_info = method_it->second;
                // 引数の型チェック
                if (member.args.size() != method_info.param_types.size()) {
                    error(current_span_, "Method '" + member.member + "' expects " +
                                             std::to_string(method_info.param_types.size()) +
                                             " arguments, got " +
                                             std::to_string(member.args.size()));
                } else {
                    for (size_t i = 0; i < member.args.size(); ++i) {
                        auto arg_type = infer_type(*member.args[i]);
                        if (!types_compatible(method_info.param_types[i], arg_type)) {
                            std::string expected = ast::type_to_string(*method_info.param_types[i]);
                            std::string actual = ast::type_to_string(*arg_type);
                            error(current_span_, "Argument type mismatch in method call '" +
                                                     member.member + "': expected " + expected +
                                                     ", got " + actual);
                        }
                    }
                }
                debug::tc::log(debug::tc::Id::Resolved,
                               "Array impl fallback to slice: " + type_key + " -> " + slice_key +
                                   "." + member.member +
                                   "() : " + ast::type_to_string(*method_info.return_type),
                               debug::Level::Debug);
                return method_info.return_type;
            }
        }
    }

    error(current_span_, "Unknown array method '" + member.member + "'");
    return ast::make_error();
}

ast::TypePtr TypeChecker::infer_string_method(ast::MemberExpr& member, ast::TypePtr obj_type) {
    std::string type_name = ast::type_to_string(*obj_type);

    if (member.member == "len" || member.member == "size" || member.member == "length") {
        if (!member.args.empty()) {
            error(current_span_, "String " + member.member + "() takes no arguments");
        }
        debug::tc::log(debug::tc::Id::Resolved,
                       "String builtin: " + type_name + "." + member.member + "() : uint",
                       debug::Level::Debug);
        return ast::make_uint();
    }
    if (member.member == "charAt" || member.member == "at") {
        if (member.args.size() != 1) {
            error(current_span_, "String " + member.member + "() takes 1 argument");
        } else {
            auto arg_type = infer_type(*member.args[0]);
            if (!arg_type->is_integer()) {
                error(current_span_, "String " + member.member + "() index must be integer");
            }
        }
        return ast::make_char();
    }
    if (member.member == "substring" || member.member == "slice") {
        if (member.args.size() < 1 || member.args.size() > 2) {
            error(current_span_, "String " + member.member + "() takes 1-2 arguments");
        } else {
            for (auto& arg : member.args) {
                auto arg_type = infer_type(*arg);
                if (!arg_type->is_integer()) {
                    error(current_span_,
                          "String " + member.member + "() arguments must be integers");
                }
            }
        }
        return ast::make_string();
    }
    if (member.member == "indexOf") {
        if (member.args.size() != 1) {
            error(current_span_, "String indexOf() takes 1 argument");
        } else {
            auto arg_type = infer_type(*member.args[0]);
            if (arg_type->kind != ast::TypeKind::String) {
                error(current_span_, "String indexOf() argument must be string");
            }
        }
        return ast::make_int();
    }
    if (member.member == "toUpperCase" || member.member == "toLowerCase" ||
        member.member == "trim") {
        if (!member.args.empty()) {
            error(current_span_, "String " + member.member + "() takes no arguments");
        }
        return ast::make_string();
    }
    if (member.member == "startsWith" || member.member == "endsWith" ||
        member.member == "includes" || member.member == "contains") {
        if (member.args.size() != 1) {
            error(current_span_, "String " + member.member + "() takes 1 argument");
        } else {
            auto arg_type = infer_type(*member.args[0]);
            if (arg_type->kind != ast::TypeKind::String) {
                error(current_span_, "String " + member.member + "() argument must be string");
            }
        }
        return ast::make_bool();
    }
    if (member.member == "repeat") {
        if (member.args.size() != 1) {
            error(current_span_, "String repeat() takes 1 argument");
        } else {
            auto arg_type = infer_type(*member.args[0]);
            if (!arg_type->is_integer()) {
                error(current_span_, "String repeat() count must be integer");
            }
        }
        return ast::make_string();
    }
    if (member.member == "replace") {
        if (member.args.size() != 2) {
            error(current_span_, "String replace() takes 2 arguments");
        } else {
            for (auto& arg : member.args) {
                auto arg_type = infer_type(*arg);
                if (arg_type->kind != ast::TypeKind::String) {
                    error(current_span_, "String replace() arguments must be strings");
                }
            }
        }
        return ast::make_string();
    }
    if (member.member == "first") {
        if (!member.args.empty()) {
            error(current_span_, "String first() takes no arguments");
        }
        return ast::make_char();
    }
    if (member.member == "last") {
        if (!member.args.empty()) {
            error(current_span_, "String last() takes no arguments");
        }
        return ast::make_char();
    }

    error(current_span_, "Unknown string method '" + member.member + "'");
    return ast::make_error();
}

}  // namespace cm
