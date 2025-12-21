// ============================================================
// TypeChecker 実装 - 文のチェック
// ============================================================

#include "../type_checker.hpp"

namespace cm {

void TypeChecker::check_statement(ast::Stmt& stmt) {
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
    } else if (auto* for_in = stmt.as<ast::ForInStmt>()) {
        check_for_in(*for_in);
    } else if (auto* block = stmt.as<ast::BlockStmt>()) {
        scopes_.push();
        for (auto& s : block->stmts) {
            check_statement(*s);
        }
        scopes_.pop();
    }
}

void TypeChecker::check_let(ast::LetStmt& let) {
    ast::TypePtr init_type;
    if (let.init) {
        if (auto* array_lit = let.init->as<ast::ArrayLiteralExpr>()) {
            if (let.type && let.type->kind == ast::TypeKind::Array) {
                init_type = let.type;
                let.init->type = let.type;
                for (auto& elem : array_lit->elements) {
                    infer_type(*elem);
                }
            } else {
                init_type = infer_type(*let.init);
            }
        } else if (auto* struct_lit = let.init->as<ast::StructLiteralExpr>()) {
            if (let.type && let.type->kind == ast::TypeKind::Struct) {
                if (struct_lit->type_name.empty()) {
                    struct_lit->type_name = let.type->name;
                }
            }
            init_type = infer_type(*let.init);
        } else {
            init_type = infer_type(*let.init);
        }
    }

    if (let.has_ctor_call && let.type) {
        std::string type_name = ast::type_to_string(*let.type);
        std::string ctor_name = type_name + "__ctor";
        if (!let.ctor_args.empty()) {
            ctor_name += "_" + std::to_string(let.ctor_args.size());
        }

        for (auto& arg : let.ctor_args) {
            infer_type(*arg);
        }

        debug::tc::log(debug::tc::Id::Resolved, "Constructor call: " + ctor_name,
                       debug::Level::Debug);
    }

    if (let.type) {
        auto resolved_type = resolve_typedef(let.type);
        if (init_type && !types_compatible(resolved_type, init_type)) {
            error(Span{}, "Type mismatch in variable declaration '" + let.name + "'");
        }
        let.type = resolved_type;
        scopes_.current().define(let.name, resolved_type, let.is_const);
    } else if (init_type) {
        let.type = init_type;
        scopes_.current().define(let.name, init_type, let.is_const);
        debug::tc::log(debug::tc::Id::TypeInfer, let.name + " : " + ast::type_to_string(*init_type),
                       debug::Level::Trace);
    } else {
        error(Span{}, "Cannot infer type for '" + let.name + "'");
    }
}

void TypeChecker::check_return(ast::ReturnStmt& ret) {
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

void TypeChecker::check_if(ast::IfStmt& if_stmt) {
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

void TypeChecker::check_while(ast::WhileStmt& while_stmt) {
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

void TypeChecker::check_for(ast::ForStmt& for_stmt) {
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

void TypeChecker::check_for_in(ast::ForInStmt& for_in) {
    scopes_.push();

    auto iterable_type = infer_type(*for_in.iterable);
    if (!iterable_type) {
        error(Span{}, "Cannot infer type of iterable expression");
        scopes_.pop();
        return;
    }

    ast::TypePtr element_type;
    if (iterable_type->kind == ast::TypeKind::Array) {
        element_type = iterable_type->element_type;
    } else {
        error(Span{}, "For-in requires an iterable type (array), got " +
                          ast::type_to_string(*iterable_type));
        scopes_.pop();
        return;
    }

    if (for_in.var_type) {
        auto resolved_type = resolve_typedef(for_in.var_type);
        if (!types_compatible(resolved_type, element_type)) {
            error(Span{}, "For-in variable type mismatch: expected " +
                              ast::type_to_string(*element_type) + ", got " +
                              ast::type_to_string(*resolved_type));
        }
        for_in.var_type = resolved_type;
    } else {
        for_in.var_type = element_type;
    }

    scopes_.current().define(for_in.var_name, for_in.var_type, false);

    for (auto& s : for_in.body) {
        check_statement(*s);
    }

    scopes_.pop();
}

}  // namespace cm
