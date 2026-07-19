// ============================================================
// TypeChecker 実装 - ラムダ式の型推論とキャプチャ解析
// ============================================================

#include "internal/base/i18n.hpp"
#include "internal/base/text_utils.hpp"
#include "internal/types/type_checker.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cm {

ast::TypePtr TypeChecker::infer_lambda(ast::LambdaExpr& lambda) {
    // ラムダ式の型チェック
    // パラメータの型が明示されていない場合はエラー
    std::vector<ast::TypePtr> param_types;
    std::unordered_set<std::string> param_names;

    for (const auto& param : lambda.params) {
        if (!param.type || param.type->kind == ast::TypeKind::Error) {
            error(current_span_, "Lambda parameter '" + param.name +
                                     "' must have an explicit type. "
                                     "Use: (Type param_name) => { ... }");
            return ast::make_error();
        }
        param_types.push_back(param.type);
        param_names.insert(param.name);
    }

    // 新しいスコープを作成してパラメータを登録
    scopes_.push();
    for (const auto& param : lambda.params) {
        scopes_.current().define(param.name, param.type);
        // ラムダパラメータは呼び出し時に必ず値が渡されるため初期化済み
        mark_variable_initialized(param.name);
    }

    // ラムダ本体の型チェック
    // 現在の戻り値型を保存し、一時的にnullに
    auto saved_return_type = current_return_type_;
    current_return_type_ = nullptr;

    ast::TypePtr return_type = ast::make_void();

    // キャプチャ検出用：ラムダ内で使用される識別子を収集
    std::unordered_set<std::string> used_identifiers;
    std::unordered_set<std::string> local_vars;  // ラムダ内で定義された変数

    // 式からすべての識別子を収集するヘルパーラムダ
    std::function<void(ast::Expr&)> collect_identifiers = [&](ast::Expr& expr) {
        if (auto* ident = expr.as<ast::IdentExpr>()) {
            used_identifiers.insert(ident->name);
        } else if (auto* binary = expr.as<ast::BinaryExpr>()) {
            collect_identifiers(*binary->left);
            collect_identifiers(*binary->right);
        } else if (auto* unary = expr.as<ast::UnaryExpr>()) {
            collect_identifiers(*unary->operand);
        } else if (auto* call = expr.as<ast::CallExpr>()) {
            collect_identifiers(*call->callee);
            for (auto& arg : call->args) {
                collect_identifiers(*arg);
            }
        } else if (auto* member = expr.as<ast::MemberExpr>()) {
            collect_identifiers(*member->object);
        } else if (auto* index = expr.as<ast::IndexExpr>()) {
            collect_identifiers(*index->object);
            collect_identifiers(*index->index);
        } else if (auto* ternary = expr.as<ast::TernaryExpr>()) {
            collect_identifiers(*ternary->condition);
            collect_identifiers(*ternary->then_expr);
            collect_identifiers(*ternary->else_expr);
        }
        // その他の式タイプも必要に応じて追加
    };

    // 文から識別子を収集するヘルパーラムダ
    std::function<void(ast::Stmt&)> collect_from_stmt = [&](ast::Stmt& stmt) {
        if (auto* let = stmt.as<ast::LetStmt>()) {
            local_vars.insert(let->name);  // ローカル変数として記録
            if (let->init) {
                collect_identifiers(*let->init);
            }
        } else if (auto* ret = stmt.as<ast::ReturnStmt>()) {
            if (ret->value) {
                collect_identifiers(*ret->value);
            }
        } else if (auto* expr_stmt = stmt.as<ast::ExprStmt>()) {
            collect_identifiers(*expr_stmt->expr);
        } else if (auto* if_stmt = stmt.as<ast::IfStmt>()) {
            collect_identifiers(*if_stmt->condition);
            for (auto& s : if_stmt->then_block) {
                collect_from_stmt(*s);
            }
            for (auto& s : if_stmt->else_block) {
                collect_from_stmt(*s);
            }
        } else if (auto* while_stmt = stmt.as<ast::WhileStmt>()) {
            collect_identifiers(*while_stmt->condition);
            for (auto& s : while_stmt->body) {
                collect_from_stmt(*s);
            }
        } else if (auto* for_stmt = stmt.as<ast::ForStmt>()) {
            if (for_stmt->init)
                collect_from_stmt(*for_stmt->init);
            if (for_stmt->condition)
                collect_identifiers(*for_stmt->condition);
            if (for_stmt->update)
                collect_identifiers(*for_stmt->update);
            for (auto& s : for_stmt->body) {
                collect_from_stmt(*s);
            }
        }
        // その他の文タイプも必要に応じて追加
    };

    if (lambda.is_expr_body()) {
        // 式ボディ: (int x) => x * 2
        auto& expr = std::get<ast::ExprPtr>(lambda.body);
        collect_identifiers(*expr);
        return_type = infer_type(*expr);
    } else {
        // 文ボディ: (int x) => { return x * 2; }
        auto& stmts = std::get<std::vector<ast::StmtPtr>>(lambda.body);

        // 識別子を収集
        for (auto& stmt : stmts) {
            collect_from_stmt(*stmt);
        }

        // まず戻り値の型を先に推論
        for (auto& stmt : stmts) {
            if (auto* ret = stmt->as<ast::ReturnStmt>()) {
                if (ret->value) {
                    return_type = infer_type(*ret->value);
                    break;
                }
            }
        }

        // 戻り値型を設定してから文をチェック
        current_return_type_ = return_type;
        for (auto& stmt : stmts) {
            check_statement(*stmt);
        }
    }

    // 戻り値型を復元
    current_return_type_ = saved_return_type;

    scopes_.pop();

    // キャプチャされる変数を特定
    // used_identifiers から param_names と local_vars を除いたものが外部変数
    lambda.captures.clear();
    for (const auto& name : used_identifiers) {
        // パラメータまたはラムダ内ローカル変数は除外
        if (param_names.count(name) > 0 || local_vars.count(name) > 0) {
            continue;
        }

        // 外側のスコープで定義されているか確認
        auto sym = scopes_.current().lookup(name);
        if (sym && sym->type) {
            ast::LambdaExpr::Capture cap;
            cap.name = name;
            cap.type = sym->type;
            cap.by_ref = false;  // デフォルトは値キャプチャ
            lambda.captures.push_back(cap);

            debug::tc::log(debug::tc::Id::Resolved, "Lambda captures: " + name,
                           debug::Level::Debug);
        }
        // 見つからない場合は、グローバル変数や関数かもしれないので無視
    }

    // 関数ポインタ型を構築: ReturnType*(ParamTypes...)
    auto func_type = std::make_shared<ast::Type>(ast::TypeKind::Function);
    func_type->return_type = return_type;
    func_type->param_types = std::move(param_types);

    return func_type;
}

}  // namespace cm
