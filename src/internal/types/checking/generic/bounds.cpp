// ============================================================
// ジェネリック本体が要求する演算子能力と宣言境界の突き合わせ（L8）
// ============================================================
// ジェネリック関数の本体を宣言時に一度走査し、型パラメータ型の値に比較演算子
// （< <= > >= == !=）を使っているのに境界（<T: Ord>等）が全く宣言されていない場合を
// エラー化する。検査は宣言時に前倒しされるため全インスタンス化へ一律に効く。
// 境界が1つでも宣言されている場合は、そのインターフェースが演算子を提供しうるため
// 誤検出回避のため対象外とする（保守的検査）。

#include "internal/base/i18n.hpp"
#include "internal/types/type_checker.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cm {

namespace {

// 比較演算子に必要な境界名を返す（対象外の演算子はnullptr）
const char* required_bound_for(ast::BinaryOp op) {
    switch (op) {
        case ast::BinaryOp::Lt:
        case ast::BinaryOp::Gt:
        case ast::BinaryOp::Le:
        case ast::BinaryOp::Ge:
            return "Ord";
        case ast::BinaryOp::Eq:
        case ast::BinaryOp::Ne:
            return "Eq";
        default:
            return nullptr;
    }
}

}  // namespace

void TypeChecker::check_generic_operator_bounds(ast::FunctionDecl& func) {
    if (func.generic_params.empty() && func.generic_params_v2.empty())
        return;

    // 型パラメータ名 → 宣言済み境界の有無
    std::unordered_map<std::string, bool> has_bound;
    for (const auto& name : func.generic_params)
        has_bound.emplace(name, false);
    for (const auto& gp : func.generic_params_v2) {
        bool bounded = !gp.constraints.empty() || !gp.type_constraint.interfaces.empty();
        auto [it, inserted] = has_bound.emplace(gp.name, bounded);
        if (!inserted)
            it->second = it->second || bounded;
    }

    // 型パラメータ型を持つ変数名 → 型パラメータ名（引数とlet宣言から収集）
    std::unordered_map<std::string, std::string> var_to_param;
    for (const auto& p : func.params) {
        if (p.type && has_bound.count(p.type->name) > 0)
            var_to_param[p.name] = p.type->name;
    }

    // 同一パラメータ・演算子の重複診断を抑止する
    std::unordered_set<std::string> reported;

    std::function<void(ast::Expr*)> walk_expr;
    std::function<void(ast::Stmt*)> walk_stmt;

    // 式が型パラメータ型の変数参照なら型パラメータ名を返す
    auto param_of = [&](ast::Expr* e) -> const std::string* {
        if (!e)
            return nullptr;
        if (auto* id = e->as<ast::IdentExpr>()) {
            auto it = var_to_param.find(id->name);
            if (it != var_to_param.end())
                return &it->second;
        }
        return nullptr;
    };

    walk_expr = [&](ast::Expr* e) {
        if (!e)
            return;
        if (auto* bin = e->as<ast::BinaryExpr>()) {
            if (const char* bound = required_bound_for(bin->op)) {
                const std::string* param = param_of(bin->left.get());
                if (!param)
                    param = param_of(bin->right.get());
                if (param) {
                    auto hb = has_bound.find(*param);
                    if (hb != has_bound.end() && !hb->second) {
                        std::string key = *param + "/" + bound;
                        if (reported.insert(key).second) {
                            // 段階導入: 既存の正当なコード（インスタンス化側の制約検査で守られて
                            // いる）を壊さないため、まず警告として導入する（破壊的変更回避）
                            warning(e->span,
                                    i18n::msgf(i18n::MsgId::TypeGenericBoundMissing, *param,
                                               ast::binary_op_str(bin->op), bound));
                        }
                    }
                }
            }
            walk_expr(bin->left.get());
            walk_expr(bin->right.get());
        } else if (auto* un = e->as<ast::UnaryExpr>()) {
            walk_expr(un->operand.get());
        } else if (auto* call = e->as<ast::CallExpr>()) {
            walk_expr(call->callee.get());
            for (auto& a : call->args)
                walk_expr(a.get());
        } else if (auto* idx = e->as<ast::IndexExpr>()) {
            walk_expr(idx->object.get());
            walk_expr(idx->index.get());
        } else if (auto* mem = e->as<ast::MemberExpr>()) {
            walk_expr(mem->object.get());
            for (auto& a : mem->args)
                walk_expr(a.get());
        } else if (auto* ter = e->as<ast::TernaryExpr>()) {
            walk_expr(ter->condition.get());
            walk_expr(ter->then_expr.get());
            walk_expr(ter->else_expr.get());
        } else if (auto* cast = e->as<ast::CastExpr>()) {
            walk_expr(cast->operand.get());
        } else if (auto* match = e->as<ast::MatchExpr>()) {
            walk_expr(match->scrutinee.get());
        }
        // その他の式は診断対象の比較演算子を含まないため走査しない
    };

    walk_stmt = [&](ast::Stmt* s) {
        if (!s)
            return;
        if (auto* let = s->as<ast::LetStmt>()) {
            if (let->type && has_bound.count(let->type->name) > 0)
                var_to_param[let->name] = let->type->name;
            walk_expr(let->init.get());
        } else if (auto* es = s->as<ast::ExprStmt>()) {
            walk_expr(es->expr.get());
        } else if (auto* ret = s->as<ast::ReturnStmt>()) {
            walk_expr(ret->value.get());
        } else if (auto* ifs = s->as<ast::IfStmt>()) {
            walk_expr(ifs->condition.get());
            for (auto& st : ifs->then_block)
                walk_stmt(st.get());
            for (auto& st : ifs->else_block)
                walk_stmt(st.get());
        } else if (auto* fs = s->as<ast::ForStmt>()) {
            walk_stmt(fs->init.get());
            walk_expr(fs->condition.get());
            walk_expr(fs->update.get());
            for (auto& st : fs->body)
                walk_stmt(st.get());
        } else if (auto* fin = s->as<ast::ForInStmt>()) {
            walk_expr(fin->iterable.get());
            for (auto& st : fin->body)
                walk_stmt(st.get());
        } else if (auto* ws = s->as<ast::WhileStmt>()) {
            walk_expr(ws->condition.get());
            for (auto& st : ws->body)
                walk_stmt(st.get());
        } else if (auto* blk = s->as<ast::BlockStmt>()) {
            for (auto& st : blk->stmts)
                walk_stmt(st.get());
        } else if (auto* sw = s->as<ast::SwitchStmt>()) {
            walk_expr(sw->expr.get());
            for (auto& c : sw->cases) {
                for (auto& st : c.stmts)
                    walk_stmt(st.get());
            }
        } else if (auto* df = s->as<ast::DeferStmt>()) {
            walk_stmt(df->body.get());
        } else if (auto* mb = s->as<ast::MustBlockStmt>()) {
            for (const auto& st : mb->body)
                walk_stmt(st.get());
        }
    };

    for (auto& stmt : func.body)
        walk_stmt(stmt.get());
}

}  // namespace cm
