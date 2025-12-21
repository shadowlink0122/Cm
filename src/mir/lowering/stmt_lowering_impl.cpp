#include "../../common/debug.hpp"
#include "stmt_lowering.hpp"

namespace cm::mir {

// let文のlowering
void StmtLowering::lower_let(const hir::HirLet& let, LoweringContext& ctx) {
    // 新しいローカル変数を作成
    // is_const = true なら変更不可、false なら変更可能
    // is_static = true なら関数呼び出し間で値が保持される
    LocalId local = ctx.new_local(let.name, let.type, !let.is_const, true, let.is_static);

    // 変数をスコープに登録
    ctx.register_variable(let.name, local);

    // const変数の場合、初期値が定数リテラルならその値を保存
    // これにより文字列補間でconst変数の値を直接使用できる
    if (let.is_const && let.init) {
        if (auto* lit = std::get_if<std::unique_ptr<hir::HirLiteral>>(&let.init->kind)) {
            if (*lit) {
                MirConstant const_val;
                const_val.type = let.type;
                const_val.value = (*lit)->value;
                ctx.register_const_value(let.name, const_val);
            }
        }
    }

    // static変数の場合、初期化コードは生成しない
    // LLVMバックエンドでグローバル変数としてゼロ初期化で生成される
    // インタプリタでは初回呼び出し時にのみ初期化される
    if (let.is_static) {
        // 初期化代入は生成しない
        // 注: 現在はゼロ初期化のみサポート。非ゼロ初期値は将来実装
        return;
    }

    // コンストラクタ呼び出しがある場合はlet.initをスキップ（コンストラクタが初期化を担当）
    if (let.init && !let.ctor_call) {
        // 配列→ポインタ暗黙変換のチェック
        // 左辺がポインタ型で右辺が配列型の場合、配列の先頭要素へのアドレスを取得
        bool is_array_to_pointer = false;
        if (let.type && let.init->type && let.type->kind == hir::TypeKind::Pointer &&
            let.init->type->kind == hir::TypeKind::Array) {
            is_array_to_pointer = true;
        }

        if (is_array_to_pointer) {
            // 配列変数への参照を取得
            if (auto* var_ref = std::get_if<std::unique_ptr<hir::HirVarRef>>(&let.init->kind)) {
                auto arr_local = ctx.resolve_variable((*var_ref)->name);
                if (arr_local) {
                    // 配列の先頭要素(&arr[0])へのRefを生成
                    // インデックス0のための一時変数
                    LocalId idx_zero = ctx.new_temp(hir::make_int());
                    MirConstant zero_const;
                    zero_const.value = int64_t(0);
                    zero_const.type = hir::make_int();
                    ctx.push_statement(MirStatement::assign(
                        MirPlace{idx_zero}, MirRvalue::use(MirOperand::constant(zero_const))));

                    // &arr[0] を生成
                    MirPlace arr_elem{*arr_local};
                    arr_elem.projections.push_back(PlaceProjection::index(idx_zero));

                    ctx.push_statement(
                        MirStatement::assign(MirPlace{local}, MirRvalue::ref(arr_elem, false)));
                } else {
                    // フォールバック: 通常のlowering
                    LocalId init_value = expr_lowering->lower_expression(*let.init, ctx);
                    ctx.push_statement(MirStatement::assign(
                        MirPlace{local}, MirRvalue::use(MirOperand::copy(MirPlace{init_value}))));
                }
            } else {
                // 変数参照でない場合は通常処理
                LocalId init_value = expr_lowering->lower_expression(*let.init, ctx);
                ctx.push_statement(MirStatement::assign(
                    MirPlace{local}, MirRvalue::use(MirOperand::copy(MirPlace{init_value}))));
            }
        } else {
            // 通常の初期化
            LocalId init_value = expr_lowering->lower_expression(*let.init, ctx);
            ctx.push_statement(MirStatement::assign(
                MirPlace{local}, MirRvalue::use(MirOperand::copy(MirPlace{init_value}))));
        }
    }

    // コンストラクタ呼び出しがある場合
    if (let.ctor_call) {
        // コンストラクタ呼び出しはHirCall形式
        if (auto* call = std::get_if<std::unique_ptr<hir::HirCall>>(&let.ctor_call->kind)) {
            const auto& hir_call = **call;

            // 引数をlowering
            std::vector<MirOperandPtr> args;

            // HIRのctor_call.argsには既にthis（変数への参照）が含まれている
            // 最初の引数は変数自身への参照なので、直接localを使う
            bool first_arg = true;
            for (const auto& arg : hir_call.args) {
                if (first_arg) {
                    // 最初の引数（this）は変数自身を使う
                    args.push_back(MirOperand::copy(MirPlace{local}));
                    first_arg = false;
                } else {
                    // 残りの引数を通常通りlowering
                    LocalId arg_local = expr_lowering->lower_expression(*arg, ctx);
                    args.push_back(MirOperand::copy(MirPlace{arg_local}));
                }
            }

            // コンストラクタ関数呼び出しを生成
            BlockId success_block = ctx.new_block();
            auto func_operand = MirOperand::function_ref(hir_call.func_name);

            auto call_term = std::make_unique<MirTerminator>();
            call_term->kind = MirTerminator::Call;
            call_term->data = MirTerminator::CallData{std::move(func_operand),
                                                      std::move(args),
                                                      std::nullopt,  // コンストラクタは戻り値なし
                                                      success_block,
                                                      std::nullopt,
                                                      "",
                                                      "",
                                                      false};  // 通常の関数呼び出し
            ctx.set_terminator(std::move(call_term));
            ctx.switch_to_block(success_block);
        }
    }

    // デストラクタを持つ型の変数を登録
    if (let.type && let.type->kind == hir::TypeKind::Struct) {
        std::string type_name = let.type->name;
        if (ctx.has_destructor(type_name)) {
            ctx.register_destructor_var(local, type_name);
        }
    }
}

// 代入文のlowering
void StmtLowering::lower_assign(const hir::HirAssign& assign, LoweringContext& ctx) {
    if (!assign.target || !assign.value) {
        return;
    }

    // 右辺値をlowering
    LocalId rhs_value = expr_lowering->lower_expression(*assign.value, ctx);

    // 左辺値のMirPlaceを構築するヘルパー関数
    // 複雑な左辺値（c.values[0], points[0].x など）を再帰的に処理
    auto build_lvalue_place = [&](const hir::HirExpr* expr, MirPlace& place,
                                  hir::TypePtr& current_type) -> bool {
        // 再帰的にプロジェクションを構築
        std::function<bool(const hir::HirExpr*,
                           std::vector<std::function<void(MirPlace&, LoweringContext&)>>&,
                           hir::TypePtr&)>
            build_projections;
        build_projections =
            [&](const hir::HirExpr* e,
                std::vector<std::function<void(MirPlace&, LoweringContext&)>>& projections,
                hir::TypePtr& typ) -> bool {
            if (auto* var_ref = std::get_if<std::unique_ptr<hir::HirVarRef>>(&e->kind)) {
                // ベース変数
                auto var_id = ctx.resolve_variable((*var_ref)->name);
                if (var_id) {
                    place.local = *var_id;
                    if (*var_id < ctx.func->locals.size()) {
                        typ = ctx.func->locals[*var_id].type;
                    }
                    return true;
                }
                return false;
            } else if (auto* member = std::get_if<std::unique_ptr<hir::HirMember>>(&e->kind)) {
                // メンバーアクセス: object.member
                hir::TypePtr inner_type;
                if (!build_projections((*member)->object.get(), projections, inner_type)) {
                    return false;
                }

                // フィールドプロジェクションを追加
                std::string field_name = (*member)->member;
                projections.push_back(
                    [field_name, inner_type, &ctx](MirPlace& p, LoweringContext&) {
                        if (inner_type && inner_type->kind == hir::TypeKind::Struct) {
                            auto field_idx = ctx.get_field_index(inner_type->name, field_name);
                            if (field_idx) {
                                p.projections.push_back(PlaceProjection::field(*field_idx));
                            }
                        }
                    });

                // 次の型を取得
                if (inner_type && inner_type->kind == hir::TypeKind::Struct) {
                    auto field_idx = ctx.get_field_index(inner_type->name, field_name);
                    if (field_idx && ctx.struct_defs && ctx.struct_defs->count(inner_type->name)) {
                        const auto* struct_def = ctx.struct_defs->at(inner_type->name);
                        if (*field_idx < struct_def->fields.size()) {
                            typ = struct_def->fields[*field_idx].type;
                        }
                    }
                }
                return true;
            } else if (auto* index = std::get_if<std::unique_ptr<hir::HirIndex>>(&e->kind)) {
                // インデックスアクセス: object[index]
                hir::TypePtr inner_type;
                if (!build_projections((*index)->object.get(), projections, inner_type)) {
                    return false;
                }

                // インデックスを評価
                LocalId idx = expr_lowering->lower_expression(*(*index)->index, ctx);

                // インデックスプロジェクションを追加
                projections.push_back([idx](MirPlace& p, LoweringContext&) {
                    p.projections.push_back(PlaceProjection::index(idx));
                });

                // 次の型を取得（配列の要素型）
                if (inner_type && inner_type->kind == hir::TypeKind::Array &&
                    inner_type->element_type) {
                    typ = inner_type->element_type;
                }
                return true;
            } else if (auto* unary = std::get_if<std::unique_ptr<hir::HirUnary>>(&e->kind)) {
                // デリファレンス: *ptr
                if ((*unary)->op == hir::HirUnaryOp::Deref) {
                    hir::TypePtr inner_type;
                    if (!build_projections((*unary)->operand.get(), projections, inner_type)) {
                        return false;
                    }

                    // デリファレンスプロジェクションを追加
                    projections.push_back([](MirPlace& p, LoweringContext&) {
                        p.projections.push_back(PlaceProjection::deref());
                    });

                    // 次の型を取得（ポインタの要素型）
                    if (inner_type && inner_type->kind == hir::TypeKind::Pointer &&
                        inner_type->element_type) {
                        typ = inner_type->element_type;
                    }
                    return true;
                }
                return false;
            }
            return false;
        };

        std::vector<std::function<void(MirPlace&, LoweringContext&)>> projections;
        if (!build_projections(expr, projections, current_type)) {
            return false;
        }

        // プロジェクションを適用
        for (auto& proj : projections) {
            proj(place, ctx);
        }

        return true;
    };

    // 左辺値の種類に応じて処理
    if (auto* var_ref = std::get_if<std::unique_ptr<hir::HirVarRef>>(&assign.target->kind)) {
        // 単純な変数代入
        auto lhs_opt = ctx.resolve_variable((*var_ref)->name);
        if (lhs_opt) {
            ctx.push_statement(MirStatement::assign(
                MirPlace{*lhs_opt}, MirRvalue::use(MirOperand::copy(MirPlace{rhs_value}))));
        }
    } else if (std::get_if<std::unique_ptr<hir::HirMember>>(&assign.target->kind) ||
               std::get_if<std::unique_ptr<hir::HirIndex>>(&assign.target->kind) ||
               std::get_if<std::unique_ptr<hir::HirUnary>>(&assign.target->kind)) {
        // 複雑な左辺値: メンバーアクセス、インデックスアクセス、またはデリファレンス
        // これには c.values[0], points[0].x, arr[i], obj.field, *ptr, (*ptr).x などが含まれる
        MirPlace place{0};
        hir::TypePtr current_type;

        if (build_lvalue_place(assign.target.get(), place, current_type)) {
            ctx.push_statement(
                MirStatement::assign(place, MirRvalue::use(MirOperand::copy(MirPlace{rhs_value}))));
        }
    }
    // その他の左辺値タイプは未対応
}

// return文のlowering
void StmtLowering::lower_return(const hir::HirReturn& ret, LoweringContext& ctx) {
    if (ret.value) {
        // 戻り値をlowering
        LocalId return_value = expr_lowering->lower_expression(*ret.value, ctx);

        // 戻り値をreturn用ローカル変数に代入
        ctx.push_statement(
            MirStatement::assign(MirPlace{ctx.func->return_local},
                                 MirRvalue::use(MirOperand::copy(MirPlace{return_value}))));
    }

    // 現在のスコープのdefer文を実行（逆順）
    auto defers = ctx.get_defer_stmts();
    for (const auto* defer_stmt : defers) {
        lower_statement(*defer_stmt, ctx);
    }

    // デストラクタを呼び出す（逆順）
    auto destructor_vars = ctx.get_all_destructor_vars();
    for (const auto& [local_id, type_name] : destructor_vars) {
        std::string dtor_name = type_name + "__dtor";

        // デストラクタ呼び出しを生成
        std::vector<MirOperandPtr> args;
        args.push_back(MirOperand::copy(MirPlace{local_id}));

        BlockId success_block = ctx.new_block();

        auto func_operand = MirOperand::function_ref(dtor_name);
        auto call_term = std::make_unique<MirTerminator>();
        call_term->kind = MirTerminator::Call;
        call_term->data = MirTerminator::CallData{std::move(func_operand),
                                                  std::move(args),
                                                  std::nullopt,  // void戻り値
                                                  success_block,
                                                  std::nullopt,
                                                  "",
                                                  "",
                                                  false};  // 通常の関数呼び出し
        ctx.set_terminator(std::move(call_term));
        ctx.switch_to_block(success_block);
    }

    // return終端命令
    ctx.set_terminator(MirTerminator::return_value());

    // 新しいブロックを作成（到達不可能だが、CFGの整合性のため）
    ctx.switch_to_block(ctx.new_block());
}

// if文のlowering
void StmtLowering::lower_if(const hir::HirIf& if_stmt, LoweringContext& ctx) {
    // 条件をlowering
    LocalId cond = expr_lowering->lower_expression(*if_stmt.cond, ctx);

    // ブロックを作成
    BlockId then_block = ctx.new_block();
    BlockId else_block = ctx.new_block();
    BlockId merge_block = ctx.new_block();

    // 条件分岐
    ctx.set_terminator(
        MirTerminator::switch_int(MirOperand::copy(MirPlace{cond}), {{1, then_block}}, else_block));

    // then部をlowering
    ctx.switch_to_block(then_block);
    for (const auto& stmt : if_stmt.then_block) {
        lower_statement(*stmt, ctx);
    }
    if (!ctx.get_current_block()->terminator) {
        ctx.set_terminator(MirTerminator::goto_block(merge_block));
    }

    // else部をlowering
    ctx.switch_to_block(else_block);
    for (const auto& stmt : if_stmt.else_block) {
        lower_statement(*stmt, ctx);
    }
    if (!ctx.get_current_block()->terminator) {
        ctx.set_terminator(MirTerminator::goto_block(merge_block));
    }

    // マージポイント
    ctx.switch_to_block(merge_block);
}

// while文のlowering
void StmtLowering::lower_while(const hir::HirWhile& while_stmt, LoweringContext& ctx) {
    // ブロックを作成
    BlockId loop_header = ctx.new_block();
    BlockId loop_body = ctx.new_block();
    BlockId loop_exit = ctx.new_block();

    // ループヘッダへジャンプ
    ctx.set_terminator(MirTerminator::goto_block(loop_header));

    // ループヘッダ（条件評価）
    ctx.switch_to_block(loop_header);
    LocalId cond = expr_lowering->lower_expression(*while_stmt.cond, ctx);
    ctx.set_terminator(
        MirTerminator::switch_int(MirOperand::copy(MirPlace{cond}), {{1, loop_body}}, loop_exit));

    // ループボディ
    ctx.switch_to_block(loop_body);
    ctx.push_loop(loop_header, loop_exit);
    for (const auto& stmt : while_stmt.body) {
        lower_statement(*stmt, ctx);
    }
    ctx.pop_loop();
    if (!ctx.get_current_block()->terminator) {
        ctx.set_terminator(MirTerminator::goto_block(loop_header));
    }

    // ループ出口
    ctx.switch_to_block(loop_exit);
}

// for文のlowering - whileループに展開
// for (init; cond; update) { body } を以下に変換:
// init; while (cond) { body; update; }
void StmtLowering::lower_for(const hir::HirFor& for_stmt, LoweringContext& ctx) {
    // 初期化部
    if (for_stmt.init) {
        lower_statement(*for_stmt.init, ctx);
    }

    // whileループとして処理
    // ブロックを作成
    BlockId loop_header = ctx.new_block();
    BlockId loop_body = ctx.new_block();
    BlockId loop_exit = ctx.new_block();

    // ループヘッダへジャンプ
    ctx.set_terminator(MirTerminator::goto_block(loop_header));

    // ループヘッダ（条件評価）
    ctx.switch_to_block(loop_header);
    if (for_stmt.cond) {
        LocalId cond = expr_lowering->lower_expression(*for_stmt.cond, ctx);
        ctx.set_terminator(MirTerminator::switch_int(MirOperand::copy(MirPlace{cond}),
                                                     {{1, loop_body}}, loop_exit));
    } else {
        // 条件なしの場合は無限ループ
        ctx.set_terminator(MirTerminator::goto_block(loop_body));
    }

    // ループボディ
    ctx.switch_to_block(loop_body);

    // continueの処理のため、更新部がある場合は特別なブロックを用意
    BlockId continue_target = loop_header;
    if (for_stmt.update) {
        // 更新式がある場合、continueは更新部を実行してからヘッダへ
        continue_target = ctx.new_block();
    }

    ctx.push_loop(loop_header, loop_exit, continue_target);

    // ループボディ用のスコープを作成（各反復でdefer文を実行）
    ctx.push_scope();

    // ボディの文を処理
    for (const auto& stmt : for_stmt.body) {
        lower_statement(*stmt, ctx);
    }

    // ループボディ終了時にdefer文を実行（逆順）
    auto defers = ctx.get_defer_stmts();
    for (const auto* defer_stmt : defers) {
        lower_statement(*defer_stmt, ctx);
    }

    ctx.pop_scope();

    // ボディの最後に更新式を追加（whileループと同じ構造）
    if (for_stmt.update && !ctx.get_current_block()->terminator) {
        // 更新式を直接lowering（結果は破棄）
        expr_lowering->lower_expression(*for_stmt.update, ctx);
    }

    ctx.pop_loop();

    // ループヘッダへ戻る
    if (!ctx.get_current_block()->terminator) {
        ctx.set_terminator(MirTerminator::goto_block(loop_header));
    }

    // continueターゲット（更新式がある場合）
    if (for_stmt.update && continue_target != loop_header) {
        ctx.switch_to_block(continue_target);
        // 更新式を直接lowering（結果は破棄）
        expr_lowering->lower_expression(*for_stmt.update, ctx);
        // ヘッダへ戻る
        ctx.set_terminator(MirTerminator::goto_block(loop_header));
    }

    // ループ出口
    ctx.switch_to_block(loop_exit);
}

// loop文のlowering
void StmtLowering::lower_loop(const hir::HirLoop& loop_stmt, LoweringContext& ctx) {
    // ブロックを作成
    BlockId loop_block = ctx.new_block();
    BlockId loop_exit = ctx.new_block();

    // ループブロックへジャンプ
    ctx.set_terminator(MirTerminator::goto_block(loop_block));

    // ループボディ
    ctx.switch_to_block(loop_block);
    ctx.push_loop(loop_block, loop_exit);
    for (const auto& stmt : loop_stmt.body) {
        lower_statement(*stmt, ctx);
    }
    ctx.pop_loop();
    if (!ctx.get_current_block()->terminator) {
        // 無限ループ
        ctx.set_terminator(MirTerminator::goto_block(loop_block));
    }

    // ループ出口（breakで到達）
    ctx.switch_to_block(loop_exit);
}

// switch文のlowering
void StmtLowering::lower_switch(const hir::HirSwitch& switch_stmt, LoweringContext& ctx) {
    // 判別式をlowering
    LocalId discriminant = expr_lowering->lower_expression(*switch_stmt.expr, ctx);

    // 各caseのブロックを作成
    std::vector<std::pair<int64_t, BlockId>> cases;
    std::vector<BlockId> case_blocks;
    for (size_t i = 0; i < switch_stmt.cases.size(); ++i) {
        // else/defaultケース（patternがnull）はスキップ
        if (!switch_stmt.cases[i].pattern) {
            case_blocks.push_back(0);  // プレースホルダー
            continue;
        }

        BlockId case_block = ctx.new_block();
        case_blocks.push_back(case_block);

        // case値を評価
        int64_t case_value = 0;

        // patternがSingleValueの場合、その値を取得
        if (switch_stmt.cases[i].pattern &&
            switch_stmt.cases[i].pattern->kind == hir::HirSwitchPattern::SingleValue &&
            switch_stmt.cases[i].pattern->value) {
            // pattern->valueから値を取得
            if (auto lit = std::get_if<std::unique_ptr<hir::HirLiteral>>(
                    &switch_stmt.cases[i].pattern->value->kind)) {
                if (*lit) {
                    auto& val = (*lit)->value;
                    if (std::holds_alternative<int64_t>(val)) {
                        case_value = std::get<int64_t>(val);
                    } else if (std::holds_alternative<char>(val)) {
                        case_value = static_cast<int64_t>(std::get<char>(val));
                    }
                }
            }
        }
        // 旧互換性のためvalueフィールドも確認
        else if (switch_stmt.cases[i].value) {
            if (auto lit = std::get_if<std::unique_ptr<hir::HirLiteral>>(
                    &switch_stmt.cases[i].value->kind)) {
                if (*lit) {
                    auto& val = (*lit)->value;
                    if (std::holds_alternative<int64_t>(val)) {
                        case_value = std::get<int64_t>(val);
                    } else if (std::holds_alternative<char>(val)) {
                        case_value = static_cast<int64_t>(std::get<char>(val));
                    }
                }
            }
        }
        cases.push_back({case_value, case_block});
    }

    // defaultブロック
    BlockId default_block = ctx.new_block();
    BlockId exit_block = ctx.new_block();

    // switch終端命令
    ctx.set_terminator(
        MirTerminator::switch_int(MirOperand::copy(MirPlace{discriminant}), cases, default_block));

    // 各caseをlowering（else/default以外）
    for (size_t i = 0; i < switch_stmt.cases.size(); ++i) {
        // else/defaultケース（patternがnull）はスキップ
        if (!switch_stmt.cases[i].pattern) {
            continue;
        }
        ctx.switch_to_block(case_blocks[i]);
        for (const auto& stmt : switch_stmt.cases[i].stmts) {
            lower_statement(*stmt, ctx);
        }
        if (!ctx.get_current_block()->terminator) {
            ctx.set_terminator(MirTerminator::goto_block(exit_block));
        }
    }

    // defaultをlowering
    ctx.switch_to_block(default_block);
    // else句（patternがnullのcase）を探して処理
    for (const auto& case_item : switch_stmt.cases) {
        if (!case_item.pattern) {  // else/defaultケース
            for (const auto& stmt : case_item.stmts) {
                lower_statement(*stmt, ctx);
            }
            break;
        }
    }
    if (!ctx.get_current_block()->terminator) {
        ctx.set_terminator(MirTerminator::goto_block(exit_block));
    }

    // 出口ブロック
    ctx.switch_to_block(exit_block);
}

// block文のlowering
void StmtLowering::lower_block(const hir::HirBlock& block, LoweringContext& ctx) {
    // 新しいスコープを開始
    ctx.push_scope();

    // ブロック内の各文をlowering
    for (const auto& stmt : block.stmts) {
        lower_statement(*stmt, ctx);
    }

    // スコープ終了時にdefer文を実行（逆順）
    auto defers = ctx.get_defer_stmts();
    for (const auto* defer_stmt : defers) {
        lower_statement(*defer_stmt, ctx);
    }

    // スコープ終了時にデストラクタを呼び出し
    emit_scope_destructors(ctx);

    // スコープを終了
    ctx.pop_scope();
}

// defer文のlowering
void StmtLowering::lower_defer(const hir::HirDefer& defer_stmt, LoweringContext& ctx) {
    // defer文を現在のスコープに登録
    // defer文は、スコープ終了時に逆順で実行される
    if (defer_stmt.body) {
        ctx.add_defer(defer_stmt.body.get());
    }
}

// スコープ終了時のデストラクタ呼び出しを生成
void StmtLowering::emit_scope_destructors(LoweringContext& ctx) {
    auto destructor_vars = ctx.get_current_scope_destructor_vars();
    for (const auto& [local_id, type_name] : destructor_vars) {
        std::string dtor_name = type_name + "__dtor";

        // デストラクタ呼び出しを生成
        std::vector<MirOperandPtr> args;
        args.push_back(MirOperand::copy(MirPlace{local_id}));

        BlockId success_block = ctx.new_block();

        auto func_operand = MirOperand::function_ref(dtor_name);
        auto call_term = std::make_unique<MirTerminator>();
        call_term->kind = MirTerminator::Call;
        call_term->data = MirTerminator::CallData{std::move(func_operand),
                                                  std::move(args),
                                                  std::nullopt,  // void戻り値
                                                  success_block,
                                                  std::nullopt,
                                                  "",
                                                  "",
                                                  false};
        ctx.set_terminator(std::move(call_term));
        ctx.switch_to_block(success_block);
    }
}

}  // namespace cm::mir