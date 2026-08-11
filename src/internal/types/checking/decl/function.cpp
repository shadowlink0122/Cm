// ============================================================
// TypeChecker 実装 - 関数本体の検査（check_function）と組み込みprintln/printの登録
// ============================================================

#include "internal/base/i18n.hpp"
#include "internal/types/type_checker.hpp"

#include <string>
#include <vector>

namespace cm {

// 文列の終端判定（実装はstmt.cpp。H6のreturn網羅解析で使用）
bool cm_stmts_terminate(const std::vector<ast::StmtPtr>& stmts, bool for_function);

void TypeChecker::register_println() {
    // printlnは可変引数で、0個以上の引数を取る
    scopes_.global().define_function("println", {}, ast::make_void(), 0);
}

void TypeChecker::register_print() {
    // printは1個の引数を取る
    scopes_.global().define_function("print", {ast::make_void()}, ast::make_void(), 1);
}

void TypeChecker::check_function(ast::FunctionDecl& func) {
    // キャプチャ付きクロージャ変数の追跡は関数単位（V5〜V7の診断用）
    closure_vars_.clear();

    // #[test]テストベンチはDUTの外部として入力ポートを駆動できる（R16の入力ポート代入診断の除外）
    in_test_function_ = false;
    for (const auto& attr : func.attributes) {
        if (attr.name == "test") {
            in_test_function_ = true;
            break;
        }
    }

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
            // 境界インターフェイス（<T: Shape>等）はgeneric_params_v2側に保持されるため、名前で引いてコンテキストへ伝搬する
            std::vector<std::string> bounds;
            for (const auto& gp : func.generic_params_v2) {
                if (gp.name == param && gp.is_type()) {
                    bounds = gp.type_constraint.interfaces;
                    break;
                }
            }
            generic_context_.add_type_param(param, bounds);
            scopes_.current().define(param, ast::make_named(param));
            debug::tc::log(debug::tc::Id::Resolved, "Added generic type param: " + param,
                           debug::Level::Trace);
        }
    }

    current_return_type_ = resolve_typedef(func.return_type);
    if (!is_valid_type(func.return_type)) {
        error(func.name_span, i18n::msgf(i18n::MsgId::TcUndefinedReturnTypeFunction,
                                         ast::type_to_string(*func.return_type), func.name));
    }
    if (generic_context_.has_type_param(ast::type_to_string(*func.return_type))) {
        current_return_type_ = func.return_type;
    }

    // R8: デフォルト引数式のパラメータ参照を診断する（パラメータをスコープへ定義する前に検査する）
    check_default_param_refs(func.params, func.name_span);

    for (auto& param : func.params) {
        if (!is_valid_type(param.type)) {
            error(func.name_span,
                  i18n::msgf(i18n::MsgId::TcUndefinedParameterTypeParameterFunction,
                             ast::type_to_string(*param.type), param.name, func.name));
        }
        auto resolved_type = resolve_typedef(param.type);
        if (generic_context_.has_type_param(ast::type_to_string(*param.type))) {
            resolved_type = param.type;
        }
        scopes_.current().define(param.name, resolved_type, param.qualifiers.is_const);
        // パラメータは初期化されているとみなす
        mark_variable_initialized(param.name);
        // デフォルト引数式へ型を注釈する（typed-hir-single-source 第2段。期待型=パラメータ型）
        if (param.default_value) {
            infer_type_expecting(*param.default_value, resolved_type);
        }
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

        // H6: return網羅解析。非void関数の一部経路が値を返さずに末尾へ到達する場合を警告する
        // （従来は無診断で、下流バックエンドが未定義値を返していた。段階導入のためまず警告）
        bool is_non_void = func.return_type && func.return_type->kind != ast::TypeKind::Void &&
                           func.return_type->kind != ast::TypeKind::Inferred;
        bool exempt = func.is_extern || func.is_always || func.is_async || func.body.empty() ||
                      func.name == "main";
        if (is_non_void && !exempt && !cm_stmts_terminate(func.body, true)) {
            // H6段階3: --strict（check/lint --strict）ではエラーへ昇格する。通常のcheck/lintは警告のまま
            if (enable_naming_check_) {
                error(func.name_span, i18n::msgf(i18n::MsgId::TypeNotAllPathsReturn, func.name));
            } else {
                warning(func.name_span, i18n::msgf(i18n::MsgId::TypeNotAllPathsReturn, func.name));
            }
        }
    }

    // 初期化追跡をクリア（次の関数用）
    initialized_variables_.clear();

    scopes_.pop();
    current_return_type_ = nullptr;
}

}  // namespace cm
