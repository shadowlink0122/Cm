#include "mir_lowering.hpp"
#include "../../common/debug.hpp"
#include <unordered_map>
#include <optional>

namespace cm::mir {

// 簡易式評価（リテラルと変数参照、二項演算のみ）
LocalId evaluate_simple_expr(const hir::HirExpr* expr,
                             MirFunction* mir_func,
                             BasicBlock* block,
                             std::unordered_map<std::string, LocalId>& variables) {
    static int recursion_depth = 0;
    recursion_depth++;

    debug::log(debug::Stage::Mir, debug::Level::Debug,
               "evaluate_simple_expr: entering (depth=" + std::to_string(recursion_depth) + ")");

    if (recursion_depth > 100) {
        debug::log(debug::Stage::Mir, debug::Level::Error,
                   "evaluate_simple_expr: recursion depth exceeded!");
        recursion_depth--;
        LocalId temp = mir_func->next_local_id++;
        mir_func->locals.emplace_back(temp, "_error", hir::make_int(), true, true);
        recursion_depth--;
    return temp;
    }

    if (!expr) {
        // デフォルト値
        debug::log(debug::Stage::Mir, debug::Level::Debug,
                   "evaluate_simple_expr: null expression, returning default value");
        recursion_depth--;
        LocalId temp = mir_func->next_local_id++;
        mir_func->locals.emplace_back(temp, "_temp", hir::make_int(), true, true);
        auto zero = MirOperand::constant(MirConstant{hir::make_int(), int64_t(0)});
        block->add_statement(MirStatement::assign(
            MirPlace{temp},
            MirRvalue::use(std::move(zero))
        ));
        recursion_depth--;
    return temp;
    }

    // リテラル値
    if (auto lit_ptr = std::get_if<std::unique_ptr<hir::HirLiteral>>(&expr->kind)) {
        auto* lit = lit_ptr->get();
        if (lit) {
            LocalId temp = mir_func->next_local_id++;
            mir_func->locals.emplace_back(temp, "_temp", expr->type, true, true);

            if (lit->value.index() == 1) { // bool値 (std::variant の2番目)
                bool val = std::get<bool>(lit->value);
                debug::log(debug::Stage::Mir, debug::Level::Debug,
                           "evaluate_simple_expr: bool literal value = " + std::string(val ? "true" : "false") +
                           ", storing in local _" + std::to_string(temp));
                // bool値をそのまま保持
                auto constant = MirOperand::constant(MirConstant{expr->type, val});
                block->add_statement(MirStatement::assign(
                    MirPlace{temp},
                    MirRvalue::use(std::move(constant))
                ));
                recursion_depth--;
    return temp;
            } else if (lit->value.index() == 2) { // int値 (std::variant の3番目)
                int64_t val = std::get<int64_t>(lit->value);
                debug::log(debug::Stage::Mir, debug::Level::Debug,
                           "evaluate_simple_expr: integer literal value = " + std::to_string(val));
                auto constant = MirOperand::constant(MirConstant{expr->type, val});
                block->add_statement(MirStatement::assign(
                    MirPlace{temp},
                    MirRvalue::use(std::move(constant))
                ));
                recursion_depth--;
                recursion_depth--;
    return temp;
            } else if (lit->value.index() == 5) { // string値 (std::variant の6番目)
                std::string val = std::get<std::string>(lit->value);
                debug::log(debug::Stage::Mir, debug::Level::Debug,
                           "evaluate_simple_expr: string literal value = \"" + val + "\"");
                auto constant = MirOperand::constant(MirConstant{expr->type, val});
                block->add_statement(MirStatement::assign(
                    MirPlace{temp},
                    MirRvalue::use(std::move(constant))
                ));
                recursion_depth--;
                return temp;
            }
        }
    }

    // 変数参照
    if (auto var_ptr = std::get_if<std::unique_ptr<hir::HirVarRef>>(&expr->kind)) {
        auto* var = var_ptr->get();
        if (var) {
            debug::log(debug::Stage::Mir, debug::Level::Debug,
                       "evaluate_simple_expr: variable reference '" + var->name + "'");
            auto it = variables.find(var->name);
            if (it != variables.end()) {
                debug::log(debug::Stage::Mir, debug::Level::Debug,
                           "evaluate_simple_expr: found variable with local_id = " + std::to_string(it->second));
                recursion_depth--;
                return it->second;
            }
            debug::log(debug::Stage::Mir, debug::Level::Warn,
                       "evaluate_simple_expr: variable '" + var->name + "' not found");
        }
    }

    // 単項演算
    if (auto unary_ptr = std::get_if<std::unique_ptr<hir::HirUnary>>(&expr->kind)) {
        auto* unary = unary_ptr->get();
        if (unary) {
            debug::log(debug::Stage::Mir, debug::Level::Debug,
                       "evaluate_simple_expr: unary operation");

            // オペランドを評価
            LocalId operand = evaluate_simple_expr(unary->operand.get(), mir_func, block, variables);

            // 結果用の一時変数
            LocalId result = mir_func->next_local_id++;
            mir_func->locals.emplace_back(result, "_temp", expr->type, true, true);

            // 単項演算の種類に応じたMIR演算
            if (unary->op == hir::HirUnaryOp::Neg) {
                debug::log(debug::Stage::Mir, debug::Level::Debug,
                           "evaluate_simple_expr: negation operation");
                // 否定演算: 0 - operand
                LocalId zero = mir_func->next_local_id++;
                mir_func->locals.emplace_back(zero, "_zero", expr->type, true, true);
                auto zero_const = MirOperand::constant(MirConstant{expr->type, int64_t(0)});
                block->add_statement(MirStatement::assign(
                    MirPlace{zero},
                    MirRvalue::use(std::move(zero_const))
                ));

                block->add_statement(MirStatement::assign(
                    MirPlace{result},
                    MirRvalue::binary_op(
                        MirBinaryOp::Sub,
                        MirOperand::copy(MirPlace{zero}),
                        MirOperand::copy(MirPlace{operand})
                    )
                ));
            } else {
                // その他の単項演算はサポートしない
                debug::log(debug::Stage::Mir, debug::Level::Warn,
                           "evaluate_simple_expr: unsupported unary operation");
                recursion_depth--;
                return operand;
            }

            recursion_depth--;
            return result;
        }
    }

    // メンバーアクセス
    if (auto member_ptr = std::get_if<std::unique_ptr<hir::HirMember>>(&expr->kind)) {
        auto* member = member_ptr->get();
        if (member && member->object) {
            debug::log(debug::Stage::Mir, debug::Level::Debug,
                       "evaluate_simple_expr: Member access - " + member->member);

            // オブジェクトを評価
            LocalId obj_id = evaluate_simple_expr(member->object.get(), mir_func, block, variables);

            // メンバー名を簡単なハッシュでFieldIdに変換
            FieldId field_id = 0;
            for (char c : member->member) {
                field_id = field_id * 31 + static_cast<uint32_t>(c);
            }
            field_id = field_id % 1000; // 適度なサイズに制限

            // 結果用のローカル変数を作成
            LocalId result = mir_func->next_local_id++;
            mir_func->locals.emplace_back(result, "_member_" + member->member, expr->type, true, true);

            // Projectionを使用してメンバーアクセス
            MirPlace source{obj_id};
            source.projections.push_back(PlaceProjection::field(field_id));

            block->add_statement(MirStatement::assign(
                MirPlace{result},
                MirRvalue::use(MirOperand::copy(source))
            ));

            recursion_depth--;
            return result;
        }
    }

    // 関数呼び出し
    if (auto call_ptr = std::get_if<std::unique_ptr<hir::HirCall>>(&expr->kind)) {
        auto* call = call_ptr->get();
        if (call) {
            // println/print の特別処理は削除
            // 通常の関数呼び出しとして処理する

            // ダミーの戻り値を作成
            LocalId result = mir_func->next_local_id++;
            mir_func->locals.emplace_back(result, "_result", expr->type, true, true);

            recursion_depth--;
            return result;
        }
    }

    // 二項演算
    if (auto bin_ptr = std::get_if<std::unique_ptr<hir::HirBinary>>(&expr->kind)) {
        auto* bin = bin_ptr->get();
        if (bin) {
            debug::log(debug::Stage::Mir, debug::Level::Debug,
                       "evaluate_simple_expr: Binary expression detected, op=" + std::to_string(static_cast<int>(bin->op)));
            debug::log(debug::Stage::Mir, debug::Level::Debug,
                       "evaluate_simple_expr: binary operation");

            // 左辺と右辺を評価
            debug::log(debug::Stage::Mir, debug::Level::Debug,
                       "evaluate_simple_expr: evaluating left operand");
            LocalId lhs = evaluate_simple_expr(bin->lhs.get(), mir_func, block, variables);

            debug::log(debug::Stage::Mir, debug::Level::Debug,
                       "evaluate_simple_expr: evaluating right operand");
            LocalId rhs = evaluate_simple_expr(bin->rhs.get(), mir_func, block, variables);

            // 結果用の一時変数
            LocalId result = mir_func->next_local_id++;
            mir_func->locals.emplace_back(result, "_temp", expr->type, true, true);

            // 代入演算の特別処理
            if (bin->op == hir::HirBinaryOp::Assign) {
                debug::log(debug::Stage::Mir, debug::Level::Debug,
                           "evaluate_simple_expr: assignment operation");

                // 左辺は変数参照またはメンバーアクセスでなければならない
                if (auto var_ptr = std::get_if<std::unique_ptr<hir::HirVarRef>>(&bin->lhs->kind)) {
                    auto* var = var_ptr->get();
                    if (var) {
                        auto it = variables.find(var->name);
                        if (it != variables.end()) {
                            LocalId target_id = it->second;
                            LocalId value_id = evaluate_simple_expr(bin->rhs.get(), mir_func, block, variables);

                            debug::log(debug::Stage::Mir, debug::Level::Debug,
                                       "Assigning value " + std::to_string(value_id) +
                                       " to variable " + std::to_string(target_id));

                            // 代入文を生成
                            block->add_statement(MirStatement::assign(
                                MirPlace{target_id},
                                MirRvalue::use(MirOperand::copy(MirPlace{value_id}))
                            ));

                            // 代入式は代入された値を返す
                            recursion_depth--;
                            return target_id;
                        }
                    }
                } else if (auto member_ptr = std::get_if<std::unique_ptr<hir::HirMember>>(&bin->lhs->kind)) {
                    // 構造体のメンバーへの代入
                    auto* member = member_ptr->get();
                    if (member && member->object) {
                        // オブジェクトを評価
                        LocalId obj_id = evaluate_simple_expr(member->object.get(), mir_func, block, variables);
                        LocalId value_id = evaluate_simple_expr(bin->rhs.get(), mir_func, block, variables);

                        debug::log(debug::Stage::Mir, debug::Level::Debug,
                                   "Assigning to struct member: " + member->member);

                        // メンバー名を簡単なハッシュでFieldIdに変換
                        // TODO: より適切な方法でフィールドマッピングを管理
                        FieldId field_id = 0;
                        for (char c : member->member) {
                            field_id = field_id * 31 + static_cast<uint32_t>(c);
                        }
                        field_id = field_id % 1000; // 適度なサイズに制限

                        // メンバーへの代入をProjectionで表現
                        MirPlace target{obj_id};
                        target.projections.push_back(PlaceProjection::field(field_id));

                        block->add_statement(MirStatement::assign(
                            target,
                            MirRvalue::use(MirOperand::copy(MirPlace{value_id}))
                        ));

                        // 代入式は代入された値を返す
                        recursion_depth--;
                        return value_id;
                    }
                }

                debug::log(debug::Stage::Mir, debug::Level::Warn,
                           "Invalid assignment target");
                recursion_depth--;
                return mir_func->next_local_id - 1;  // ダミー値
            }

            // 演算の種類に応じたMIR演算
            MirBinaryOp mir_op;
            switch (bin->op) {
                case hir::HirBinaryOp::Add:
                    mir_op = MirBinaryOp::Add;
                    debug::log(debug::Stage::Mir, debug::Level::Debug,
                               "evaluate_simple_expr: operation = Add");
                    break;
                case hir::HirBinaryOp::Sub:
                    mir_op = MirBinaryOp::Sub;
                    debug::log(debug::Stage::Mir, debug::Level::Debug,
                               "evaluate_simple_expr: operation = Sub");
                    break;
                case hir::HirBinaryOp::Mul:
                    mir_op = MirBinaryOp::Mul;
                    debug::log(debug::Stage::Mir, debug::Level::Debug,
                               "evaluate_simple_expr: operation = Mul");
                    break;
                case hir::HirBinaryOp::Div:
                    mir_op = MirBinaryOp::Div;
                    debug::log(debug::Stage::Mir, debug::Level::Debug,
                               "evaluate_simple_expr: operation = Div");
                    break;
                case hir::HirBinaryOp::Mod:
                    mir_op = MirBinaryOp::Rem;
                    debug::log(debug::Stage::Mir, debug::Level::Debug,
                               "evaluate_simple_expr: operation = Mod (MIR: Rem)");
                    break;  // 剰余演算子を追加
                case hir::HirBinaryOp::BitAnd: mir_op = MirBinaryOp::BitAnd; break;
                case hir::HirBinaryOp::BitOr: mir_op = MirBinaryOp::BitOr; break;
                case hir::HirBinaryOp::BitXor: mir_op = MirBinaryOp::BitXor; break;
                case hir::HirBinaryOp::Shl: mir_op = MirBinaryOp::Shl; break;
                case hir::HirBinaryOp::Shr: mir_op = MirBinaryOp::Shr; break;
                case hir::HirBinaryOp::Eq:
                    mir_op = MirBinaryOp::Eq;
                    debug::log(debug::Stage::Mir, debug::Level::Debug,
                               "evaluate_simple_expr: operation = Eq (comparison)");
                    break;
                case hir::HirBinaryOp::Ne:
                    mir_op = MirBinaryOp::Ne;
                    debug::log(debug::Stage::Mir, debug::Level::Debug,
                               "evaluate_simple_expr: operation = Ne (comparison)");
                    break;
                case hir::HirBinaryOp::Lt:
                    mir_op = MirBinaryOp::Lt;
                    debug::log(debug::Stage::Mir, debug::Level::Debug,
                               "evaluate_simple_expr: operation = Lt (comparison)");
                    break;
                case hir::HirBinaryOp::Le:
                    mir_op = MirBinaryOp::Le;
                    debug::log(debug::Stage::Mir, debug::Level::Debug,
                               "evaluate_simple_expr: operation = Le (comparison)");
                    break;
                case hir::HirBinaryOp::Gt:
                    mir_op = MirBinaryOp::Gt;
                    debug::log(debug::Stage::Mir, debug::Level::Debug,
                               "evaluate_simple_expr: operation = Gt (comparison)");
                    break;
                case hir::HirBinaryOp::Ge:
                    mir_op = MirBinaryOp::Ge;
                    debug::log(debug::Stage::Mir, debug::Level::Debug,
                               "evaluate_simple_expr: operation = Ge (comparison)");
                    break;
                default:
                    debug::log(debug::Stage::Mir, debug::Level::Warn,
                               "Unknown binary operation, defaulting to Add");
                    mir_op = MirBinaryOp::Add;
            }

            // 二項演算文を生成
            debug::log(debug::Stage::Mir, debug::Level::Debug,
                       "evaluate_simple_expr: generating binary operation (lhs=" + std::to_string(lhs) +
                       ", rhs=" + std::to_string(rhs) + ", result=" + std::to_string(result) + ")");
            block->add_statement(MirStatement::assign(
                MirPlace{result},
                MirRvalue::binary_op(
                    mir_op,
                    MirOperand::copy(MirPlace{lhs}),
                    MirOperand::copy(MirPlace{rhs})
                )
            ));

            recursion_depth--;
            return result;
        }
    }

    // デフォルト
    LocalId temp = mir_func->next_local_id++;
    mir_func->locals.emplace_back(temp, "_temp", expr ? expr->type : hir::make_int(), true, true);
    auto zero = MirOperand::constant(MirConstant{expr ? expr->type : hir::make_int(), int64_t(0)});
    block->add_statement(MirStatement::assign(
        MirPlace{temp},
        MirRvalue::use(std::move(zero))
    ));
    recursion_depth--;
    return temp;
}

// 前方宣言
void lower_statement(const hir::HirStmt* stmt,
                     MirFunction* mir_func,
                     BasicBlock*& current_block,
                     std::unordered_map<std::string, LocalId>& variables,
                     std::vector<hir::HirStmt*>& defer_statements,
                     std::unique_ptr<BasicBlock>& entry_block,
                     size_t stmt_idx,
                     size_t total_stmts);

// 汎用的な文処理関数
void lower_statement(const hir::HirStmt* stmt,
                     MirFunction* mir_func,
                     BasicBlock*& current_block,
                     std::unordered_map<std::string, LocalId>& variables,
                     std::vector<hir::HirStmt*>& defer_statements,
                     std::unique_ptr<BasicBlock>& entry_block,
                     size_t stmt_idx,
                     size_t total_stmts) {
    if (!stmt) return;

    // 変数宣言
    if (auto let_ptr = std::get_if<std::unique_ptr<hir::HirLet>>(&stmt->kind)) {
        auto* let_stmt = let_ptr->get();
        if (let_stmt) {
            debug::log(debug::Stage::Mir, debug::Level::Debug,
                       "Processing variable declaration: " + let_stmt->name);

            // 新しいローカル変数を作成
            LocalId local_id = mir_func->next_local_id++;
            mir_func->locals.emplace_back(local_id, let_stmt->name, let_stmt->type, !let_stmt->is_const, false);
            variables[let_stmt->name] = local_id;

            // 初期値がある場合
            if (let_stmt->init) {
                // 初期化式が関数呼び出しかチェック
                if (auto call_ptr = std::get_if<std::unique_ptr<hir::HirCall>>(&let_stmt->init->kind)) {
                    auto* call = call_ptr->get();
                    if (call) {
                        // 引数を評価
                        std::vector<MirOperandPtr> mir_args;
                        for (const auto& arg : call->args) {
                            LocalId arg_val = evaluate_simple_expr(arg.get(), mir_func, current_block, variables);
                            mir_args.push_back(MirOperand::copy(MirPlace{arg_val}));
                        }

                        // 新しいブロックを作成
                        BlockId next_block_id = mir_func->next_block_id++;
                        auto next_block = std::make_unique<BasicBlock>(next_block_id);

                        // Call terminatorを設定
                        current_block->set_terminator(MirTerminator::call(
                            call->func_name,
                            std::move(mir_args),
                            MirPlace{local_id},  // 戻り値を変数に格納
                            next_block_id,
                            std::nullopt
                        ));

                        // ブロックを追加して現在のブロックを更新
                        if (current_block == entry_block.get()) {
                            mir_func->basic_blocks.push_back(std::move(entry_block));
                            entry_block = std::move(next_block);
                            current_block = entry_block.get();
                        } else {
                            mir_func->basic_blocks.push_back(std::move(next_block));
                            current_block = mir_func->basic_blocks.back().get();
                        }
                    }
                } else {
                    // その他の初期化式
                    LocalId init_val = evaluate_simple_expr(let_stmt->init.get(), mir_func, current_block, variables);
                    current_block->add_statement(MirStatement::assign(
                        MirPlace{local_id},
                        MirRvalue::use(MirOperand::copy(MirPlace{init_val}))
                    ));
                }
            }
        }
    }
    // 代入文
    else if (auto assign_ptr = std::get_if<std::unique_ptr<hir::HirAssign>>(&stmt->kind)) {
        auto* assign_stmt = assign_ptr->get();
        if (assign_stmt && assign_stmt->value) {
            debug::log(debug::Stage::Mir, debug::Level::Debug,
                       "Processing assignment to: " + assign_stmt->target);

            // 変数を探す
            auto it = variables.find(assign_stmt->target);
            if (it != variables.end()) {
                LocalId target_id = it->second;

                // 右辺値の評価
                LocalId value_id = evaluate_simple_expr(assign_stmt->value.get(), mir_func, current_block, variables);

                // 代入命令を生成
                current_block->add_statement(MirStatement::assign(
                    MirPlace{target_id},
                    MirRvalue::use(MirOperand::copy(MirPlace{value_id}))
                ));

                debug::log(debug::Stage::Mir, debug::Level::Debug,
                           "Assignment: local " + std::to_string(target_id) +
                           " = local " + std::to_string(value_id));
            } else {
                debug::log(debug::Stage::Mir, debug::Level::Warn,
                           "Variable not found for assignment: " + assign_stmt->target);
            }
        }
    }
    // 式文
    else if (auto expr_ptr = std::get_if<std::unique_ptr<hir::HirExprStmt>>(&stmt->kind)) {
        auto* expr_stmt = expr_ptr->get();
        if (expr_stmt && expr_stmt->expr) {
            // 代入式の処理（二項演算として表現される）
            if (auto binary_ptr = std::get_if<std::unique_ptr<hir::HirBinary>>(&expr_stmt->expr->kind)) {
                auto* binary = binary_ptr->get();
                if (binary && binary->op == hir::HirBinaryOp::Assign) {
                    debug::log(debug::Stage::Mir, debug::Level::Debug,
                               "Processing assignment expression in expression statement");

                    // 左辺値（変数）の解決
                    if (binary->lhs) {
                        if (auto var_ptr = std::get_if<std::unique_ptr<hir::HirVarRef>>(&binary->lhs->kind)) {
                            auto* var = var_ptr->get();
                            if (var) {
                                auto it = variables.find(var->name);
                                if (it != variables.end()) {
                                    LocalId target_id = it->second;

                                    // 右辺値の評価
                                    if (binary->rhs) {
                                        LocalId value_id = evaluate_simple_expr(binary->rhs.get(), mir_func, current_block, variables);

                                        // 代入命令を生成
                                        current_block->add_statement(MirStatement::assign(
                                            MirPlace{target_id},
                                            MirRvalue::use(MirOperand::copy(MirPlace{value_id}))
                                        ));

                                        debug::log(debug::Stage::Mir, debug::Level::Debug,
                                                   "Assignment expression: local " + std::to_string(target_id) +
                                                   " = local " + std::to_string(value_id));
                                    }
                                } else {
                                    debug::log(debug::Stage::Mir, debug::Level::Warn,
                                               "Variable not found in assignment expression: " + var->name);
                                }
                            }
                        }
                    }
                }
            }
            // 関数呼び出しの処理
            else if (auto call_ptr = std::get_if<std::unique_ptr<hir::HirCall>>(&expr_stmt->expr->kind)) {
                auto* call = call_ptr->get();
                if (call) {
                    // printlnの場合
                    if (call->func_name == "println" && !call->args.empty()) {
                        debug::log(debug::Stage::Mir, debug::Level::Debug,
                                   "Processing println call");

                        LocalId arg_val = evaluate_simple_expr(call->args[0].get(), mir_func, current_block, variables);

                        // 新しいブロックを作成
                        BlockId next_block_id = mir_func->next_block_id++;
                        auto next_block = std::make_unique<BasicBlock>(next_block_id);

                        // 適切な関数名を選択
                        std::string runtime_func;
                        if (call->args[0]->type && call->args[0]->type->kind == hir::TypeKind::Bool) {
                            runtime_func = "cm_println_bool";
                        } else {
                            runtime_func = "cm_println_int";
                        }

                        std::vector<MirOperandPtr> args;
                        args.push_back(MirOperand::copy(MirPlace{arg_val}));
                        current_block->set_terminator(MirTerminator::call(
                            runtime_func,
                            std::move(args),
                            std::nullopt,
                            next_block_id,
                            std::nullopt
                        ));

                        // ブロックを追加して現在のブロックを更新
                        if (current_block == entry_block.get()) {
                            mir_func->basic_blocks.push_back(std::move(entry_block));
                            entry_block = std::move(next_block);
                            current_block = entry_block.get();
                        } else {
                            mir_func->basic_blocks.push_back(std::move(next_block));
                            current_block = mir_func->basic_blocks.back().get();
                        }
                    } else {
                        // その他の関数呼び出し
                        std::vector<MirOperandPtr> mir_args;
                        for (const auto& arg : call->args) {
                            LocalId arg_val = evaluate_simple_expr(arg.get(), mir_func, current_block, variables);
                            mir_args.push_back(MirOperand::copy(MirPlace{arg_val}));
                        }

                        // 戻り値用のローカル変数
                        LocalId result_id = mir_func->next_local_id++;
                        mir_func->locals.emplace_back(result_id, "_func_result", expr_stmt->expr->type, true, true);

                        // 新しいブロックを作成
                        BlockId next_block_id = mir_func->next_block_id++;
                        auto next_block = std::make_unique<BasicBlock>(next_block_id);

                        // Call terminatorを設定
                        current_block->set_terminator(MirTerminator::call(
                            call->func_name,
                            std::move(mir_args),
                            MirPlace{result_id},
                            next_block_id,
                            std::nullopt
                        ));

                        // ブロックを追加して現在のブロックを更新
                        if (current_block == entry_block.get()) {
                            mir_func->basic_blocks.push_back(std::move(entry_block));
                            entry_block = std::move(next_block);
                            current_block = entry_block.get();
                        } else {
                            mir_func->basic_blocks.push_back(std::move(next_block));
                            current_block = mir_func->basic_blocks.back().get();
                        }
                    }
                }
            }
        }
    }
    // if文
    else if (auto if_ptr = std::get_if<std::unique_ptr<hir::HirIf>>(&stmt->kind)) {
        auto* if_stmt = if_ptr->get();
        if (if_stmt) {
            debug::log(debug::Stage::Mir, debug::Level::Debug,
                       "Processing if statement");

            // ブロックを作成
            BlockId then_block_id = mir_func->next_block_id++;
            BlockId else_block_id = mir_func->next_block_id++;
            BlockId end_block_id = mir_func->next_block_id++;

            auto then_block = std::make_unique<BasicBlock>(then_block_id);
            auto else_block = std::make_unique<BasicBlock>(else_block_id);
            auto end_block = std::make_unique<BasicBlock>(end_block_id);

            // 条件式を評価
            LocalId cond_value = evaluate_simple_expr(if_stmt->cond.get(), mir_func, current_block, variables);

            // 条件分岐
            current_block->set_terminator(MirTerminator::switch_int(
                MirOperand::copy(MirPlace{cond_value}),
                {{1, then_block_id}},
                else_block_id
            ));

            // thenブロックの処理
            BasicBlock* then_current = then_block.get();
            for (const auto& then_stmt : if_stmt->then_block) {
                if (then_stmt) {
                    // 再帰的に文を処理
                    lower_statement(then_stmt.get(), mir_func, then_current, variables,
                                    defer_statements, then_block, 0, 1);
                }
            }
            // thenブロックから終了ブロックへ
            if (!then_current->terminator) {
                then_current->set_terminator(MirTerminator::goto_block(end_block_id));
            }
            mir_func->basic_blocks.push_back(std::move(then_block));

            // elseブロックの処理
            BasicBlock* else_current = else_block.get();
            if (!if_stmt->else_block.empty()) {
                for (const auto& else_stmt : if_stmt->else_block) {
                    if (else_stmt) {
                        // 再帰的に文を処理
                        lower_statement(else_stmt.get(), mir_func, else_current, variables,
                                        defer_statements, else_block, 0, 1);
                    }
                }
            }
            // elseブロックから終了ブロックへ
            if (!else_current->terminator) {
                else_current->set_terminator(MirTerminator::goto_block(end_block_id));
            }
            mir_func->basic_blocks.push_back(std::move(else_block));

            // 現在のentry_blockを保存
            if (current_block == entry_block.get()) {
                mir_func->basic_blocks.push_back(std::move(entry_block));
                entry_block = std::move(end_block);
                current_block = entry_block.get();
            } else {
                mir_func->basic_blocks.push_back(std::move(end_block));
                // 新しいブロックを作成
                BlockId next_block_id = mir_func->next_block_id++;
                entry_block = std::make_unique<BasicBlock>(next_block_id);
                current_block = entry_block.get();
                // 前のend_blockから新しいブロックへ接続
                auto& last_block = mir_func->basic_blocks.back();
                if (last_block && !last_block->terminator) {
                    last_block->set_terminator(MirTerminator::goto_block(next_block_id));
                }
            }
        }
    }
    // while文
    else if (auto while_ptr = std::get_if<std::unique_ptr<hir::HirWhile>>(&stmt->kind)) {
        auto* while_stmt = while_ptr->get();
        if (while_stmt) {
            debug::log(debug::Stage::Mir, debug::Level::Debug,
                       "Processing while statement");

            BlockId loop_header_id = mir_func->next_block_id++;
            BlockId loop_body_id = mir_func->next_block_id++;
            BlockId loop_exit_id = mir_func->next_block_id++;

            debug::log(debug::Stage::Mir, debug::Level::Debug,
                       "[WHILE] Block IDs - header:" + std::to_string(loop_header_id) +
                       " body:" + std::to_string(loop_body_id) +
                       " exit:" + std::to_string(loop_exit_id));

            auto loop_header = std::make_unique<BasicBlock>(loop_header_id);
            auto loop_body = std::make_unique<BasicBlock>(loop_body_id);
            auto loop_exit = std::make_unique<BasicBlock>(loop_exit_id);

            // 現在のブロックからループヘッダへ
            current_block->set_terminator(MirTerminator::goto_block(loop_header_id));
            debug::log(debug::Stage::Mir, debug::Level::Debug,
                       "[WHILE] Entry goto header bb" + std::to_string(loop_header_id));

            // ループヘッダで条件評価
            debug::log(debug::Stage::Mir, debug::Level::Debug,
                       "[WHILE] Evaluating condition in header");
            LocalId cond_value = evaluate_simple_expr(while_stmt->cond.get(), mir_func, loop_header.get(), variables);
            loop_header->set_terminator(MirTerminator::switch_int(
                MirOperand::copy(MirPlace{cond_value}),
                {{1, loop_body_id}},
                loop_exit_id
            ));
            debug::log(debug::Stage::Mir, debug::Level::Debug,
                       "[WHILE] Header terminator - true->bb" + std::to_string(loop_body_id) +
                       " false->bb" + std::to_string(loop_exit_id));

            // ループ本体の処理
            debug::log(debug::Stage::Mir, debug::Level::Debug,
                       "[WHILE] Processing body statements");
            BasicBlock* body_current = loop_body.get();
            for (const auto& body_stmt : while_stmt->body) {
                if (body_stmt) {
                    // 再帰的に文を処理
                    lower_statement(body_stmt.get(), mir_func, body_current, variables,
                                    defer_statements, loop_body, 0, 1);
                }
            }
            // ループ本体からループヘッダへ戻る
            if (!body_current->terminator) {
                body_current->set_terminator(MirTerminator::goto_block(loop_header_id));
                debug::log(debug::Stage::Mir, debug::Level::Debug,
                           "[WHILE] Body returns to header bb" + std::to_string(loop_header_id));
            }

            // ブロックを追加
            if (current_block == entry_block.get()) {
                mir_func->basic_blocks.push_back(std::move(entry_block));
            }
            mir_func->basic_blocks.push_back(std::move(loop_header));
            mir_func->basic_blocks.push_back(std::move(loop_body));

            // ループ終了ブロックを新しいentry_blockとする
            entry_block = std::move(loop_exit);
            current_block = entry_block.get();
        }
    }
    // for文
    else if (auto for_ptr = std::get_if<std::unique_ptr<hir::HirFor>>(&stmt->kind)) {
        auto* for_stmt = for_ptr->get();
        if (for_stmt) {
            debug::log(debug::Stage::Mir, debug::Level::Debug,
                       "Processing for statement");

            debug::log(debug::Stage::Mir, debug::Level::Debug,
                       "[FOR] Creating blocks - header, body, update, exit");

            // 初期化部の処理
            if (for_stmt->init) {
                debug::log(debug::Stage::Mir, debug::Level::Debug,
                           "[FOR] Processing init statement");
                lower_statement(for_stmt->init.get(), mir_func, current_block, variables,
                                defer_statements, entry_block, 0, 1);
            }

            BlockId loop_header_id = mir_func->next_block_id++;
            BlockId loop_body_id = mir_func->next_block_id++;
            BlockId loop_update_id = mir_func->next_block_id++;
            BlockId loop_exit_id = mir_func->next_block_id++;

            debug::log(debug::Stage::Mir, debug::Level::Debug,
                       "[FOR] Block IDs - header:" + std::to_string(loop_header_id) +
                       " body:" + std::to_string(loop_body_id) +
                       " update:" + std::to_string(loop_update_id) +
                       " exit:" + std::to_string(loop_exit_id));

            auto loop_header = std::make_unique<BasicBlock>(loop_header_id);
            auto loop_body = std::make_unique<BasicBlock>(loop_body_id);
            auto loop_update = std::make_unique<BasicBlock>(loop_update_id);
            auto loop_exit = std::make_unique<BasicBlock>(loop_exit_id);

            // 現在のブロックからループヘッダへ
            current_block->set_terminator(MirTerminator::goto_block(loop_header_id));

            // ループヘッダで条件評価
            if (for_stmt->cond) {
                debug::log(debug::Stage::Mir, debug::Level::Debug,
                           "[FOR] Evaluating condition in header block");
                LocalId cond_value = evaluate_simple_expr(for_stmt->cond.get(), mir_func, loop_header.get(), variables);
                loop_header->set_terminator(MirTerminator::switch_int(
                    MirOperand::copy(MirPlace{cond_value}),
                    {{1, loop_body_id}},
                    loop_exit_id
                ));
                debug::log(debug::Stage::Mir, debug::Level::Debug,
                           "[FOR] Header terminator set - true->bb" + std::to_string(loop_body_id) +
                           " false->bb" + std::to_string(loop_exit_id));
            } else {
                // 条件がない場合は常にループ本体へ
                loop_header->set_terminator(MirTerminator::goto_block(loop_body_id));
                debug::log(debug::Stage::Mir, debug::Level::Debug,
                           "[FOR] No condition, always goto body bb" + std::to_string(loop_body_id));
            }

            // ループ本体の処理
            debug::log(debug::Stage::Mir, debug::Level::Debug,
                       "[FOR] Processing loop body statements");
            BasicBlock* body_current = loop_body.get();
            for (const auto& body_stmt : for_stmt->body) {
                if (body_stmt) {
                    lower_statement(body_stmt.get(), mir_func, body_current, variables,
                                    defer_statements, loop_body, 0, 1);
                }
            }
            // ループ本体から更新部へ
            if (!body_current->terminator) {
                body_current->set_terminator(MirTerminator::goto_block(loop_update_id));
                debug::log(debug::Stage::Mir, debug::Level::Debug,
                           "[FOR] Body exits to update bb" + std::to_string(loop_update_id));
            }

            // 更新部の処理
            BasicBlock* update_current = loop_update.get();
            if (for_stmt->update) {
                debug::log(debug::Stage::Mir, debug::Level::Debug,
                           "[FOR] Processing update expression");

                // 更新式が代入式の場合は特別に処理
                if (auto binary_ptr = std::get_if<std::unique_ptr<hir::HirBinary>>(&for_stmt->update->kind)) {
                    auto* binary = binary_ptr->get();
                    if (binary && binary->op == hir::HirBinaryOp::Assign) {
                        debug::log(debug::Stage::Mir, debug::Level::Debug,
                                   "[FOR] Update is an assignment expression");
                        // 左辺値（変数）の解決
                        if (binary->lhs) {
                            if (auto var_ptr = std::get_if<std::unique_ptr<hir::HirVarRef>>(&binary->lhs->kind)) {
                                auto* var = var_ptr->get();
                                if (var) {
                                    auto it = variables.find(var->name);
                                    if (it != variables.end()) {
                                        LocalId target_id = it->second;
                                        // 右辺値の評価
                                        if (binary->rhs) {
                                            LocalId value_id = evaluate_simple_expr(binary->rhs.get(), mir_func, update_current, variables);
                                            // 代入命令を生成
                                            update_current->add_statement(MirStatement::assign(
                                                MirPlace{target_id},
                                                MirRvalue::use(MirOperand::copy(MirPlace{value_id}))
                                            ));
                                            debug::log(debug::Stage::Mir, debug::Level::Debug,
                                                       "[FOR] Update assignment: local " + std::to_string(target_id) +
                                                       " = local " + std::to_string(value_id));
                                        }
                                    }
                                }
                            }
                        }
                    } else {
                        // 代入以外の式の場合は単に評価
                        evaluate_simple_expr(for_stmt->update.get(), mir_func, update_current, variables);
                    }
                } else {
                    // その他の式の場合は単に評価
                    evaluate_simple_expr(for_stmt->update.get(), mir_func, update_current, variables);
                }
            }
            // 更新部からループヘッダへ戻る
            if (!update_current->terminator) {
                update_current->set_terminator(MirTerminator::goto_block(loop_header_id));
                debug::log(debug::Stage::Mir, debug::Level::Debug,
                           "[FOR] Update returns to header bb" + std::to_string(loop_header_id));
            }

            // ブロックを追加
            if (current_block == entry_block.get()) {
                mir_func->basic_blocks.push_back(std::move(entry_block));
            }
            mir_func->basic_blocks.push_back(std::move(loop_header));
            mir_func->basic_blocks.push_back(std::move(loop_body));
            mir_func->basic_blocks.push_back(std::move(loop_update));

            // ループ終了ブロックを新しいentry_blockとする
            entry_block = std::move(loop_exit);
            current_block = entry_block.get();
        }
    }
    // defer文
    else if (auto defer_ptr = std::get_if<std::unique_ptr<hir::HirDefer>>(&stmt->kind)) {
        auto* defer_stmt = defer_ptr->get();
        if (defer_stmt && defer_stmt->body) {
            // defer文を保存（後で逆順で実行）
            defer_statements.push_back(defer_stmt->body.get());
        }
    }
    // return文
    else if (auto ret_ptr = std::get_if<std::unique_ptr<hir::HirReturn>>(&stmt->kind)) {
        auto* ret_stmt = ret_ptr->get();
        if (ret_stmt && ret_stmt->value) {
            // 式を評価
            LocalId result = evaluate_simple_expr(ret_stmt->value.get(), mir_func, current_block, variables);
            current_block->add_statement(MirStatement::assign(
                MirPlace{0},
                MirRvalue::use(MirOperand::copy(MirPlace{result}))
            ));
        } else {
            // 値がないreturn文の場合、デフォルト値を設定
            auto default_value = MirOperand::constant(MirConstant{hir::make_int(), int64_t(0)});
            current_block->add_statement(MirStatement::assign(
                MirPlace{0},
                MirRvalue::use(std::move(default_value))
            ));
        }

        // defer文を逆順で実行
        for (auto it = defer_statements.rbegin(); it != defer_statements.rend(); ++it) {
            hir::HirStmt* defer_body = *it;
            if (defer_body) {
                // defer文の内容を実行
                lower_statement(defer_body, mir_func, current_block, variables,
                                defer_statements, entry_block, 0, 1);
            }
        }

        // return文の後はreturn terminatorを設定
        current_block->set_terminator(MirTerminator::return_value());

        // 後続のステートメントがある場合は新しいブロックを作成
        if (stmt_idx + 1 < total_stmts) {
            mir_func->basic_blocks.push_back(std::move(entry_block));
            BlockId new_block_id = mir_func->next_block_id++;
            entry_block = std::make_unique<BasicBlock>(new_block_id);
            current_block = entry_block.get();
        }
    }
}

// 最小限の関数lowering実装
std::unique_ptr<MirFunction> MirLowering::lower_function(const hir::HirFunction& func) {
    debug::log(debug::Stage::Mir, debug::Level::Info,
               "Lowering function (minimal): " + func.name);

    auto mir_func = std::make_unique<MirFunction>();
    mir_func->name = func.name;

    // 戻り値用のローカル変数
    mir_func->return_local = 0;
    mir_func->next_local_id = 1;
    mir_func->locals.emplace_back(0, "@return", func.return_type, true, false);

    // エントリーブロックを作成
    mir_func->entry_block = 0;
    mir_func->next_block_id = 1;

    auto entry_block = std::make_unique<BasicBlock>(0);
    BasicBlock* current_block = entry_block.get();  // 現在処理中のブロックを追跡

    // 変数マップ
    std::unordered_map<std::string, LocalId> variables;

    // 関数パラメータをローカル変数として登録
    for (const auto& param : func.params) {
        LocalId param_id = mir_func->next_local_id++;
        mir_func->locals.emplace_back(param_id, param.name, param.type, false, false);
        mir_func->arg_locals.push_back(param_id);  // 引数ローカルIDを記録
        variables[param.name] = param_id;

        debug::log(debug::Stage::Mir, debug::Level::Debug,
                   "Registered parameter '" + param.name + "' as local " + std::to_string(param_id) +
                   " (arg_local index " + std::to_string(mir_func->arg_locals.size() - 1) + ")");
    }

    // defer文を追跡（LIFOで実行するため）- ポインタを保持
    std::vector<hir::HirStmt*> defer_statements;

    // 新しい汎用関数を使用して文を処理
    for (size_t stmt_idx = 0; stmt_idx < func.body.size(); ++stmt_idx) {
        const auto& stmt = func.body[stmt_idx];
        if (stmt) {
            // 汎用的な文処理関数を呼び出す
            lower_statement(stmt.get(), mir_func.get(), current_block, variables,
                            defer_statements, entry_block, stmt_idx, func.body.size());

            // return文があったかチェック
            if (auto ret_ptr = std::get_if<std::unique_ptr<hir::HirReturn>>(&stmt->kind)) {
                // すでにlower_statementで処理済み
                break;
            }
        }
    }

    // デフォルト値で戻る（return文がない場合）
    bool has_return = current_block->terminator && current_block->terminator->kind == MirTerminator::Kind::Return;
    if (!has_return) {
        auto default_value = MirOperand::constant(MirConstant{func.return_type, int64_t(0)});
        current_block->add_statement(MirStatement::assign(
            MirPlace{0},
            MirRvalue::use(std::move(default_value))
        ));
    }

    // 関数の最後でdefer文を実行（明示的なreturnがない場合）
    if (!has_return && !defer_statements.empty()) {
        for (auto it = defer_statements.rbegin(); it != defer_statements.rend(); ++it) {
            hir::HirStmt* defer_body = *it;
            if (defer_body) {
                // defer文の内容を実行（主に式文）
                if (auto expr_stmt_ptr = std::get_if<std::unique_ptr<hir::HirExprStmt>>(&defer_body->kind)) {
                    auto* expr_stmt = expr_stmt_ptr->get();
                    if (expr_stmt && expr_stmt->expr) {
                        // 式を評価（副作用のために）
                        evaluate_simple_expr(expr_stmt->expr.get(), mir_func.get(), current_block, variables);
                    }
                }
            }
        }
    }

    // 最後のブロックにreturn terminatorを設定
    if (!current_block->terminator) {
        current_block->set_terminator(MirTerminator::return_value());
    }
    if (current_block == entry_block.get()) {
        mir_func->basic_blocks.push_back(std::move(entry_block));
    }

    // CFGを構築
    mir_func->build_cfg();

    // デバッグ: 生成されたブロックの情報を表示
    debug::log(debug::Stage::Mir, debug::Level::Debug,
               "Generated " + std::to_string(mir_func->basic_blocks.size()) + " basic blocks");
    for (const auto& block : mir_func->basic_blocks) {
        if (block) {
            debug::log(debug::Stage::Mir, debug::Level::Debug,
                       "  Block " + std::to_string(block->id));
        }
    }

    debug::log(debug::Stage::Mir, debug::Level::Info,
               "Successfully lowered function (minimal): " + func.name);

    return mir_func;
}

// impl内のメソッドをlowering（空実装）
void MirLowering::lower_impl(const hir::HirImpl& /* impl */) {
    // 空実装
}

// デストラクタを生成（空実装）
void MirLowering::generate_destructor(const std::string& /* type_name */, LoweringContext& /* ctx */) {
    // 空実装
}

}  // namespace cm::mir