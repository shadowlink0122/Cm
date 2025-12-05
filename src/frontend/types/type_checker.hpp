#pragma once

#include "../../common/debug/tc.hpp"
#include "../ast/decl.hpp"
#include "../parser/parser.hpp"
#include "scope.hpp"

#include <functional>

namespace cm {

// ============================================================
// 型チェッカー
// ============================================================
class TypeChecker {
   public:
    TypeChecker() = default;

    // プログラム全体をチェック
    bool check(ast::Program& program) {
        debug::tc::log(debug::tc::Id::Start);

        // Pass 1: 関数シグネチャを登録
        for (auto& decl : program.declarations) {
            register_declaration(*decl);
        }

        // Pass 2: 関数本体をチェック
        for (auto& decl : program.declarations) {
            check_declaration(*decl);
        }

        debug::tc::log(debug::tc::Id::End, std::to_string(diagnostics_.size()) + " issues");
        return !has_errors();
    }

    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }
    bool has_errors() const {
        for (const auto& d : diagnostics_) {
            if (d.kind == DiagKind::Error)
                return true;
        }
        return false;
    }

   private:
    // 宣言の登録（パス1）
    void register_declaration(ast::Decl& decl) {
        if (auto* func = decl.as<ast::FunctionDecl>()) {
            std::vector<ast::TypePtr> param_types;
            for (const auto& p : func->params) {
                param_types.push_back(p.type);
            }
            scopes_.global().define_function(func->name, std::move(param_types), func->return_type);
        } else if (auto* st = decl.as<ast::StructDecl>()) {
            // 構造体を型として登録
            scopes_.global().define(st->name, ast::make_named(st->name));
        }
    }

    // 宣言のチェック（パス2）
    void check_declaration(ast::Decl& decl) {
        debug::tc::log(debug::tc::Id::CheckDecl, "", debug::Level::Trace);

        if (auto* func = decl.as<ast::FunctionDecl>()) {
            check_function(*func);
        }
    }

    // 関数チェック
    void check_function(ast::FunctionDecl& func) {
        scopes_.push();
        current_return_type_ = func.return_type;

        // パラメータをスコープに追加
        for (const auto& param : func.params) {
            scopes_.current().define(param.name, param.type, param.qualifiers.is_const);
        }

        // 本体をチェック
        for (auto& stmt : func.body) {
            check_statement(*stmt);
        }

        scopes_.pop();
        current_return_type_ = nullptr;
    }

    // 文のチェック
    void check_statement(ast::Stmt& stmt) {
        debug::tc::log(debug::tc::Id::CheckStmt, "", debug::Level::Trace);

        if (auto* let = stmt.as<ast::LetStmt>()) {
            check_let(*let);
        } else if (auto* ret = stmt.as<ast::ReturnStmt>()) {
            check_return(*ret);
        } else if (auto* expr_stmt = stmt.as<ast::ExprStmt>()) {
            if (expr_stmt->expr) {
                infer_type(*expr_stmt->expr);
            }
        } else if (auto* if_stmt = stmt.as<ast::IfStmt>()) {
            check_if(*if_stmt);
        } else if (auto* while_stmt = stmt.as<ast::WhileStmt>()) {
            check_while(*while_stmt);
        } else if (auto* for_stmt = stmt.as<ast::ForStmt>()) {
            check_for(*for_stmt);
        } else if (auto* block = stmt.as<ast::BlockStmt>()) {
            scopes_.push();
            for (auto& s : block->stmts) {
                check_statement(*s);
            }
            scopes_.pop();
        }
    }

    // let文
    void check_let(ast::LetStmt& let) {
        ast::TypePtr init_type;
        if (let.init) {
            init_type = infer_type(*let.init);
        }

        if (let.type) {
            // 明示的型あり
            if (init_type && !types_compatible(let.type, init_type)) {
                error(Span{}, "Type mismatch in variable declaration '" + let.name + "'");
            }
            scopes_.current().define(let.name, let.type, let.is_const);
        } else if (init_type) {
            // 型推論
            let.type = init_type;
            scopes_.current().define(let.name, init_type, let.is_const);
            debug::tc::log(debug::tc::Id::TypeInfer,
                           let.name + " : " + ast::type_to_string(*init_type), debug::Level::Trace);
        } else {
            error(Span{}, "Cannot infer type for '" + let.name + "'");
        }
    }

    // return文
    void check_return(ast::ReturnStmt& ret) {
        if (!current_return_type_)
            return;

        if (ret.value) {
            auto val_type = infer_type(*ret.value);
            if (!types_compatible(current_return_type_, val_type)) {
                error(Span{}, "Return type mismatch");
            }
        } else if (current_return_type_->kind != ast::TypeKind::Void) {
            error(Span{}, "Missing return value");
        }
    }

    // if文
    void check_if(ast::IfStmt& if_stmt) {
        auto cond_type = infer_type(*if_stmt.condition);
        if (cond_type && cond_type->kind != ast::TypeKind::Bool) {
            error(Span{}, "Condition must be bool");
        }

        scopes_.push();
        for (auto& s : if_stmt.then_block) {
            check_statement(*s);
        }
        scopes_.pop();

        if (!if_stmt.else_block.empty()) {
            scopes_.push();
            for (auto& s : if_stmt.else_block) {
                check_statement(*s);
            }
            scopes_.pop();
        }
    }

    // while文
    void check_while(ast::WhileStmt& while_stmt) {
        auto cond_type = infer_type(*while_stmt.condition);
        if (cond_type && cond_type->kind != ast::TypeKind::Bool) {
            error(Span{}, "Condition must be bool");
        }

        scopes_.push();
        for (auto& s : while_stmt.body) {
            check_statement(*s);
        }
        scopes_.pop();
    }

    // for文
    void check_for(ast::ForStmt& for_stmt) {
        scopes_.push();

        if (for_stmt.init) {
            check_statement(*for_stmt.init);
        }
        if (for_stmt.condition) {
            auto cond_type = infer_type(*for_stmt.condition);
            if (cond_type && cond_type->kind != ast::TypeKind::Bool) {
                error(Span{}, "For condition must be bool");
            }
        }
        if (for_stmt.update) {
            infer_type(*for_stmt.update);
        }

        for (auto& s : for_stmt.body) {
            check_statement(*s);
        }

        scopes_.pop();
    }

    // 式の型推論
    ast::TypePtr infer_type(ast::Expr& expr) {
        debug::tc::log(debug::tc::Id::CheckExpr, "", debug::Level::Trace);

        if (auto* lit = expr.as<ast::LiteralExpr>()) {
            return infer_literal(*lit);
        } else if (auto* ident = expr.as<ast::IdentExpr>()) {
            return infer_ident(*ident);
        } else if (auto* binary = expr.as<ast::BinaryExpr>()) {
            return infer_binary(*binary);
        } else if (auto* unary = expr.as<ast::UnaryExpr>()) {
            return infer_unary(*unary);
        } else if (auto* call = expr.as<ast::CallExpr>()) {
            return infer_call(*call);
        } else if (auto* member = expr.as<ast::MemberExpr>()) {
            return infer_member(*member);
        }

        return ast::make_error();
    }

    // リテラル
    ast::TypePtr infer_literal(ast::LiteralExpr& lit) {
        if (lit.is_null())
            return ast::make_void();
        if (lit.is_bool())
            return std::make_shared<ast::Type>(ast::TypeKind::Bool);
        if (lit.is_int())
            return ast::make_int();
        if (lit.is_float())
            return ast::make_double();
        if (lit.is_char())
            return ast::make_char();
        if (lit.is_string())
            return ast::make_string();
        return ast::make_error();
    }

    // 識別子
    ast::TypePtr infer_ident(ast::IdentExpr& ident) {
        auto sym = scopes_.current().lookup(ident.name);
        if (!sym) {
            error(Span{}, "Undefined variable '" + ident.name + "'");
            return ast::make_error();
        }
        debug::tc::log(debug::tc::Id::Resolved,
                       ident.name + " : " + ast::type_to_string(*sym->type), debug::Level::Trace);
        return sym->type;
    }

    // 二項演算
    ast::TypePtr infer_binary(ast::BinaryExpr& binary) {
        auto ltype = infer_type(*binary.left);
        auto rtype = infer_type(*binary.right);

        if (!ltype || !rtype)
            return ast::make_error();

        switch (binary.op) {
            // 比較演算子 → bool
            case ast::BinaryOp::Eq:
            case ast::BinaryOp::Ne:
            case ast::BinaryOp::Lt:
            case ast::BinaryOp::Gt:
            case ast::BinaryOp::Le:
            case ast::BinaryOp::Ge:
                return std::make_shared<ast::Type>(ast::TypeKind::Bool);

            // 論理演算子 → bool
            case ast::BinaryOp::And:
            case ast::BinaryOp::Or:
                if (ltype->kind != ast::TypeKind::Bool || rtype->kind != ast::TypeKind::Bool) {
                    error(Span{}, "Logical operators require bool operands");
                }
                return std::make_shared<ast::Type>(ast::TypeKind::Bool);

            // 代入 → 左辺の型
            case ast::BinaryOp::Assign:
            case ast::BinaryOp::AddAssign:
            case ast::BinaryOp::SubAssign:
            case ast::BinaryOp::MulAssign:
            case ast::BinaryOp::DivAssign:
                if (!types_compatible(ltype, rtype)) {
                    error(Span{}, "Assignment type mismatch");
                }
                return ltype;

            // 算術演算子 → 共通型
            default:
                if (!ltype->is_numeric() || !rtype->is_numeric()) {
                    error(Span{}, "Arithmetic operators require numeric operands");
                    return ast::make_error();
                }
                return common_type(ltype, rtype);
        }
    }

    // 単項演算
    ast::TypePtr infer_unary(ast::UnaryExpr& unary) {
        auto otype = infer_type(*unary.operand);
        if (!otype)
            return ast::make_error();

        switch (unary.op) {
            case ast::UnaryOp::Neg:
                if (!otype->is_numeric()) {
                    error(Span{}, "Negation requires numeric operand");
                }
                return otype;
            case ast::UnaryOp::Not:
                if (otype->kind != ast::TypeKind::Bool) {
                    error(Span{}, "Logical not requires bool operand");
                }
                return std::make_shared<ast::Type>(ast::TypeKind::Bool);
            case ast::UnaryOp::BitNot:
                if (!otype->is_integer()) {
                    error(Span{}, "Bitwise not requires integer operand");
                }
                return otype;
            case ast::UnaryOp::Deref:
                if (otype->kind != ast::TypeKind::Pointer) {
                    error(Span{}, "Cannot dereference non-pointer");
                    return ast::make_error();
                }
                return otype->element_type;
            case ast::UnaryOp::AddrOf:
                return ast::make_pointer(otype);
            case ast::UnaryOp::PreInc:
            case ast::UnaryOp::PreDec:
            case ast::UnaryOp::PostInc:
            case ast::UnaryOp::PostDec:
                if (!otype->is_numeric()) {
                    error(Span{}, "Increment/decrement requires numeric operand");
                }
                return otype;
        }
        return ast::make_error();
    }

    // 関数呼び出し
    ast::TypePtr infer_call(ast::CallExpr& call) {
        // 呼び出し先の型を取得
        if (auto* ident = call.callee->as<ast::IdentExpr>()) {
            auto sym = scopes_.current().lookup(ident->name);
            if (!sym || !sym->is_function) {
                error(Span{}, "'" + ident->name + "' is not a function");
                return ast::make_error();
            }

            // 引数チェック
            if (call.args.size() != sym->param_types.size()) {
                error(Span{}, "Function '" + ident->name + "' expects " +
                                  std::to_string(sym->param_types.size()) + " arguments, got " +
                                  std::to_string(call.args.size()));
            } else {
                for (size_t i = 0; i < call.args.size(); ++i) {
                    auto arg_type = infer_type(*call.args[i]);
                    if (!types_compatible(sym->param_types[i], arg_type)) {
                        error(Span{}, "Argument type mismatch in call to '" + ident->name + "'");
                    }
                }
            }

            return sym->return_type;
        }

        return ast::make_error();
    }

    // メンバアクセス
    ast::TypePtr infer_member(ast::MemberExpr& member) {
        auto obj_type = infer_type(*member.object);
        // TODO: 構造体フィールド解決
        return ast::make_error();
    }

    // 型互換性チェック
    bool types_compatible(ast::TypePtr a, ast::TypePtr b) {
        if (!a || !b)
            return false;
        if (a->kind == ast::TypeKind::Error || b->kind == ast::TypeKind::Error)
            return true;

        // 同じ型
        if (a->kind == b->kind) {
            if (a->kind == ast::TypeKind::Struct) {
                return a->name == b->name;
            }
            return true;
        }

        // 数値型間の暗黙変換
        if (a->is_numeric() && b->is_numeric()) {
            return true;
        }

        return false;
    }

    // 共通型の計算
    ast::TypePtr common_type(ast::TypePtr a, ast::TypePtr b) {
        if (a->kind == b->kind)
            return a;

        // float > int
        if (a->is_floating() || b->is_floating()) {
            return a->kind == ast::TypeKind::Double || b->kind == ast::TypeKind::Double
                       ? ast::make_double()
                       : ast::make_float();
        }

        // より大きい整数型
        auto a_info = a->info();
        auto b_info = b->info();
        return a_info.size >= b_info.size ? a : b;
    }

    void error(Span span, const std::string& msg) {
        debug::tc::log(debug::tc::Id::TypeError, msg, debug::Level::Error);
        diagnostics_.emplace_back(DiagKind::Error, span, msg);
    }

    ScopeStack scopes_;
    ast::TypePtr current_return_type_;
    std::vector<Diagnostic> diagnostics_;
};

}  // namespace cm
