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
            error(current_span_, i18n::msg(i18n::MsgId::TcPointerTypeDoesNotSupport));
            return ast::make_error();
        }

        // メソッド検索用の型名リストを作成（名前空間付きと名前空間なし）
        std::vector<std::string> type_names_to_search = {type_name};
        size_t last_colon = type_name.rfind("::");
        if (last_colon != std::string::npos) {
            type_names_to_search.push_back(type_name.substr(last_colon + 2));
        }
        // Q5: int正規化された値enum（resolve_typedefがnameへ元enum名を保持）はenum名でもメソッド表を引く（impl Color { string name() {...} }のinherent implを解決可能にする）
        if (obj_type->kind == ast::TypeKind::Int && !obj_type->name.empty() &&
            enum_names_.count(obj_type->name) > 0) {
            type_names_to_search.push_back(obj_type->name);
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
                            error(current_span_,
                                  i18n::msgf(i18n::MsgId::TcCannotCallPrivateMethodFrom,
                                             member.member, type_name));
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
                                error(current_span_,
                                      i18n::msgf(i18n::MsgId::TcArgumentTypeMismatchMethodCall,
                                                 member.member, expected, actual));
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
            auto gen_it = generic_structs_.find(obj_type->name);
            if (gen_it != generic_structs_.end()) {
                // 定義キー（G<T, U>形）は正準関数で計算する（登録側type_to_string形とバイト一致）
                std::string generic_type_name = generic_def_method_key(obj_type->name);

                auto git = type_methods_.find(generic_type_name);
                if (git != type_methods_.end()) {
                    auto method_it = git->second.find(member.member);
                    if (method_it != git->second.end()) {
                        const auto& method_info = method_it->second;
                        // 引数へ型を注釈する（typed-hir-single-source 第2段）。
                        // 期待型は型引数を代入したパラメータ型（無名リテラルの型決定にも必要）
                        for (size_t i = 0; i < member.args.size(); ++i) {
                            ast::TypePtr expected =
                                (i < method_info.param_types.size())
                                    ? substitute_generic_type(method_info.param_types[i],
                                                              gen_it->second, obj_type->type_args)
                                    : nullptr;
                            infer_type_expecting(*member.args[i], expected);
                        }
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
                            error(current_span_,
                                  i18n::msgf(i18n::MsgId::TcArgumentTypeMismatchMethodCall,
                                             member.member, expected, actual));
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
            // 境界インターフェイス（<T: Shape>等）の宣言シグネチャからメソッドを解決し、戻り値型を式型へ伝搬する
            if (const auto* type_param = generic_context_.get_type_param(type_name)) {
                for (const auto& bound : type_param->bounds) {
                    auto bound_it = interface_methods_.find(bound);
                    if (bound_it == interface_methods_.end()) {
                        continue;
                    }
                    auto method_it = bound_it->second.find(member.member);
                    if (method_it == bound_it->second.end()) {
                        continue;
                    }
                    const auto& method_info = method_it->second;

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
                                error(current_span_,
                                      i18n::msgf(i18n::MsgId::TcArgumentTypeMismatchMethodCall,
                                                 member.member, expected, actual));
                            }
                        }
                    }

                    // 戻り値型がインターフェイス自身の型パラメータ（Cloneのclone()等）の場合はレシーバ型で置き換える
                    ast::TypePtr bound_return = method_info.return_type;
                    if (bound_return && bound_return->kind == ast::TypeKind::Generic) {
                        bound_return = obj_type;
                    }
                    debug::tc::log(debug::tc::Id::Resolved,
                                   "Generic bound " + bound + "." + member.member +
                                       "() : " + ast::type_to_string(*bound_return),
                                   debug::Level::Debug);
                    return bound_return;
                }
            }
            debug::tc::log(debug::tc::Id::Resolved,
                           "Generic type param " + type_name + "." + member.member +
                               "() - assuming valid (constraint check deferred)",
                           debug::Level::Debug);
            return ast::make_void();
        }

        // 組み込みenum型（Result<T,E>/Option<T>）のメソッド
        // type_methods_はベース名（"Result"）で登録されているため、インスタンス化名（"Result<int, string>" / "Result__int__string"）から
        // ベース名で引き、戻り値・引数のジェネリックパラメータを型引数で置換する
        const std::string enum_base = strip_spec_suffix(obj_type->name);
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
                                error(current_span_,
                                      i18n::msgf(i18n::MsgId::TcArgumentTypeMismatchMethodCall,
                                                 member.member, ast::type_to_string(*expected_type),
                                                 ast::type_to_string(*arg_type)));
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
                        error(
                            current_span_,
                            i18n::msgf(i18n::MsgId::TcFunctionFieldExpectsArguments, member.member,
                                       std::to_string(field_type->param_types.size()),
                                       std::to_string(member.args.size())));
                        return ast::make_error();
                    }
                    for (size_t i = 0; i < member.args.size(); ++i) {
                        auto arg_type = infer_type(*member.args[i]);
                        if (!types_compatible(field_type->param_types[i], arg_type)) {
                            error(current_span_,
                                  i18n::msgf(i18n::MsgId::TcArgumentTypeMismatchFunctionField,
                                             member.member,
                                             ast::type_to_string(*field_type->param_types[i]),
                                             ast::type_to_string(*arg_type)));
                        }
                    }
                    return field_type->return_type ? field_type->return_type : ast::make_void();
                }
            }
        }

        error(current_span_,
              i18n::msgf(i18n::MsgId::TcUnknownMethodType, member.member, type_name));
        return ast::make_error();
    }

    // フィールドアクセスの場合（構造体のみ）
    if (obj_type->kind == ast::TypeKind::Struct) {
        std::string base_type_name = obj_type->name;
        const ast::StructDecl* struct_decl = get_struct(base_type_name);
        if (struct_decl) {
            for (const auto& field : struct_decl->fields) {
                if (field.name == member.member) {
                    // privateフィールドの外部アクセス検査（X2）。
                    // メソッド側の検査（Cannot call private method）と同じimpl粒度で、
                    // 従来はフィールドだけ無検査で読み書きが素通りしていた
                    if (field.visibility == ast::Visibility::Private) {
                        if (current_impl_target_type_.empty() ||
                            (current_impl_target_type_ != type_name &&
                             current_impl_target_type_ != base_type_name)) {
                            error(current_span_,
                                  i18n::msgf(i18n::MsgId::TcCannotAccessPrivateFieldFrom,
                                             member.member, base_type_name));
                            return ast::make_error();
                        }
                    }
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
                  i18n::msgf(i18n::MsgId::TcUnknownFieldStruct, member.member, type_name));
        } else {
            error(current_span_, i18n::msgf(i18n::MsgId::TcUnknownStructType, type_name));
        }
    } else {
        // ポインタ型の場合は、より具体的なエラーメッセージを表示
        if (obj_type->kind == ast::TypeKind::Pointer) {
            error(current_span_,
                  i18n::msgf(i18n::MsgId::TcCannotPointerTypeFieldAccess, type_name));
        } else {
            error(current_span_, i18n::msgf(i18n::MsgId::TcFieldAccessNonStructType, type_name));
        }
    }

    return ast::make_error();
}

ast::TypePtr TypeChecker::infer_array_method(ast::MemberExpr& member, ast::TypePtr obj_type) {
    std::string type_name = ast::type_to_string(*obj_type);
    bool is_dynamic = !obj_type->array_size.has_value();

    if (member.member == "size" || member.member == "len" || member.member == "length") {
        if (!member.args.empty()) {
            error(current_span_, i18n::msgf(i18n::MsgId::TcArrayTakesNoArguments, member.member));
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
                error(current_span_,
                      i18n::msgf(i18n::MsgId::TcSliceTakesNoArguments, member.member));
            }
            return ast::make_usize();
        }
        if (member.member == "push") {
            if (member.args.size() != 1) {
                error(current_span_, i18n::msg(i18n::MsgId::TcSlicePushTakes1Argument));
            }
            if (!member.args.empty()) {
                // レシーバの要素型を期待型として引数へ渡す（X3/X4。無名リテラルの型決定はinfer_type_expectingへ一元化）
                auto push_arg_type = infer_type_expecting(*member.args[0], obj_type->element_type);
                // 要素型と引数型の互換検査（Z4穴1: 従来は無検査でエラー型が下流へ漏れ内部エラーになっていた）。
                // ユニオン要素への変種値push（Y3）はユニオン構築で受けるため互換とみなす
                if (obj_type->element_type && push_arg_type &&
                    push_arg_type->kind != ast::TypeKind::Error) {
                    auto elem_resolved = resolve_typedef(obj_type->element_type);
                    auto arg_resolved = resolve_typedef(push_arg_type);
                    bool ok = types_compatible(obj_type->element_type, push_arg_type);
                    if (!ok && elem_resolved && arg_resolved &&
                        elem_resolved->kind == ast::TypeKind::Union) {
                        for (const auto& v : ast::union_variant_types(elem_resolved)) {
                            if (v && types_compatible(v, push_arg_type)) {
                                ok = true;
                                break;
                            }
                        }
                    }
                    if (!ok) {
                        error(current_span_, i18n::msgf(i18n::MsgId::TcSlicePushTypeMismatch,
                                                        ast::type_to_string(*elem_resolved),
                                                        ast::type_to_string(*arg_resolved)));
                        return ast::make_void();
                    }
                }
                // キャプチャ付きクロージャのスライス格納は環境喪失・未解決シンボルになるため拒否（V7）
                if (obj_type->element_type &&
                    obj_type->element_type->kind == ast::TypeKind::Function &&
                    is_capturing_closure_expr(*member.args[0])) {
                    error(current_span_, i18n::msg(i18n::MsgId::TcCannotPushCapturingClosureInto));
                }
            }
            return ast::make_void();
        }
        if (member.member == "pop") {
            if (!member.args.empty()) {
                error(current_span_, i18n::msg(i18n::MsgId::TcSlicePopTakesNoArguments));
            }
            return obj_type->element_type ? obj_type->element_type : ast::make_int();
        }
        if (member.member == "remove" || member.member == "delete") {
            if (member.args.size() != 1) {
                error(current_span_,
                      i18n::msgf(i18n::MsgId::TcSliceTakes1IndexArgument, member.member));
            }
            if (!member.args.empty()) {
                infer_type(*member.args[0]);
            }
            return ast::make_void();
        }
        if (member.member == "clear") {
            if (!member.args.empty()) {
                error(current_span_, i18n::msg(i18n::MsgId::TcSliceClearTakesNoArguments));
            }
            return ast::make_void();
        }
    }

    // 検索ビルトインの要素型サポート判定（Z1）。
    // 値比較系（indexOf/includes/contains）はスカラ+stringの変種、述語系（some/every/findIndex）はi32/i64変種のみ存在する。
    // 未対応の要素型は黙ってi32変種へ落とさず診断で停止する
    auto search_elem_supported = [&](bool value_compare) -> bool {
        auto elem = obj_type->element_type ? resolve_typedef(obj_type->element_type) : nullptr;
        if (!elem) {
            return true;
        }
        switch (elem->kind) {
            case ast::TypeKind::Bool:
            case ast::TypeKind::Char:
            case ast::TypeKind::Tiny:
            case ast::TypeKind::UTiny:
            case ast::TypeKind::Short:
            case ast::TypeKind::UShort:
            case ast::TypeKind::Float:
            case ast::TypeKind::UFloat:
            case ast::TypeKind::Double:
            case ast::TypeKind::UDouble:
            case ast::TypeKind::String:
                return value_compare;
            case ast::TypeKind::Int:
            case ast::TypeKind::UInt:
            case ast::TypeKind::Long:
            case ast::TypeKind::ULong:
            case ast::TypeKind::ISize:
            case ast::TypeKind::USize:
                return true;
            default:
                return false;
        }
    };
    auto diag_unsupported_elem = [&](const std::string& method) {
        auto elem = obj_type->element_type ? resolve_typedef(obj_type->element_type) : nullptr;
        error(current_span_, i18n::msgf(i18n::MsgId::TcArraySearchUnsupportedElem, method,
                                        elem ? ast::type_to_string(*elem) : "unknown"));
    };
    if (member.member == "indexOf") {
        if (member.args.size() != 1) {
            error(current_span_, i18n::msg(i18n::MsgId::TcArrayIndexofTakes1Argument));
        }
        if (!search_elem_supported(true)) {
            diag_unsupported_elem("indexOf");
            return ast::make_error();
        }
        if (!member.args.empty()) {
            infer_type(*member.args[0]);
        }
        return ast::make_int();
    }
    if (member.member == "includes" || member.member == "contains") {
        if (member.args.size() != 1) {
            error(current_span_, i18n::msgf(i18n::MsgId::TcArrayTakes1Argument, member.member));
        }
        if (!search_elem_supported(true)) {
            diag_unsupported_elem(member.member);
            return ast::make_error();
        }
        if (!member.args.empty()) {
            infer_type(*member.args[0]);
        }
        return ast::make_bool();
    }
    if (member.member == "some") {
        if (member.args.size() != 1) {
            error(current_span_, i18n::msg(i18n::MsgId::TcArraySomeTakes1Predicate));
        }
        if (!search_elem_supported(false)) {
            diag_unsupported_elem("some");
            return ast::make_error();
        }
        if (!member.args.empty()) {
            infer_type(*member.args[0]);
        }
        return ast::make_bool();
    }
    if (member.member == "every") {
        if (member.args.size() != 1) {
            error(current_span_, i18n::msg(i18n::MsgId::TcArrayEveryTakes1Predicate));
        }
        if (!search_elem_supported(false)) {
            diag_unsupported_elem("every");
            return ast::make_error();
        }
        if (!member.args.empty()) {
            infer_type(*member.args[0]);
        }
        return ast::make_bool();
    }
    if (member.member == "findIndex") {
        if (member.args.size() != 1) {
            error(current_span_, i18n::msg(i18n::MsgId::TcArrayFindindexTakes1Predicate));
        }
        if (!search_elem_supported(false)) {
            diag_unsupported_elem("findIndex");
            return ast::make_error();
        }
        if (!member.args.empty()) {
            infer_type(*member.args[0]);
        }
        return ast::make_int();
    }
    if (member.member == "reduce") {
        if (member.args.size() < 1 || member.args.size() > 2) {
            error(current_span_, i18n::msg(i18n::MsgId::TcArrayReduceTakes12));
        }
        for (auto& arg : member.args) {
            infer_type(*arg);
        }
        return ast::make_int();
    }
    if (member.member == "forEach") {
        if (member.args.size() != 1) {
            error(current_span_, i18n::msg(i18n::MsgId::TcArrayForeachTakes1Callback));
        }
        if (!member.args.empty()) {
            infer_type(*member.args[0]);
        }
        return ast::make_void();
    }
    if (member.member == "map") {
        if (member.args.size() != 1) {
            error(current_span_, i18n::msg(i18n::MsgId::TcArrayMapTakes1Callback));
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
            error(current_span_, i18n::msg(i18n::MsgId::TcArrayFilterTakes1Predicate));
        }
        if (!member.args.empty()) {
            infer_type(*member.args[0]);
        }
        // フィルター結果は同じ要素型の配列（サイズは動的）
        return ast::make_array(obj_type->element_type, 0);
    }
    if (member.member == "reverse") {
        if (!member.args.empty()) {
            error(current_span_, i18n::msg(i18n::MsgId::TcArrayReverseTakesNoArguments));
        }
        // 逆順の動的配列を返す（サイズは動的）
        return ast::make_array(obj_type->element_type, std::nullopt);
    }
    if (member.member == "sort") {
        if (!member.args.empty()) {
            error(current_span_, i18n::msg(i18n::MsgId::TcArraySortTakesNoArguments));
        }
        // ソート済み動的配列を返す（サイズは動的）
        return ast::make_array(obj_type->element_type, std::nullopt);
    }
    if (member.member == "sortBy") {
        if (member.args.size() != 1) {
            error(current_span_, i18n::msg(i18n::MsgId::TcArraySortbyTakes1Comparator));
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
            error(current_span_, i18n::msg(i18n::MsgId::TcArrayGetTakes1Index));
        } else {
            auto idx_type = infer_type(*member.args[0]);
            if (idx_type && !idx_type->is_integer()) {
                error(current_span_, i18n::msg(i18n::MsgId::TcArrayGetIndexMustInteger));
            }
        }
        auto opt = std::make_shared<ast::Type>(ast::TypeKind::Struct);
        opt->name = "Option";
        opt->type_args.push_back(obj_type->element_type ? obj_type->element_type : ast::make_int());
        return opt;
    }
    if (member.member == "first") {
        if (!member.args.empty()) {
            error(current_span_, i18n::msg(i18n::MsgId::TcArrayFirstTakesNoArguments));
        }
        // 最初の要素を返す
        return obj_type->element_type ? obj_type->element_type : ast::make_error();
    }
    if (member.member == "last") {
        if (!member.args.empty()) {
            error(current_span_, i18n::msg(i18n::MsgId::TcArrayLastTakesNoArguments));
        }
        // 最後の要素を返す
        return obj_type->element_type ? obj_type->element_type : ast::make_error();
    }
    if (member.member == "find") {
        if (member.args.size() != 1) {
            error(current_span_, i18n::msg(i18n::MsgId::TcArrayFindTakes1Predicate));
        }
        if (!member.args.empty()) {
            infer_type(*member.args[0]);
        }
        // 見つかった要素を返す（オプショナル型が望ましいが、現時点では要素型を返す）
        return obj_type->element_type ? obj_type->element_type : ast::make_error();
    }
    if (member.member == "dim") {
        if (!member.args.empty()) {
            error(current_span_, i18n::msg(i18n::MsgId::TcArrayDimTakesNoArguments));
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
                        error(current_span_,
                              i18n::msgf(i18n::MsgId::TcArgumentTypeMismatchMethodCall,
                                         member.member, expected, actual));
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
                            error(current_span_,
                                  i18n::msgf(i18n::MsgId::TcArgumentTypeMismatchMethodCall,
                                             member.member, expected, actual));
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

    error(current_span_, i18n::msgf(i18n::MsgId::TcUnknownArrayMethod, member.member));
    return ast::make_error();
}

ast::TypePtr TypeChecker::infer_string_method(ast::MemberExpr& member, ast::TypePtr obj_type) {
    std::string type_name = ast::type_to_string(*obj_type);

    if (member.member == "len" || member.member == "size" || member.member == "length" ||
        member.member == "byte_len") {
        if (!member.args.empty()) {
            error(current_span_, i18n::msgf(i18n::MsgId::TcStringTakesNoArguments, member.member));
        }
        debug::tc::log(debug::tc::Id::Resolved,
                       "String builtin: " + type_name + "." + member.member + "() : uint",
                       debug::Level::Debug);
        return ast::make_uint();
    }
    if (member.member == "chars") {
        if (!member.args.empty()) {
            error(current_span_, i18n::msg(i18n::MsgId::TcStringCharsTakesNoArguments));
        }
        return ast::make_array(ast::make_uint());
    }
    if (member.member == "codepoint_at") {
        if (member.args.size() != 1) {
            error(current_span_, i18n::msg(i18n::MsgId::TcStringCodepointAtTakes1));
        } else {
            auto arg_type = infer_type(*member.args[0]);
            if (!arg_type->is_integer()) {
                error(current_span_, i18n::msg(i18n::MsgId::TcStringCodepointAtIndexMust));
            }
        }
        return ast::make_uint();
    }
    if (member.member == "byte_at") {
        // R2: バイト系アクセス（byte_lenと対）。バイト添字の生バイト値（0..255・int）を返す
        if (member.args.size() != 1) {
            error(current_span_, i18n::msgf(i18n::MsgId::TcStringTakes1Argument, member.member));
        } else {
            auto arg_type = infer_type(*member.args[0]);
            if (!arg_type->is_integer()) {
                error(current_span_,
                      i18n::msgf(i18n::MsgId::TcStringIndexMustInteger, member.member));
            }
        }
        return ast::make_int();
    }
    if (member.member == "charAt" || member.member == "at") {
        if (member.args.size() != 1) {
            error(current_span_, i18n::msgf(i18n::MsgId::TcStringTakes1Argument, member.member));
        } else {
            auto arg_type = infer_type(*member.args[0]);
            if (!arg_type->is_integer()) {
                error(current_span_,
                      i18n::msgf(i18n::MsgId::TcStringIndexMustInteger, member.member));
            }
        }
        return ast::make_char();
    }
    if (member.member == "substring" || member.member == "slice") {
        if (member.args.size() < 1 || member.args.size() > 2) {
            error(current_span_, i18n::msgf(i18n::MsgId::TcStringTakes12Arguments, member.member));
        } else {
            for (auto& arg : member.args) {
                auto arg_type = infer_type(*arg);
                if (!arg_type->is_integer()) {
                    error(current_span_,
                          i18n::msgf(i18n::MsgId::TcStringArgumentsMustIntegers, member.member));
                }
            }
        }
        return ast::make_string();
    }
    if (member.member == "indexOf") {
        if (member.args.size() != 1) {
            error(current_span_, i18n::msg(i18n::MsgId::TcStringIndexofTakes1Argument));
        } else {
            auto arg_type = infer_type(*member.args[0]);
            if (arg_type->kind != ast::TypeKind::String) {
                error(current_span_, i18n::msg(i18n::MsgId::TcStringIndexofArgumentMustString));
            }
        }
        return ast::make_int();
    }
    if (member.member == "toUpperCase" || member.member == "toLowerCase" ||
        member.member == "trim") {
        if (!member.args.empty()) {
            error(current_span_, i18n::msgf(i18n::MsgId::TcStringTakesNoArguments, member.member));
        }
        return ast::make_string();
    }
    if (member.member == "startsWith" || member.member == "endsWith" ||
        member.member == "includes" || member.member == "contains") {
        if (member.args.size() != 1) {
            error(current_span_, i18n::msgf(i18n::MsgId::TcStringTakes1Argument, member.member));
        } else {
            auto arg_type = infer_type(*member.args[0]);
            if (arg_type->kind != ast::TypeKind::String) {
                error(current_span_,
                      i18n::msgf(i18n::MsgId::TcStringArgumentMustString, member.member));
            }
        }
        return ast::make_bool();
    }
    if (member.member == "repeat") {
        if (member.args.size() != 1) {
            error(current_span_, i18n::msg(i18n::MsgId::TcStringRepeatTakes1Argument));
        } else {
            auto arg_type = infer_type(*member.args[0]);
            if (!arg_type->is_integer()) {
                error(current_span_, i18n::msg(i18n::MsgId::TcStringRepeatCountMustInteger));
            }
        }
        return ast::make_string();
    }
    if (member.member == "replace") {
        if (member.args.size() != 2) {
            error(current_span_, i18n::msg(i18n::MsgId::TcStringReplaceTakes2Arguments));
        } else {
            for (auto& arg : member.args) {
                auto arg_type = infer_type(*arg);
                if (arg_type->kind != ast::TypeKind::String) {
                    error(current_span_,
                          i18n::msg(i18n::MsgId::TcStringReplaceArgumentsMustStrings));
                }
            }
        }
        return ast::make_string();
    }
    if (member.member == "first") {
        if (!member.args.empty()) {
            error(current_span_, i18n::msg(i18n::MsgId::TcStringFirstTakesNoArguments));
        }
        return ast::make_char();
    }
    if (member.member == "last") {
        if (!member.args.empty()) {
            error(current_span_, i18n::msg(i18n::MsgId::TcStringLastTakesNoArguments));
        }
        return ast::make_char();
    }

    error(current_span_, i18n::msgf(i18n::MsgId::TcUnknownStringMethod, member.member));
    return ast::make_error();
}

}  // namespace cm
