// ============================================================
// TypeChecker 実装 - メンバアクセス・メソッド呼び出しと配列・文字列ビルトインメソッドの型推論
// ============================================================

#include "internal/base/i18n.hpp"
#include "internal/types/type_checker.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace cm {

namespace {

// メソッド呼び出しの引数個数を検査する（デフォルト引数による省略を考慮）。
// 不適合ならエラーメッセージを、適合なら空文字列を返す
std::string method_arity_error(const std::string& method_name, size_t arg_count,
                               const MethodInfo& method_info) {
    size_t param_count = method_info.param_types.size();
    size_t required = std::min(method_info.required_params, param_count);
    if (arg_count >= required && arg_count <= param_count) {
        return "";
    }
    std::string expected = (required == param_count)
                               ? std::to_string(param_count)
                               : std::to_string(required) + " to " + std::to_string(param_count);
    return "Method '" + method_name + "' expects " + expected + " arguments, got " +
           std::to_string(arg_count);
}

}  // namespace

ast::TypePtr TypeChecker::infer_member(ast::MemberExpr& member) {
    // メソッド呼び出しの受け手は評価前に初期化済み・変更ありとしてマークする（arr.dim() / v.push() 等を「使用前の未初期化」と誤検出しない。
    // 受け手の型を推論する時点で使用チェックが先に発火するため、ここで行う）
    if (member.is_method_call) {
        if (auto* recv_ident = member.object->as<ast::IdentExpr>()) {
            mark_variable_initialized(recv_ident->name);
            mark_variable_modified(recv_ident->name);
        }
    }
    auto obj_type = infer_type(*member.object);
    if (!obj_type) {
        return ast::make_error();
    }

    std::string type_name = ast::type_to_string(*obj_type);

    // メソッド呼び出しの場合
    if (member.is_method_call) {
        // メソッドがselfを変更するかは静的に判別しないため、受け手の変数を保守的に「変更あり」として扱う（const推奨の誤提案を防ぐ）
        if (auto* recv = member.object->as<ast::IdentExpr>()) {
            mark_variable_modified(recv->name);
            mark_variable_initialized(recv->name);
        }

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

                    // 引数の個数チェック（デフォルト引数による省略を考慮）
                    std::string arity_err =
                        method_arity_error(member.member, member.args.size(), method_info);
                    if (!arity_err.empty()) {
                        error(current_span_, arity_err);
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

                std::string arity_err =
                    method_arity_error(member.member, member.args.size(), method_info);
                if (!arity_err.empty()) {
                    error(current_span_, arity_err);
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

        // 組み込みenum型（Result<T,E>/Option<T>）のメソッド
        // type_methods_はベース名（"Result"）で登録されているため、インスタンス化名（"Result<int, string>" / "Result__int__string"）から
        // ベース名で引き、戻り値・引数のジェネリックパラメータを型引数で置換する
        std::string enum_base = obj_type->name;
        {
            auto lt = enum_base.find('<');
            if (lt != std::string::npos) {
                enum_base = enum_base.substr(0, lt);
            }
            auto us = enum_base.find("__");
            if (us != std::string::npos && us > 0) {
                enum_base = enum_base.substr(0, us);
            }
        }
        if (obj_type->kind == ast::TypeKind::Struct && enum_names_.count(enum_base) > 0) {
            auto em_it = type_methods_.find(enum_base);
            auto ge_it = generic_enums_.find(enum_base);
            if (em_it != type_methods_.end()) {
                auto method_it = em_it->second.find(member.member);
                if (method_it != em_it->second.end()) {
                    const auto& method_info = method_it->second;
                    auto substitute = [&](ast::TypePtr t) -> ast::TypePtr {
                        if (t && ge_it != generic_enums_.end() && !obj_type->type_args.empty()) {
                            return substitute_generic_type(t, ge_it->second, obj_type->type_args);
                        }
                        return t;
                    };
                    std::string arity_err =
                        method_arity_error(member.member, member.args.size(), method_info);
                    if (!arity_err.empty()) {
                        error(current_span_, arity_err);
                    } else {
                        for (size_t i = 0; i < member.args.size(); ++i) {
                            auto arg_type = infer_type(*member.args[i]);
                            auto expected_type = substitute(method_info.param_types[i]);
                            if (!types_compatible(expected_type, arg_type)) {
                                error(current_span_, "Argument type mismatch in method call '" +
                                                         member.member + "': expected " +
                                                         ast::type_to_string(*expected_type) +
                                                         ", got " + ast::type_to_string(*arg_type));
                            }
                        }
                    }
                    auto return_type = substitute(method_info.return_type);
                    debug::tc::log(debug::tc::Id::Resolved,
                                   "Enum method: " + type_name + "." + member.member +
                                       "() : " + ast::type_to_string(*return_type),
                                   debug::Level::Debug);
                    return return_type;
                }
            }
        }

        // 関数型フィールドの呼び出し（obj.field(args)）: JSオブジェクトのメソッド等、関数値を保持するフィールドをメソッド呼び出し構文で起動できるようにする
        if (obj_type->kind == ast::TypeKind::Struct) {
            if (const ast::StructDecl* struct_decl = get_struct(obj_type->name)) {
                for (const auto& field : struct_decl->fields) {
                    if (field.name != member.member) {
                        continue;
                    }
                    auto field_type = resolve_typedef(field.type);
                    if (!field_type || field_type->kind != ast::TypeKind::Function) {
                        break;
                    }
                    if (member.args.size() != field_type->param_types.size()) {
                        error(current_span_, "Function field '" + member.member + "' expects " +
                                                 std::to_string(field_type->param_types.size()) +
                                                 " arguments, got " +
                                                 std::to_string(member.args.size()));
                        return ast::make_error();
                    }
                    for (size_t i = 0; i < member.args.size(); ++i) {
                        auto arg_type = infer_type(*member.args[i]);
                        if (!types_compatible(field_type->param_types[i], arg_type)) {
                            error(current_span_,
                                  "Argument type mismatch in function field call '" +
                                      member.member + "': expected " +
                                      ast::type_to_string(*field_type->param_types[i]) + ", got " +
                                      ast::type_to_string(*arg_type));
                        }
                    }
                    return field_type->return_type ? field_type->return_type : ast::make_void();
                }
            }
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
    if (member.member == "get") {
        // チェック付き要素アクセス: 範囲内ならOption::Some(要素)、範囲外ならOption::None（Rustのslice::get相当。arr[i]の範囲外アクセスを避けるための安全API）
        if (member.args.size() != 1) {
            error(current_span_, "Array get() takes 1 index argument");
        } else {
            auto idx_type = infer_type(*member.args[0]);
            if (idx_type && !idx_type->is_integer()) {
                error(current_span_, "Array get() index must be an integer");
            }
        }
        auto opt = std::make_shared<ast::Type>(ast::TypeKind::Struct);
        opt->name = "Option";
        opt->type_args.push_back(obj_type->element_type ? obj_type->element_type : ast::make_int());
        return opt;
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
            // 引数の個数チェック（デフォルト引数による省略を考慮）
            std::string arity_err =
                method_arity_error(member.member, member.args.size(), method_info);
            if (!arity_err.empty()) {
                error(current_span_, arity_err);
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
                // 引数の個数チェック（デフォルト引数による省略を考慮）
                std::string arity_err =
                    method_arity_error(member.member, member.args.size(), method_info);
                if (!arity_err.empty()) {
                    error(current_span_, arity_err);
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
