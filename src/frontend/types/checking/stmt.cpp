// ============================================================
// TypeChecker 実装 - 文のチェック
// ============================================================

#include "../type_checker.hpp"

namespace cm {

void TypeChecker::check_statement(ast::Stmt& stmt) {
    debug::tc::log(debug::tc::Id::CheckStmt, "", debug::Level::Trace);

    // エラー表示用に現在の文のSpanを保存
    current_span_ = stmt.span;

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
    // エラー表示用に文のSpanを保存
    Span stmt_span = current_span_;

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

    // auto型（Inferred）の場合は初期化式から型を推論
    if (let.type && let.type->kind == ast::TypeKind::Inferred) {
        if (init_type) {
            let.type = init_type;
            scopes_.current().define(let.name, init_type, let.is_const, let.is_static, stmt_span);
            debug::tc::log(debug::tc::Id::TypeInfer,
                           "auto " + let.name + " : " + ast::type_to_string(*init_type),
                           debug::Level::Trace);
        } else {
            error(stmt_span,
                  "Cannot infer type for 'auto' variable '" + let.name + "' without initializer");
        }
    } else if (let.type) {
        auto resolved_type = resolve_typedef(let.type);
        // ポインタ型の場合、宣言時のconst情報を保持（借用システム Phase 2）
        // const int* p = &x の場合、let.type->element_type->qualifiers.is_const = true
        if (resolved_type->kind == ast::TypeKind::Pointer &&
            let.type->kind == ast::TypeKind::Pointer && let.type->element_type &&
            let.type->element_type->qualifiers.is_const) {
            if (resolved_type->element_type) {
                resolved_type->element_type->qualifiers.is_const = true;
            }
        }
        // 借用システム Phase 2: const変数がポインタ型の場合、element_typeにconstを適用
        // Cmでは "const int* p" は "const (int*) p" としてパースされるため、
        // ポインタのelement_typeにconst修飾を伝播する
        if (let.is_const && resolved_type->kind == ast::TypeKind::Pointer &&
            resolved_type->element_type) {
            resolved_type->element_type->qualifiers.is_const = true;
        }
        if (init_type && !types_compatible(resolved_type, init_type)) {
            error(stmt_span, "Type mismatch in variable declaration '" + let.name +
                                 "': expected '" + ast::type_to_string(*resolved_type) +
                                 "', got '" + ast::type_to_string(*init_type) + "'");
        }
        let.type = resolved_type;
        scopes_.current().define(let.name, resolved_type, let.is_const, let.is_static, stmt_span);
    } else if (init_type) {
        let.type = init_type;
        scopes_.current().define(let.name, init_type, let.is_const, let.is_static, stmt_span);
        debug::tc::log(debug::tc::Id::TypeInfer, let.name + " : " + ast::type_to_string(*init_type),
                       debug::Level::Trace);
    } else {
        error(stmt_span, "Cannot infer type for '" + let.name + "'");
    }

    // 非const変数を追跡（const推奨警告用）
    if (!let.is_const) {
        non_const_variable_spans_[let.name] = stmt_span;
    }

    // 初期化式がある場合は初期化済みとしてマーク
    if (let.init) {
        mark_variable_initialized(let.name);
    }

    // 命名規則チェック
    // _ で始まる変数は意図的に命名規則を無視（unused placeholder等）
    if (!let.name.empty() && let.name[0] != '_') {
        // name_spanが設定されていればそれを使用、なければstmt_span
        Span name_pos = let.name_span.is_empty() ? stmt_span : let.name_span;
        if (let.is_const) {
            // L102: 定数はUPPER_SNAKE_CASE（ただし小文字snake_caseも許容）
            // 厳密なチェックは設定可能にする予定
            if (enable_lint_warnings_ && !is_snake_case(let.name) &&
                !is_upper_snake_case(let.name)) {
                warning(name_pos, "Constant name '" + let.name +
                                      "' should be UPPER_SNAKE_CASE or snake_case [L102]");
            }
        } else {
            // L101: 変数はsnake_case
            if (enable_lint_warnings_ && !is_snake_case(let.name)) {
                warning(name_pos, "Variable name '" + let.name + "' should be snake_case [L101]");
            }
        }
    }
}

void TypeChecker::check_return(ast::ReturnStmt& ret) {
    Span stmt_span = current_span_;
    if (!current_return_type_)
        return;

    if (ret.value) {
        auto val_type = infer_type(*ret.value);
        if (!types_compatible(current_return_type_, val_type)) {
            error(stmt_span, "Return type mismatch: expected '" +
                                 ast::type_to_string(*current_return_type_) + "', got '" +
                                 (val_type ? ast::type_to_string(*val_type) : "unknown") + "'");
        }

        // ライフタイムチェック: ローカル変数への参照を返すことを禁止
        // return &x の場合、xがローカル変数ならダングリングポインタになる
        // ただし、static変数はプログラム全体のライフタイムを持つので許可
        if (val_type && val_type->kind == ast::TypeKind::Pointer) {
            if (auto* unary = ret.value->as<ast::UnaryExpr>()) {
                if (unary->op == ast::UnaryOp::AddrOf) {
                    if (auto* ident = unary->operand->as<ast::IdentExpr>()) {
                        // 変数のスコープレベルを確認
                        int var_level = scopes_.current().get_scope_level(ident->name);
                        // static変数かどうか確認
                        auto sym = scopes_.current().lookup(ident->name);
                        bool is_static = sym && sym->is_static;
                        // レベル1以上はローカル変数（0=グローバル）、ただしstaticは除外
                        if (var_level >= 1 && !is_static) {
                            error(stmt_span,
                                  "Cannot return reference to local variable '" + ident->name +
                                      "': variable will be dropped when function returns");
                        }
                    }
                }
            }
        }
    } else if (current_return_type_->kind != ast::TypeKind::Void) {
        error(stmt_span, "Missing return value: expected '" +
                             ast::type_to_string(*current_return_type_) + "'");
    }
}

void TypeChecker::check_if(ast::IfStmt& if_stmt) {
    Span stmt_span = current_span_;
    auto cond_type = infer_type(*if_stmt.condition);
    if (cond_type && cond_type->kind != ast::TypeKind::Bool) {
        error(stmt_span,
              "If condition must be bool, got '" + ast::type_to_string(*cond_type) + "'");
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
    Span stmt_span = current_span_;
    auto cond_type = infer_type(*while_stmt.condition);
    if (cond_type && cond_type->kind != ast::TypeKind::Bool) {
        error(stmt_span,
              "While condition must be bool, got '" + ast::type_to_string(*cond_type) + "'");
    }

    scopes_.push();
    for (auto& s : while_stmt.body) {
        check_statement(*s);
    }
    scopes_.pop();
}

void TypeChecker::check_for(ast::ForStmt& for_stmt) {
    Span stmt_span = current_span_;
    scopes_.push();

    if (for_stmt.init) {
        check_statement(*for_stmt.init);
    }
    if (for_stmt.condition) {
        auto cond_type = infer_type(*for_stmt.condition);
        if (cond_type && cond_type->kind != ast::TypeKind::Bool) {
            error(stmt_span,
                  "For condition must be bool, got '" + ast::type_to_string(*cond_type) + "'");
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
    Span stmt_span = current_span_;
    scopes_.push();

    auto iterable_type = infer_type(*for_in.iterable);
    if (!iterable_type) {
        error(stmt_span, "Cannot infer type of iterable expression");
        scopes_.pop();
        return;
    }

    ast::TypePtr element_type;

    // Struct型の場合: iter()メソッドがあるか確認
    if (iterable_type->kind == ast::TypeKind::Struct) {
        std::string type_name = ast::type_to_string(*iterable_type);

        // iter()メソッドを検索
        auto it = type_methods_.find(type_name);
        if (it != type_methods_.end()) {
            auto method_it = it->second.find("iter");
            if (method_it != it->second.end()) {
                // iter()メソッドが存在 → イテレータベースで展開
                for_in.use_iterator = true;

                // イテレータの戻り値型を取得
                if (method_it->second.return_type) {
                    for_in.iterator_type_name = ast::type_to_string(*method_it->second.return_type);

                    // イテレータのnext()メソッドから要素型を推定
                    auto iter_it = type_methods_.find(for_in.iterator_type_name);
                    if (iter_it != type_methods_.end()) {
                        auto next_it = iter_it->second.find("next");
                        if (next_it != iter_it->second.end() && next_it->second.return_type) {
                            element_type = next_it->second.return_type;
                        }
                    }
                }

                debug::tc::log(debug::tc::Id::TypeInfer,
                               "for-in: using iterator pattern for " + type_name +
                                   " (iterator: " + for_in.iterator_type_name + ")",
                               debug::Level::Debug);
            }
        }

        // iter()メソッドがない場合はエラー
        if (!for_in.use_iterator) {
            error(stmt_span,
                  "For-in requires an iterable type (array or type with iter() method), got '" +
                      ast::type_to_string(*iterable_type) + "'");
            scopes_.pop();
            return;
        }
    } else if (iterable_type->kind == ast::TypeKind::Array) {
        // 配列型: 従来のインデックスベース展開
        element_type = iterable_type->element_type;
    } else {
        error(stmt_span, "For-in requires an iterable type (array), got '" +
                             ast::type_to_string(*iterable_type) + "'");
        scopes_.pop();
        return;
    }

    if (for_in.var_type) {
        auto resolved_type = resolve_typedef(for_in.var_type);
        // auto（Inferred型）の場合は要素型を使用
        if (resolved_type->kind == ast::TypeKind::Inferred) {
            for_in.var_type = element_type;
        } else if (!types_compatible(resolved_type, element_type)) {
            error(stmt_span, "For-in variable type mismatch: expected '" +
                                 ast::type_to_string(*element_type) + "', got '" +
                                 ast::type_to_string(*resolved_type) + "'");
        } else {
            for_in.var_type = resolved_type;
        }
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
