// ============================================================
// TypeChecker 実装 - implブロックの登録（メソッド表・演算子・ctor/dtor）と本体検査
// ============================================================

#include "internal/base/i18n.hpp"
#include "internal/base/mangle.hpp"
#include "internal/types/type_checker.hpp"

#include <string>
#include <utility>
#include <vector>

namespace cm {

void TypeChecker::register_impl(ast::ImplDecl& impl) {
    reject_const_generic_params(impl.generic_params_v2, current_span_);
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
        // derive合成の総称impl（#[__derived]マーカー）を記録する。特殊化時のフィールド型検証が
        // auto_impls除去後もderive規則を適用できるようにする
        if (impl.target_type && !impl.target_type->type_args.empty()) {
            for (const auto& attr : impl.attributes) {
                if (attr.name == "__derived") {
                    derived_generic_impls_[impl.target_type->name].insert(impl.interface_name);
                    break;
                }
            }
        }
        // インターフェースの存在チェック（Q4: 例外→通常診断。従来はbuild側のcatchで内部エラー表示になっていた）
        if (interface_names_.find(impl.interface_name) == interface_names_.end()) {
            error(current_span_,
                  i18n::msgf(i18n::MsgId::TcNotDeclaredInterface, impl.interface_name));
            return;
        }
        if (impl_interfaces_[type_name].count(impl.interface_name) > 0) {
            error(current_span_, i18n::msgf(i18n::MsgId::TcDuplicateImplInterface, type_name,
                                            impl.interface_name));
            return;
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
        // 演算子実装のMethodInfoをメソッド表へ登録する（operator+等のキー。
        // 型検査の演算子オーバーロード判定がresolve_methodの統一機構で引けるようにする）
        {
            std::string op_symbol;
            switch (op->op) {
                case ast::OperatorKind::Eq:
                    op_symbol = "==";
                    break;
                case ast::OperatorKind::Ne:
                    op_symbol = "!=";
                    break;
                case ast::OperatorKind::Lt:
                    op_symbol = "<";
                    break;
                case ast::OperatorKind::Gt:
                    op_symbol = ">";
                    break;
                case ast::OperatorKind::Le:
                    op_symbol = "<=";
                    break;
                case ast::OperatorKind::Ge:
                    op_symbol = ">=";
                    break;
                case ast::OperatorKind::Add:
                    op_symbol = "+";
                    break;
                case ast::OperatorKind::Sub:
                    op_symbol = "-";
                    break;
                case ast::OperatorKind::Mul:
                    op_symbol = "*";
                    break;
                case ast::OperatorKind::Div:
                    op_symbol = "/";
                    break;
                case ast::OperatorKind::Mod:
                    op_symbol = "%";
                    break;
                case ast::OperatorKind::BitAnd:
                    op_symbol = "&";
                    break;
                case ast::OperatorKind::BitOr:
                    op_symbol = "|";
                    break;
                case ast::OperatorKind::BitXor:
                    op_symbol = "^";
                    break;
                case ast::OperatorKind::Shl:
                    op_symbol = "<<";
                    break;
                case ast::OperatorKind::Shr:
                    op_symbol = ">>";
                    break;
                default:
                    break;
            }
            if (!op_symbol.empty()) {
                MethodInfo op_info;
                op_info.name = "operator" + op_symbol;
                for (const auto& param : op->params) {
                    op_info.param_types.push_back(param.type);
                }
                op_info.return_type = op->return_type ? op->return_type : impl.target_type;
                type_methods_[type_name][op_info.name] = op_info;
            }
        }
    }

    for (const auto& method : impl.methods) {
        // Q4: 例外→通常診断（重複メソッドは当該メソッドの登録だけを飛ばし他は処理継続する）
        if (type_methods_[type_name].count(method->name) > 0) {
            error(current_span_,
                  i18n::msgf(i18n::MsgId::TcDuplicateMethodOnType, type_name, method->name));
            continue;
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
            // 境界インターフェイス付きimpl（impl<T: Shape>等）でも境界をコンテキストへ伝搬する
            std::vector<std::string> bounds;
            for (const auto& gp : impl.generic_params_v2) {
                if (gp.name == param && gp.is_type()) {
                    bounds = gp.type_constraint.interfaces;
                    break;
                }
            }
            // where句の境界（impl G<T> for Hash where T: Hash 等）も本文検査の解決に使う
            // （従来は受理のみで境界が未配線のため、T値へのメソッド呼び出しが解決できなかった）
            for (const auto& wc : impl.where_clauses) {
                if (wc.type_param == param) {
                    for (const auto& iface : wc.constraint.interfaces) {
                        bounds.push_back(iface);
                    }
                }
            }
            generic_context_.add_type_param(param, bounds);
        }
    }
    // impl G<T> for Iface 形式（プレフィックス<T>なし）: ターゲット型引数のうち既知の型でない
    // 裸名を型パラメータとして登録する（where句の境界も付与）。従来は未登録のため、
    // 本文のT値へのメソッド呼び出しが境界経由で解決できなかった
    if (impl.generic_params.empty() && impl.target_type && !impl.target_type->type_args.empty()) {
        for (const auto& targ : impl.target_type->type_args) {
            if (!targ || targ->name.empty() || !targ->type_args.empty()) {
                continue;
            }
            if (struct_defs_.count(targ->name) > 0 || enum_names_.count(targ->name) > 0 ||
                typedef_defs_.count(targ->name) > 0 || interface_names_.count(targ->name) > 0 ||
                targ->is_primitive()) {
                continue;
            }
            std::vector<std::string> bounds;
            for (const auto& wc : impl.where_clauses) {
                if (wc.type_param == targ->name) {
                    for (const auto& iface : wc.constraint.interfaces) {
                        bounds.push_back(iface);
                    }
                }
            }
            generic_context_.add_type_param(targ->name, bounds);
        }
    }

    std::string type_name = ast::type_to_string(*impl.target_type);

    if (!impl.interface_name.empty()) {
        // インターフェースの存在チェック（未宣言はregister_implが診断済みのため、ここでは重複報告せず本文検査だけ打ち切る）
        if (interface_names_.find(impl.interface_name) == interface_names_.end()) {
            return;
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

    // コンストラクタ/デストラクタ本体でもprivateメンバへアクセスできるよう、impl対象型はここで設定する（X2）
    current_impl_target_type_ = type_name;

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
        // R8: メソッドのデフォルト引数もパラメータ参照を診断する
        check_default_param_refs(method->params, method->name_span);
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

    // 演算子実装の本体も型検査する（typed-hir-single-source 第2段）。
    // 従来は未検査でexpr.typeが注釈されず、HIRの演算子本体が全ノードerror型になっていた
    for (auto& op : impl.operators) {
        scopes_.push();
        current_return_type_ = op->return_type;
        scopes_.current().define("self", impl.target_type, false);
        mark_variable_initialized("self");
        for (const auto& param : op->params) {
            scopes_.current().define(param.name, param.type, param.qualifiers.is_const);
            mark_variable_initialized(param.name);
        }
        for (auto& stmt : op->body) {
            check_statement(*stmt);
        }
        initialized_variables_.clear();
        scopes_.pop();
    }
    current_return_type_ = nullptr;
    current_impl_target_type_.clear();
    generic_context_.clear();
}

}  // namespace cm
