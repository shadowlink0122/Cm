#pragma once

#include "../common/debug.hpp"
#include "../hir/hir_nodes.hpp"
#include "mir_cpp_nodes.hpp"

#include <unordered_map>

namespace cm::mir_cpp {

class HirToMirCppConverter {
   private:
    std::unordered_map<std::string, hir::TypePtr> variable_types;
    int temp_counter = 0;

    std::string gen_temp_name() { return "_tmp" + std::to_string(temp_counter++); }

   public:
    Program convert(const hir::Program& hir_program) {
        Program program;

        // 必要なヘッダー
        program.imports.push_back("<iostream>");
        program.imports.push_back("<string>");
        program.imports.push_back("<cstdlib>");
        program.imports.push_back("<sstream>");
        program.imports.push_back("<iomanip>");
        program.imports.push_back("<bitset>");

        // 各関数を変換
        for (const auto& hir_func : hir_program.functions) {
            program.functions.push_back(convert_function(hir_func));
        }

        return program;
    }

   private:
    Function convert_function(const hir::Function& hir_func) {
        Function func;
        func.name = hir_func.name;
        func.return_type = hir_func.return_type;
        func.is_main = (hir_func.name == "main");

        // パラメータを変換
        for (const auto& param : hir_func.params) {
            func.params.push_back({param.name, param.type});
            variable_types[param.name] = param.type;
        }

        // ボディを変換
        func.body = convert_block(hir_func.body);

        return func;
    }

    BlockPtr convert_block(const hir::Block& hir_block) {
        auto block = make_block();

        for (const auto& hir_stmt : hir_block.statements) {
            auto stmt = convert_statement(hir_stmt);
            if (stmt) {
                block->statements.push_back(stmt);
            }
        }

        return block;
    }

    StmtPtr convert_statement(const hir::Statement& hir_stmt) {
        switch (hir_stmt.kind) {
            case hir::StatementKind::VarDecl: {
                auto& decl = std::get<hir::VarDeclStatement>(hir_stmt.data);
                variable_types[decl.name] = decl.type;

                ExprPtr init = nullptr;
                if (decl.init) {
                    init = convert_expression(*decl.init);
                }

                return make_var_decl(decl.name, decl.type, init);
            }

            case hir::StatementKind::Assignment: {
                auto& assign = std::get<hir::AssignmentStatement>(hir_stmt.data);
                auto value = convert_expression(*assign.value);
                return make_assignment(assign.target, value);
            }

            case hir::StatementKind::Expression: {
                auto& expr_stmt = std::get<hir::ExpressionStatement>(hir_stmt.data);
                auto expr = convert_expression(*expr_stmt.expr);

                auto stmt = std::make_shared<Statement>();
                stmt->kind = StmtKind::Expression;
                stmt->data = expr;
                return stmt;
            }

            case hir::StatementKind::If: {
                auto& if_stmt = std::get<hir::IfStatement>(hir_stmt.data);
                auto cond = convert_expression(*if_stmt.condition);
                auto then_block = convert_block(*if_stmt.then_block);

                BlockPtr else_block = nullptr;
                if (if_stmt.else_block) {
                    else_block = convert_block(*if_stmt.else_block);
                }

                return make_if(cond, then_block, else_block);
            }

            case hir::StatementKind::While: {
                auto& while_stmt = std::get<hir::WhileStatement>(hir_stmt.data);
                auto cond = convert_expression(*while_stmt.condition);
                auto body = convert_block(*while_stmt.body);

                auto stmt = std::make_shared<Statement>();
                stmt->kind = StmtKind::While;
                stmt->data = WhileStmt{cond, body};
                return stmt;
            }

            case hir::StatementKind::For: {
                auto& for_stmt = std::get<hir::ForStatement>(hir_stmt.data);

                // for文をwhile文に変換する簡単なアプローチ
                // TODO: ネイティブなfor文として保持
                auto block = make_block();

                // 初期化
                if (for_stmt.init) {
                    block->statements.push_back(convert_statement(*for_stmt.init));
                }

                // whileループ
                if (for_stmt.condition) {
                    auto cond = convert_expression(*for_stmt.condition);
                    auto loop_body = convert_block(*for_stmt.body);

                    // 更新式をループ本体の最後に追加
                    if (for_stmt.update) {
                        auto update = convert_expression(*for_stmt.update);
                        auto update_stmt = std::make_shared<Statement>();
                        update_stmt->kind = StmtKind::Expression;
                        update_stmt->data = update;
                        loop_body->statements.push_back(update_stmt);
                    }

                    auto while_stmt = std::make_shared<Statement>();
                    while_stmt->kind = StmtKind::While;
                    while_stmt->data = WhileStmt{cond, loop_body};
                    block->statements.push_back(while_stmt);
                }

                auto block_stmt = std::make_shared<Statement>();
                block_stmt->kind = StmtKind::Block;
                block_stmt->data = block;
                return block_stmt;
            }

            case hir::StatementKind::Return: {
                auto& ret_stmt = std::get<hir::ReturnStatement>(hir_stmt.data);
                ExprPtr value = nullptr;
                if (ret_stmt.value) {
                    value = convert_expression(*ret_stmt.value);
                }
                return make_return(value);
            }

            case hir::StatementKind::Break: {
                auto stmt = std::make_shared<Statement>();
                stmt->kind = StmtKind::Break;
                return stmt;
            }

            case hir::StatementKind::Continue: {
                auto stmt = std::make_shared<Statement>();
                stmt->kind = StmtKind::Continue;
                return stmt;
            }

            default:
                return nullptr;
        }
    }

    ExprPtr convert_expression(const hir::Expression& hir_expr) {
        switch (hir_expr.kind) {
            case hir::ExpressionKind::Literal: {
                auto& lit = std::get<hir::LiteralExpression>(hir_expr.data);
                Literal mir_lit;
                mir_lit.value = lit.value;
                mir_lit.type = hir_expr.type;
                return make_literal(mir_lit);
            }

            case hir::ExpressionKind::Variable: {
                auto& var = std::get<hir::VariableExpression>(hir_expr.data);
                return make_var(var.name, hir_expr.type);
            }

            case hir::ExpressionKind::Binary: {
                auto& bin = std::get<hir::BinaryExpression>(hir_expr.data);
                auto left = convert_expression(*bin.left);
                auto right = convert_expression(*bin.right);

                BinaryOp::Op op;
                switch (bin.op) {
                    case hir::BinaryOp::Add:
                        op = BinaryOp::Add;
                        break;
                    case hir::BinaryOp::Sub:
                        op = BinaryOp::Sub;
                        break;
                    case hir::BinaryOp::Mul:
                        op = BinaryOp::Mul;
                        break;
                    case hir::BinaryOp::Div:
                        op = BinaryOp::Div;
                        break;
                    case hir::BinaryOp::Mod:
                        op = BinaryOp::Mod;
                        break;
                    case hir::BinaryOp::Eq:
                        op = BinaryOp::Eq;
                        break;
                    case hir::BinaryOp::Ne:
                        op = BinaryOp::Ne;
                        break;
                    case hir::BinaryOp::Lt:
                        op = BinaryOp::Lt;
                        break;
                    case hir::BinaryOp::Le:
                        op = BinaryOp::Le;
                        break;
                    case hir::BinaryOp::Gt:
                        op = BinaryOp::Gt;
                        break;
                    case hir::BinaryOp::Ge:
                        op = BinaryOp::Ge;
                        break;
                    case hir::BinaryOp::And:
                        op = BinaryOp::And;
                        break;
                    case hir::BinaryOp::Or:
                        op = BinaryOp::Or;
                        break;
                    case hir::BinaryOp::BitAnd:
                        op = BinaryOp::BitAnd;
                        break;
                    case hir::BinaryOp::BitOr:
                        op = BinaryOp::BitOr;
                        break;
                    case hir::BinaryOp::BitXor:
                        op = BinaryOp::BitXor;
                        break;
                    case hir::BinaryOp::Shl:
                        op = BinaryOp::Shl;
                        break;
                    case hir::BinaryOp::Shr:
                        op = BinaryOp::Shr;
                        break;
                }

                return make_binary(op, left, right);
            }

            case hir::ExpressionKind::Unary: {
                auto& un = std::get<hir::UnaryExpression>(hir_expr.data);
                auto operand = convert_expression(*un.operand);

                UnaryOp::Op op;
                switch (un.op) {
                    case hir::UnaryOp::Neg:
                        op = UnaryOp::Neg;
                        break;
                    case hir::UnaryOp::Not:
                        op = UnaryOp::Not;
                        break;
                    case hir::UnaryOp::BitNot:
                        op = UnaryOp::BitNot;
                        break;
                }

                auto expr = std::make_shared<Expression>();
                expr->kind = ExprKind::Unary;
                expr->data = UnaryOp{op, operand};
                expr->type = hir_expr.type;
                return expr;
            }

            case hir::ExpressionKind::Call: {
                auto& call = std::get<hir::CallExpression>(hir_expr.data);

                CallExpr mir_call;
                mir_call.func_name = call.func_name;
                mir_call.return_type = hir_expr.type;

                for (const auto& arg : call.args) {
                    mir_call.args.push_back(convert_expression(*arg));
                }

                auto expr = std::make_shared<Expression>();
                expr->kind = ExprKind::Call;
                expr->data = mir_call;
                expr->type = hir_expr.type;
                return expr;
            }

            case hir::ExpressionKind::StringInterpolation: {
                auto& interp = std::get<hir::StringInterpolation>(hir_expr.data);

                StringInterpolation mir_interp;
                for (const auto& part : interp.parts) {
                    StringInterpolation::Part mir_part;
                    mir_part.text = part.text;
                    if (part.expr) {
                        mir_part.expr = convert_expression(*part.expr);
                    }
                    mir_part.format_spec = part.format_spec;
                    mir_interp.parts.push_back(mir_part);
                }

                auto expr = std::make_shared<Expression>();
                expr->kind = ExprKind::StringInterpolation;
                expr->data = mir_interp;
                expr->type = hir_expr.type;
                return expr;
            }

            default: {
                // デフォルトケース
                Literal lit;
                lit.value = int64_t(0);
                lit.type = hir::make_int();
                return make_literal(lit);
            }
        }
    }
};

}  // namespace cm::mir_cpp