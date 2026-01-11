// lowering_stmt.cpp - 文のlowering
#include "fwd.hpp"

namespace cm::hir {

// 文の変換
HirStmtPtr HirLowering::lower_stmt(ast::Stmt& stmt) {
    if (auto* let = stmt.as<ast::LetStmt>()) {
        return lower_let(*let);
    } else if (auto* ret = stmt.as<ast::ReturnStmt>()) {
        return lower_return(*ret);
    } else if (auto* if_stmt = stmt.as<ast::IfStmt>()) {
        return lower_if(*if_stmt);
    } else if (auto* while_stmt = stmt.as<ast::WhileStmt>()) {
        return lower_while(*while_stmt);
    } else if (auto* for_stmt = stmt.as<ast::ForStmt>()) {
        return lower_for(*for_stmt);
    } else if (auto* for_in = stmt.as<ast::ForInStmt>()) {
        return lower_for_in(*for_in);
    } else if (auto* switch_stmt = stmt.as<ast::SwitchStmt>()) {
        return lower_switch(*switch_stmt);
    } else if (auto* expr_stmt = stmt.as<ast::ExprStmt>()) {
        return lower_expr_stmt(*expr_stmt);
    } else if (auto* block_stmt = stmt.as<ast::BlockStmt>()) {
        return lower_block(*block_stmt);
    } else if (stmt.as<ast::BreakStmt>()) {
        return std::make_unique<HirStmt>(std::make_unique<HirBreak>());
    } else if (stmt.as<ast::ContinueStmt>()) {
        return std::make_unique<HirStmt>(std::make_unique<HirContinue>());
    } else if (auto* defer_stmt = stmt.as<ast::DeferStmt>()) {
        return lower_defer(*defer_stmt);
    }
    return nullptr;
}

// defer文
HirStmtPtr HirLowering::lower_defer(ast::DeferStmt& defer) {
    auto hir_defer = std::make_unique<HirDefer>();
    if (defer.body) {
        hir_defer->body = lower_stmt(*defer.body);
    }
    return std::make_unique<HirStmt>(std::move(hir_defer));
}

// ブロック文
HirStmtPtr HirLowering::lower_block(ast::BlockStmt& block) {
    auto hir_block = std::make_unique<HirBlock>();
    for (auto& s : block.stmts) {
        if (auto hs = lower_stmt(*s)) {
            hir_block->stmts.push_back(std::move(hs));
        }
    }
    return std::make_unique<HirStmt>(std::move(hir_block));
}

// let文
HirStmtPtr HirLowering::lower_let(ast::LetStmt& let) {
    debug::hir::log(debug::hir::Id::LetLower, "let " + let.name, debug::Level::Debug);
    debug::hir::log(debug::hir::Id::LetName, let.name, debug::Level::Trace);

    if (let.is_const) {
        debug::hir::log(debug::hir::Id::LetConst, "const variable: " + let.name,
                        debug::Level::Trace);
    }
    if (let.is_static) {
        debug::hir::log(debug::hir::Id::LetLower, "static variable: " + let.name,
                        debug::Level::Debug);
    }

    auto hir_let = std::make_unique<HirLet>();
    hir_let->name = let.name;
    hir_let->type = let.type;
    hir_let->is_const = let.is_const;
    hir_let->is_static = let.is_static;

    if (let.type) {
        debug::hir::log(debug::hir::Id::LetType, type_to_string(*let.type), debug::Level::Trace);
    }

    if (let.init) {
        debug::hir::log(debug::hir::Id::LetInit, "initializer present", debug::Level::Trace);

        // move初期化の検出（真のゼロコストmove用）
        if (auto* move_expr = let.init->as<ast::MoveExpr>()) {
            hir_let->is_move = true;
            debug::hir::log(debug::hir::Id::LetInit,
                            "move initialization detected for: " + let.name, debug::Level::Debug);
        }

        // 暗黙的構造体リテラルに型を伝播
        if (let.type && let.type->kind == ast::TypeKind::Struct) {
            if (auto* struct_lit = let.init->as<ast::StructLiteralExpr>()) {
                if (struct_lit->type_name.empty()) {
                    struct_lit->type_name = let.type->name;
                    debug::hir::log(debug::hir::Id::LetInit,
                                    "Propagated type to implicit struct literal: " + let.type->name,
                                    debug::Level::Debug);
                }
            }
        }
        // 配列リテラルへの型伝播
        if (let.type && let.type->kind == ast::TypeKind::Array && let.type->element_type) {
            if (auto* array_lit = let.init->as<ast::ArrayLiteralExpr>()) {
                if (let.type->element_type->kind == ast::TypeKind::Struct) {
                    for (auto& elem : array_lit->elements) {
                        if (auto* struct_lit = elem->as<ast::StructLiteralExpr>()) {
                            if (struct_lit->type_name.empty()) {
                                struct_lit->type_name = let.type->element_type->name;
                            }
                        }
                    }
                }
            }
        }

        // initが構造体のコンストラクタ呼び出しかチェック
        bool is_constructor_init = false;
        if (auto* call = let.init->as<ast::CallExpr>()) {
            if (auto* ident = call->callee->as<ast::IdentExpr>()) {
                if (let.type && ident->name == let.type->name) {
                    is_constructor_init = true;
                    std::vector<ast::ExprPtr> ctor_args_temp;
                    for (auto& arg : call->args) {
                        ctor_args_temp.push_back(std::move(arg));
                    }
                    let.ctor_args = std::move(ctor_args_temp);
                    let.has_ctor_call = true;
                    debug::hir::log(debug::hir::Id::LetInit,
                                    "Detected constructor init: " + ident->name + " with " +
                                        std::to_string(let.ctor_args.size()) + " args",
                                    debug::Level::Debug);
                }
            }
        }

        if (!is_constructor_init) {
            auto init_type = let.init->type;
            if (let.type && let.type->kind != ast::TypeKind::Struct && init_type &&
                init_type->kind == ast::TypeKind::Struct) {
                std::string default_member = get_default_member_name(init_type->name);
                if (!default_member.empty()) {
                    debug::hir::log(debug::hir::Id::LetInit,
                                    "Converting to default member access: " + default_member,
                                    debug::Level::Debug);
                    auto member = std::make_unique<HirMember>();
                    member->object = lower_expr(*let.init);
                    member->member = default_member;
                    hir_let->init = std::make_unique<HirExpr>(std::move(member), let.type);
                } else {
                    hir_let->init = lower_expr(*let.init);
                }
            } else {
                hir_let->init = lower_expr(*let.init);
            }
        }
    }

    // コンストラクタ呼び出しの処理
    bool should_call_ctor = let.has_ctor_call;

    if (!should_call_ctor && !let.init && let.type) {
        std::string type_name = type_to_string(*let.type);
        if (types_with_default_ctor_.count(type_name)) {
            should_call_ctor = true;
            debug::hir::log(debug::hir::Id::LetInit,
                            "Implicit default constructor call for: " + type_name,
                            debug::Level::Debug);
        }
    }

    if (should_call_ctor && let.type) {
        std::string type_name = type_to_string(*let.type);
        std::string ctor_name = type_name + "__ctor";
        if (!let.ctor_args.empty()) {
            ctor_name += "_" + std::to_string(let.ctor_args.size());
        }

        debug::hir::log(debug::hir::Id::LetInit, "Adding constructor call: " + ctor_name,
                        debug::Level::Debug);

        auto ctor_call = std::make_unique<HirCall>();
        ctor_call->func_name = ctor_name;

        auto this_ref = std::make_unique<HirVarRef>();
        this_ref->name = let.name;
        ctor_call->args.push_back(std::make_unique<HirExpr>(std::move(this_ref), let.type));

        for (auto& arg : let.ctor_args) {
            ctor_call->args.push_back(lower_expr(*arg));
        }

        hir_let->ctor_call = std::make_unique<HirExpr>(std::move(ctor_call), ast::make_void());
    }

    return std::make_unique<HirStmt>(std::move(hir_let));
}

// return文
HirStmtPtr HirLowering::lower_return(ast::ReturnStmt& ret) {
    auto hir_ret = std::make_unique<HirReturn>();
    if (ret.value) {
        hir_ret->value = lower_expr(*ret.value);
    }
    return std::make_unique<HirStmt>(std::move(hir_ret));
}

// if文
HirStmtPtr HirLowering::lower_if(ast::IfStmt& if_stmt) {
    auto hir_if = std::make_unique<HirIf>();
    hir_if->cond = lower_expr(*if_stmt.condition);

    for (auto& s : if_stmt.then_block) {
        if (auto hs = lower_stmt(*s)) {
            hir_if->then_block.push_back(std::move(hs));
        }
    }

    for (auto& s : if_stmt.else_block) {
        if (auto hs = lower_stmt(*s)) {
            hir_if->else_block.push_back(std::move(hs));
        }
    }

    return std::make_unique<HirStmt>(std::move(hir_if));
}

// while文
HirStmtPtr HirLowering::lower_while(ast::WhileStmt& while_stmt) {
    auto hir_while = std::make_unique<HirWhile>();
    hir_while->cond = lower_expr(*while_stmt.condition);

    for (auto& s : while_stmt.body) {
        if (auto hs = lower_stmt(*s)) {
            hir_while->body.push_back(std::move(hs));
        }
    }

    return std::make_unique<HirStmt>(std::move(hir_while));
}

// for文
HirStmtPtr HirLowering::lower_for(ast::ForStmt& for_stmt) {
    auto hir_for = std::make_unique<HirFor>();

    if (for_stmt.init) {
        hir_for->init = lower_stmt(*for_stmt.init);
    }

    if (for_stmt.condition) {
        hir_for->cond = lower_expr(*for_stmt.condition);
    }

    if (for_stmt.update) {
        hir_for->update = lower_expr(*for_stmt.update);
    }

    for (auto& s : for_stmt.body) {
        if (auto hs = lower_stmt(*s)) {
            hir_for->body.push_back(std::move(hs));
        }
    }

    return std::make_unique<HirStmt>(std::move(hir_for));
}

// for-in文
HirStmtPtr HirLowering::lower_for_in(ast::ForInStmt& for_in) {
    debug::hir::log(debug::hir::Id::LoopLower, "Lowering for-in statement", debug::Level::Debug);

    // イテレータベース展開: for (x in collection) → while (iter.has_next()) { x = iter.next(); ...
    // }
    if (for_in.use_iterator) {
        debug::hir::log(debug::hir::Id::LoopLower,
                        "Using iterator pattern: " + for_in.iterator_type_name,
                        debug::Level::Debug);

        // HirBlockを使ってスコープを作成
        auto hir_block = std::make_unique<HirBlock>();

        // 1. イテレータ変数の宣言: auto __iter = collection.iter();
        std::string iter_name = "__for_in_iter_" + for_in.var_name;
        auto iter_let = std::make_unique<HirLet>();
        iter_let->name = iter_name;
        // イテレータ型は未設定（型推論に任せる）

        // collection.iter() 呼び出し
        auto iter_call = std::make_unique<HirCall>();
        std::string type_name =
            for_in.iterable->type ? ast::type_to_string(*for_in.iterable->type) : "";
        iter_call->func_name = type_name + "__iter";
        iter_call->args.push_back(lower_expr(*for_in.iterable));
        iter_let->init = std::make_unique<HirExpr>(std::move(iter_call), nullptr);

        hir_block->stmts.push_back(std::make_unique<HirStmt>(std::move(iter_let)));

        // 2. whileループ: while (__iter.has_next()) { ... }
        auto hir_while = std::make_unique<HirWhile>();

        // 条件: __iter.has_next()
        auto has_next_call = std::make_unique<HirCall>();
        has_next_call->func_name = for_in.iterator_type_name + "__has_next";
        auto iter_ref = std::make_unique<HirVarRef>();
        iter_ref->name = iter_name;
        has_next_call->args.push_back(std::make_unique<HirExpr>(std::move(iter_ref), nullptr));
        hir_while->cond = std::make_unique<HirExpr>(std::move(has_next_call), ast::make_bool());

        // ループ本体
        // var = __iter.next()
        auto elem_let = std::make_unique<HirLet>();
        elem_let->name = for_in.var_name;
        elem_let->type = for_in.var_type;

        auto next_call = std::make_unique<HirCall>();
        next_call->func_name = for_in.iterator_type_name + "__next";
        auto iter_ref2 = std::make_unique<HirVarRef>();
        iter_ref2->name = iter_name;
        next_call->args.push_back(std::make_unique<HirExpr>(std::move(iter_ref2), nullptr));
        elem_let->init = std::make_unique<HirExpr>(std::move(next_call), for_in.var_type);

        hir_while->body.push_back(std::make_unique<HirStmt>(std::move(elem_let)));

        // ユーザーのループ本体
        for (auto& s : for_in.body) {
            if (auto hs = lower_stmt(*s)) {
                hir_while->body.push_back(std::move(hs));
            }
        }

        hir_block->stmts.push_back(std::make_unique<HirStmt>(std::move(hir_while)));

        return std::make_unique<HirStmt>(std::move(hir_block));
    }

    // 従来のインデックスベース展開（配列/スライス用）
    auto hir_for = std::make_unique<HirFor>();

    auto iterable_type = for_in.iterable->type;
    bool is_fixed_array = iterable_type && iterable_type->kind == ast::TypeKind::Array &&
                          iterable_type->array_size.has_value();
    bool is_slice = iterable_type && iterable_type->kind == ast::TypeKind::Array &&
                    !iterable_type->array_size.has_value();

    std::string idx_name = "__for_in_idx_" + for_in.var_name;

    // init: int __i = 0;
    auto init_let = std::make_unique<HirLet>();
    init_let->name = idx_name;
    init_let->type = ast::make_int();
    auto zero_lit = std::make_unique<HirLiteral>();
    zero_lit->value = int64_t{0};
    init_let->init = std::make_unique<HirExpr>(std::move(zero_lit), ast::make_int());
    hir_for->init = std::make_unique<HirStmt>(std::move(init_let));

    // cond: __i < size (固定配列) または __i < slice.len() (スライス)
    auto idx_ref = std::make_unique<HirVarRef>();
    idx_ref->name = idx_name;
    auto cond_binary = std::make_unique<HirBinary>();
    cond_binary->op = HirBinaryOp::Lt;
    cond_binary->lhs = std::make_unique<HirExpr>(std::move(idx_ref), ast::make_int());

    if (is_fixed_array) {
        uint32_t size = iterable_type->array_size.value();
        auto size_lit = std::make_unique<HirLiteral>();
        size_lit->value = static_cast<int64_t>(size);
        cond_binary->rhs = std::make_unique<HirExpr>(std::move(size_lit), ast::make_int());
    } else if (is_slice) {
        // スライスの場合: iterable.len() を呼び出す
        auto len_call = std::make_unique<HirCall>();
        len_call->func_name = "__builtin_slice_len";
        len_call->args.push_back(lower_expr(*for_in.iterable));
        cond_binary->rhs = std::make_unique<HirExpr>(std::move(len_call), ast::make_int());
    } else {
        // その他（エラーケース）: 0回ループ
        auto zero = std::make_unique<HirLiteral>();
        zero->value = int64_t{0};
        cond_binary->rhs = std::make_unique<HirExpr>(std::move(zero), ast::make_int());
    }
    hir_for->cond = std::make_unique<HirExpr>(std::move(cond_binary), ast::make_bool());

    // update: __i = __i + 1
    auto ast_idx_ref_left = std::make_unique<ast::IdentExpr>(idx_name);
    auto ast_idx_ref_right = std::make_unique<ast::IdentExpr>(idx_name);
    auto ast_one = std::make_unique<ast::LiteralExpr>(int64_t{1});
    auto ast_add = std::make_unique<ast::BinaryExpr>(
        ast::BinaryOp::Add, std::make_unique<ast::Expr>(std::move(ast_idx_ref_right)),
        std::make_unique<ast::Expr>(std::move(ast_one)));
    auto ast_assign = std::make_unique<ast::BinaryExpr>(
        ast::BinaryOp::Assign, std::make_unique<ast::Expr>(std::move(ast_idx_ref_left)),
        std::make_unique<ast::Expr>(std::move(ast_add)));
    ast::Expr update_expr(std::move(ast_assign));
    update_expr.type = ast::make_int();
    hir_for->update = lower_expr(update_expr);

    // ループ変数の初期化
    auto elem_let = std::make_unique<HirLet>();
    elem_let->name = for_in.var_name;
    elem_let->type = for_in.var_type;

    auto arr_expr = lower_expr(*for_in.iterable);
    auto idx_ref3 = std::make_unique<HirVarRef>();
    idx_ref3->name = idx_name;
    auto index_expr = std::make_unique<HirIndex>();
    index_expr->object = std::move(arr_expr);
    index_expr->index = std::make_unique<HirExpr>(std::move(idx_ref3), ast::make_int());
    elem_let->init = std::make_unique<HirExpr>(std::move(index_expr), for_in.var_type);

    hir_for->body.push_back(std::make_unique<HirStmt>(std::move(elem_let)));

    for (auto& s : for_in.body) {
        if (auto hs = lower_stmt(*s)) {
            hir_for->body.push_back(std::move(hs));
        }
    }

    return std::make_unique<HirStmt>(std::move(hir_for));
}

// switch文
HirStmtPtr HirLowering::lower_switch(ast::SwitchStmt& switch_stmt) {
    auto hir_switch = std::make_unique<HirSwitch>();
    hir_switch->expr = lower_expr(*switch_stmt.expr);

    for (auto& case_ : switch_stmt.cases) {
        HirSwitchCase hir_case;

        if (case_.pattern) {
            hir_case.pattern = lower_pattern(*case_.pattern);
        }

        for (auto& stmt : case_.stmts) {
            if (auto hir_stmt = lower_stmt(*stmt)) {
                hir_case.stmts.push_back(std::move(hir_stmt));
            }
        }

        hir_switch->cases.push_back(std::move(hir_case));
    }

    return std::make_unique<HirStmt>(std::move(hir_switch));
}

// パターン
std::unique_ptr<HirSwitchPattern> HirLowering::lower_pattern(ast::Pattern& pattern) {
    auto hir_pattern = std::make_unique<HirSwitchPattern>();

    switch (pattern.kind) {
        case ast::PatternKind::Value:
            hir_pattern->kind = HirSwitchPattern::SingleValue;
            hir_pattern->value = lower_expr(*pattern.value);
            break;

        case ast::PatternKind::Range:
            hir_pattern->kind = HirSwitchPattern::Range;
            hir_pattern->range_start = lower_expr(*pattern.range_start);
            hir_pattern->range_end = lower_expr(*pattern.range_end);
            break;

        case ast::PatternKind::Or:
            hir_pattern->kind = HirSwitchPattern::Or;
            for (auto& sub_pattern : pattern.or_patterns) {
                hir_pattern->or_patterns.push_back(lower_pattern(*sub_pattern));
            }
            break;
    }

    return hir_pattern;
}

// 式文
HirStmtPtr HirLowering::lower_expr_stmt(ast::ExprStmt& expr_stmt) {
    if (!expr_stmt.expr)
        return nullptr;

    auto hir = std::make_unique<HirExprStmt>();
    hir->expr = lower_expr(*expr_stmt.expr);
    return std::make_unique<HirStmt>(std::move(hir));
}

}  // namespace cm::hir
