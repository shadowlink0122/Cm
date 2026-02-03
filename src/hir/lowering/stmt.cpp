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
    } else if (auto* must_stmt = stmt.as<ast::MustBlockStmt>()) {
        return lower_must_block(*must_stmt);
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

// must {} ブロック（最適化禁止）
HirStmtPtr HirLowering::lower_must_block(ast::MustBlockStmt& must) {
    auto hir_must = std::make_unique<HirMustBlock>();
    for (auto& s : must.body) {
        if (auto hs = lower_stmt(*s)) {
            hir_must->body.push_back(std::move(hs));
        }
    }
    return std::make_unique<HirStmt>(std::move(hir_must));
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
            (void)move_expr;  // 未使用警告を抑制（検出のみで実際の値は使用しない）
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

            // 初期化式がTagged Union型の場合、変数の型も同様に設定
            // これによりenum変数がTagged Union構造体として正しく型付けされる
            if (hir_let->init && hir_let->init->type) {
                const auto& init_type_name = hir_let->init->type->name;
                if (init_type_name.find("__TaggedUnion_") == 0) {
                    hir_let->type = hir_let->init->type;
                }
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
        // イテレータ型を設定（Struct型として作成）
        if (!for_in.iterator_type_name.empty()) {
            iter_let->type = ast::make_named(for_in.iterator_type_name);
        }

        // collection.iter() 呼び出し
        auto iter_call = std::make_unique<HirCall>();
        std::string type_name =
            for_in.iterable->type ? ast::type_to_string(*for_in.iterable->type) : "";
        iter_call->func_name = type_name + "__iter";
        iter_call->args.push_back(lower_expr(*for_in.iterable));
        iter_let->init = std::make_unique<HirExpr>(std::move(iter_call), iter_let->type);

        hir_block->stmts.push_back(std::make_unique<HirStmt>(std::move(iter_let)));

        // 2. whileループ: while (__iter.has_next()) { ... }
        auto hir_while = std::make_unique<HirWhile>();

        // 条件: __iter.has_next()
        auto has_next_call = std::make_unique<HirCall>();
        has_next_call->func_name = for_in.iterator_type_name + "__has_next";
        // イテレータのアドレスを渡す（selfはポインタとして受け取る）
        auto iter_ref = std::make_unique<HirVarRef>();
        iter_ref->name = iter_name;
        auto iter_addr = std::make_unique<HirUnary>();
        iter_addr->op = HirUnaryOp::AddrOf;
        iter_addr->operand = std::make_unique<HirExpr>(std::move(iter_ref), nullptr);
        has_next_call->args.push_back(std::make_unique<HirExpr>(std::move(iter_addr), nullptr));
        hir_while->cond = std::make_unique<HirExpr>(std::move(has_next_call), ast::make_bool());

        // ループ本体
        // var = __iter.next()
        auto elem_let = std::make_unique<HirLet>();
        elem_let->name = for_in.var_name;
        elem_let->type = for_in.var_type;

        auto next_call = std::make_unique<HirCall>();
        next_call->func_name = for_in.iterator_type_name + "__next";
        // イテレータのアドレスを渡す（selfはポインタとして受け取る）
        auto iter_ref2 = std::make_unique<HirVarRef>();
        iter_ref2->name = iter_name;
        auto iter_addr2 = std::make_unique<HirUnary>();
        iter_addr2->op = HirUnaryOp::AddrOf;
        iter_addr2->operand = std::make_unique<HirExpr>(std::move(iter_ref2), nullptr);
        next_call->args.push_back(std::make_unique<HirExpr>(std::move(iter_addr2), nullptr));
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

    // v0.13.0: match式を文としてif-elseチェーンに変換
    if (auto* match_expr = expr_stmt.expr->as<ast::MatchExpr>()) {
        return lower_match_as_stmt(*match_expr);
    }

    // __llvm__ の特別処理
    if (auto* call = expr_stmt.expr->as<ast::CallExpr>()) {
        if (auto* ident = call->callee->as<ast::IdentExpr>()) {
            if (ident->name == "__llvm__") {
                // 引数から文字列リテラルを取得
                if (!call->args.empty()) {
                    if (auto* arg = call->args[0]->as<ast::LiteralExpr>()) {
                        if (std::holds_alternative<std::string>(arg->value)) {
                            auto hir_asm = std::make_unique<HirAsm>();
                            std::string code = std::get<std::string>(arg->value);

                            // ${+r:varname} パターンを検出して展開
                            // フォーマット: ${constraint:variable}
                            // 例: ${+r:x} → %0 (制約+r、変数x)
                            std::string processed_code;
                            std::string constraints;
                            std::vector<std::string> operand_vars;
                            size_t operand_idx = 0;

                            size_t pos = 0;
                            while (pos < code.size()) {
                                // ${...} パターンを探す
                                size_t start = code.find("${", pos);
                                if (start == std::string::npos) {
                                    processed_code += code.substr(pos);
                                    break;
                                }

                                // ${の前の部分を追加
                                processed_code += code.substr(pos, start - pos);

                                // }を探す
                                size_t end = code.find("}", start);
                                if (end == std::string::npos) {
                                    processed_code += code.substr(start);
                                    break;
                                }

                                // ${...} の内容を解析
                                std::string inner = code.substr(start + 2, end - start - 2);
                                size_t colon = inner.find(':');
                                if (colon != std::string::npos) {
                                    std::string constraint = inner.substr(0, colon);
                                    std::string varname = inner.substr(colon + 1);

                                    // 同じ変数名かつ同じ制約が既に登録されているかチェック
                                    // 注意:
                                    // 同じ変数でも制約が異なる場合は別オペランドとして登録する
                                    // これにより ${r:x} と ${=r:x} は別々のオペランドになる
                                    int existing_idx = -1;
                                    for (size_t i = 0; i < hir_asm->operands.size(); ++i) {
                                        if (hir_asm->operands[i].var_name == varname &&
                                            hir_asm->operands[i].constraint == constraint) {
                                            existing_idx = static_cast<int>(i);
                                            break;
                                        }
                                    }

                                    if (existing_idx >= 0) {
                                        // 既存のオペランド番号を再利用
                                        processed_code += "$" + std::to_string(existing_idx);
                                    } else {
                                        // 新規オペランド: 番号を割り当て
                                        processed_code += "$" + std::to_string(operand_idx);

                                        // 制約文字列に追加
                                        if (operand_idx > 0)
                                            constraints += ",";
                                        constraints += constraint;

                                        // HirAsm.operandsにAsmOperandを格納
                                        hir_asm->operands.push_back(
                                            AsmOperand{constraint, varname});
                                        operand_idx++;

                                        debug::hir::log(
                                            debug::hir::Id::StmtLower,
                                            "asm operand: " + constraint + ":" + varname,
                                            debug::Level::Debug);
                                    }
                                } else {
                                    // 制約なし: そのまま出力
                                    processed_code += "${" + inner + "}";
                                }

                                pos = end + 1;
                            }

                            hir_asm->code = processed_code;
                            hir_asm->is_must = true;

                            debug::hir::log(debug::hir::Id::StmtLower,
                                            "__llvm__: " + hir_asm->code + " operands=" +
                                                std::to_string(hir_asm->operands.size()),
                                            debug::Level::Debug);
                            return std::make_unique<HirStmt>(std::move(hir_asm));
                        }
                    }
                }
                // 引数が不正な場合
                debug::hir::log(debug::hir::Id::StmtLower,
                                "__llvm__ requires string literal argument", debug::Level::Error);
                return nullptr;
            }
        }
    }

    auto hir = std::make_unique<HirExprStmt>();
    hir->expr = lower_expr(*expr_stmt.expr);
    return std::make_unique<HirStmt>(std::move(hir));
}

// v0.13.0: match式を文として処理（if-elseチェーンに変換）
// 式形式とブロック形式の両方をサポート
HirStmtPtr HirLowering::lower_match_as_stmt(ast::MatchExpr& match) {
    debug::hir::log(debug::hir::Id::StmtLower, "Lowering match as statement", debug::Level::Debug);

    // AST段階の元の型名を保存（enum_defs_検索用）
    std::string original_enum_name;
    if (match.scrutinee && match.scrutinee->type) {
        original_enum_name = match.scrutinee->type->name;
    }
    // scrutinee->type->nameが空の場合、パターンのenum_variantからenum名を抽出
    if (original_enum_name.empty()) {
        for (const auto& arm : match.arms) {
            if (arm.pattern &&
                (arm.pattern->kind == ast::MatchPatternKind::EnumVariant ||
                 arm.pattern->kind == ast::MatchPatternKind::EnumVariantWithBinding)) {
                const std::string& variant_name = arm.pattern->enum_variant;
                auto sep = variant_name.rfind("::");
                if (sep != std::string::npos) {
                    original_enum_name = variant_name.substr(0, sep);
                    break;
                }
            }
        }
    }

    auto scrutinee = lower_expr(*match.scrutinee);
    auto scrutinee_type = scrutinee->type;

    if (match.arms.empty()) {
        // 空のmatchは空のブロックを返す
        return std::make_unique<HirStmt>(std::make_unique<HirBlock>());
    }

    // matchをif-elseチェーンに変換
    // 逆順にarmsを処理し、ネストしたif-else構造を構築
    HirStmtPtr result = nullptr;

    // ワイルドカードアームを探す（else部分になる）
    std::vector<HirStmtPtr> else_stmts;
    int wildcard_arm_idx = -1;
    for (size_t i = 0; i < match.arms.size(); ++i) {
        if (match.arms[i].pattern->kind == ast::MatchPatternKind::Wildcard) {
            wildcard_arm_idx = static_cast<int>(i);
            auto& arm = match.arms[i];
            if (arm.is_block_form) {
                // ブロック形式
                for (auto& stmt : arm.block_body) {
                    if (auto hir_stmt = lower_stmt(*stmt)) {
                        else_stmts.push_back(std::move(hir_stmt));
                    }
                }
            } else if (arm.expr_body) {
                // 式形式（文として実行）
                auto expr = lower_expr(*arm.expr_body);
                auto expr_stmt = std::make_unique<HirExprStmt>();
                expr_stmt->expr = std::move(expr);
                else_stmts.push_back(std::make_unique<HirStmt>(std::move(expr_stmt)));
            }
            break;
        }
    }

    // 非ワイルドカードアームを逆順に処理
    for (int i = static_cast<int>(match.arms.size()) - 1; i >= 0; i--) {
        if (i == wildcard_arm_idx)
            continue;  // ワイルドカードはスキップ

        auto& arm = match.arms[i];

        // armのbodyを文のリストとしてlower
        std::vector<HirStmtPtr> body_stmts;

        // EnumVariantWithBindingパターンの場合、ペイロード抽出を生成
        if (arm.pattern && arm.pattern->kind == ast::MatchPatternKind::EnumVariantWithBinding) {
            if (!arm.pattern->binding_name.empty()) {
                // ペイロード抽出: binding_name = ペイロード値
                // enum定義からバリアントのフィールド型を取得
                TypePtr payload_type = scrutinee_type;  // フォールバック
                std::string variant_name = arm.pattern->enum_variant;

                // 元のenum名でenum定義を検索
                if (!original_enum_name.empty()) {
                    auto enum_it = enum_defs_.find(original_enum_name);
                    if (enum_it != enum_defs_.end() && enum_it->second) {
                        // バリアント名を取得（Type::Variant形式からVariantを抽出）
                        auto sep = variant_name.rfind("::");
                        std::string short_variant = (sep != std::string::npos)
                                                        ? variant_name.substr(sep + 2)
                                                        : variant_name;
                        // enum定義からフィールド型を取得
                        for (const auto& member : enum_it->second->members) {
                            if (member.name == short_variant && !member.fields.empty()) {
                                payload_type = member.fields[0].second;
                                break;
                            }
                        }
                    }
                }

                // HirEnumPayloadノードを生成してペイロード抽出を表現
                auto payload_extract = std::make_unique<HirEnumPayload>();
                payload_extract->scrutinee = clone_hir_expr(scrutinee);
                payload_extract->variant_name = variant_name;
                payload_extract->payload_type = payload_type;

                auto var_decl = std::make_unique<HirLet>();
                var_decl->name = arm.pattern->binding_name;
                var_decl->type = payload_type;
                var_decl->init =
                    std::make_unique<HirExpr>(std::move(payload_extract), payload_type);
                var_decl->is_const = false;
                body_stmts.push_back(std::make_unique<HirStmt>(std::move(var_decl)));
            }
        }

        // Variableパターンの場合もバインディング変数に代入
        if (arm.pattern && arm.pattern->kind == ast::MatchPatternKind::Variable) {
            if (!arm.pattern->var_name.empty()) {
                auto var_decl = std::make_unique<HirLet>();
                var_decl->name = arm.pattern->var_name;
                var_decl->type = scrutinee_type;
                var_decl->init = clone_hir_expr(scrutinee);
                var_decl->is_const = false;
                body_stmts.push_back(std::make_unique<HirStmt>(std::move(var_decl)));
            }
        }

        if (arm.is_block_form) {
            // ブロック形式
            for (auto& stmt : arm.block_body) {
                if (auto hir_stmt = lower_stmt(*stmt)) {
                    body_stmts.push_back(std::move(hir_stmt));
                }
            }
        } else if (arm.expr_body) {
            // 式形式（文として実行）
            auto expr = lower_expr(*arm.expr_body);
            auto expr_stmt = std::make_unique<HirExprStmt>();
            expr_stmt->expr = std::move(expr);
            body_stmts.push_back(std::make_unique<HirStmt>(std::move(expr_stmt)));
        }

        // 条件を構築
        auto cond = build_match_condition(scrutinee, scrutinee_type, arm);

        // ガード条件がある場合は結合
        if (arm.guard) {
            HirExprPtr guard_cond;
            // EnumVariantWithBindingの場合、ガード内のバインディング変数をペイロードで置換
            if (arm.pattern && arm.pattern->kind == ast::MatchPatternKind::EnumVariantWithBinding &&
                !arm.pattern->binding_name.empty()) {
                // ペイロード型を取得（677-702行と同じロジック）
                TypePtr payload_type = scrutinee_type;
                std::string variant_name = arm.pattern->enum_variant;
                if (!original_enum_name.empty()) {
                    auto enum_it = enum_defs_.find(original_enum_name);
                    if (enum_it != enum_defs_.end() && enum_it->second) {
                        auto sep = variant_name.rfind("::");
                        std::string short_variant = (sep != std::string::npos)
                                                        ? variant_name.substr(sep + 2)
                                                        : variant_name;
                        for (const auto& member : enum_it->second->members) {
                            if (member.name == short_variant && !member.fields.empty()) {
                                payload_type = member.fields[0].second;
                                break;
                            }
                        }
                    }
                }
                // ペイロード抽出式を生成
                auto payload_extract = std::make_unique<HirEnumPayload>();
                payload_extract->scrutinee = clone_hir_expr(scrutinee);
                payload_extract->variant_name = variant_name;
                payload_extract->payload_type = payload_type;
                auto payload_expr =
                    std::make_unique<HirExpr>(std::move(payload_extract), payload_type);
                // ガード内のバインディング変数をペイロードで置換
                guard_cond = lower_guard_with_binding(*arm.guard, arm.pattern->binding_name,
                                                      payload_expr, payload_type);
            } else {
                guard_cond = lower_expr(*arm.guard);
            }
            auto combined = std::make_unique<HirBinary>();
            combined->op = HirBinaryOp::And;
            combined->lhs = std::move(cond);
            combined->rhs = std::move(guard_cond);
            cond = std::make_unique<HirExpr>(std::move(combined), ast::make_bool());
        }

        // if文を構築
        auto if_stmt = std::make_unique<HirIf>();
        if_stmt->cond = std::move(cond);
        if_stmt->then_block = std::move(body_stmts);

        // else部分（前に処理したarmの結果、または最後のワイルドカード）
        if (result) {
            if_stmt->else_block.push_back(std::move(result));
        } else if (!else_stmts.empty()) {
            if_stmt->else_block = std::move(else_stmts);
            else_stmts.clear();  // 使用済み
        }

        result = std::make_unique<HirStmt>(std::move(if_stmt));
    }

    // resultがない場合（ワイルドカードのみの場合）
    if (!result && !else_stmts.empty()) {
        auto block = std::make_unique<HirBlock>();
        block->stmts = std::move(else_stmts);
        return std::make_unique<HirStmt>(std::move(block));
    }

    if (!result) {
        // 空のブロックを返す
        return std::make_unique<HirStmt>(std::make_unique<HirBlock>());
    }

    return result;
}

}  // namespace cm::hir
