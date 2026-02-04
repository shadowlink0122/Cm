// expr_lowering_ops.cpp - 演算式のlowering
// lower_binary, lower_unary

#include "../../common/debug.hpp"
#include "expr.hpp"

#include <functional>

namespace cm::mir {

LocalId ExprLowering::lower_binary(const hir::HirBinary& bin, LoweringContext& ctx) {
    // 代入演算の処理
    if (bin.op == hir::HirBinaryOp::Assign) {
        // 右辺を先に評価
        LocalId rhs_value = lower_expression(*bin.rhs, ctx);

        // 左辺値のMirPlaceを構築するヘルパー関数
        // 複雑な左辺値（c.values[0], points[0].x など）を再帰的に処理
        std::function<bool(const hir::HirExpr*, MirPlace&, hir::TypePtr&)> build_lvalue_place;
        build_lvalue_place = [&](const hir::HirExpr* expr, MirPlace& place,
                                 hir::TypePtr& current_type) -> bool {
            if (auto* var_ref = std::get_if<std::unique_ptr<hir::HirVarRef>>(&expr->kind)) {
                // ベース変数
                auto var_id = ctx.resolve_variable((*var_ref)->name);
                if (var_id) {
                    place.local = *var_id;
                    if (*var_id < ctx.func->locals.size()) {
                        current_type = ctx.func->locals[*var_id].type;
                    }
                    return true;
                }
                return false;
            } else if (auto* member = std::get_if<std::unique_ptr<hir::HirMember>>(&expr->kind)) {
                // メンバーアクセス: object.member
                hir::TypePtr inner_type;
                if (!build_lvalue_place((*member)->object.get(), place, inner_type)) {
                    return false;
                }

                // ポインタ型の場合、デリファレンスを追加
                if (inner_type && inner_type->kind == hir::TypeKind::Pointer) {
                    place.projections.push_back(PlaceProjection::deref());
                    inner_type = inner_type->element_type;
                }

                // フィールドプロジェクションを追加
                std::string field_name = (*member)->member;
                if (inner_type && inner_type->kind == hir::TypeKind::Struct) {
                    auto field_idx = ctx.get_field_index(inner_type->name, field_name);
                    if (field_idx) {
                        place.projections.push_back(PlaceProjection::field(*field_idx));

                        // 次の型を取得
                        // ジェネリック構造体の場合はベース構造体名で検索し、
                        // フィールド型がジェネリックパラメータなら置換する
                        std::string lookup_name = inner_type->name;
                        if (ctx.struct_defs && ctx.struct_defs->count(lookup_name)) {
                            const auto* struct_def = ctx.struct_defs->at(lookup_name);
                            if (*field_idx < struct_def->fields.size()) {
                                hir::TypePtr field_type = struct_def->fields[*field_idx].type;

                                // フィールド型がジェネリックパラメータの場合、type_argsから置換
                                // 例: Node<Item>のfield "data: T" → T=Item
                                if (field_type && field_type->kind == hir::TypeKind::Generic &&
                                    !inner_type->type_args.empty()) {
                                    // ジェネリックパラメータ名を型引数にマッピング
                                    for (size_t i = 0; i < struct_def->generic_params.size() &&
                                                       i < inner_type->type_args.size();
                                         ++i) {
                                        if (struct_def->generic_params[i].name ==
                                            field_type->name) {
                                            field_type = inner_type->type_args[i];
                                            break;
                                        }
                                    }
                                }
                                current_type = field_type;
                            }
                        }
                        return true;
                    }
                }
                return false;
            } else if (auto* index = std::get_if<std::unique_ptr<hir::HirIndex>>(&expr->kind)) {
                // インデックスアクセス: object[index] または object[i][j][k]...（多次元）
                hir::TypePtr inner_type;
                if (!build_lvalue_place((*index)->object.get(), place, inner_type)) {
                    return false;
                }

                // 多次元配列最適化: indices が設定されている場合、全インデックスを処理
                if (!(*index)->indices.empty()) {
                    // 多次元: 全インデックスをプロジェクションとして追加
                    for (const auto& idx_expr : (*index)->indices) {
                        LocalId idx = lower_expression(*idx_expr, ctx);
                        place.projections.push_back(PlaceProjection::index(idx));
                        // 型を更新（配列またはポインタの要素型）
                        if (inner_type && inner_type->element_type) {
                            if (inner_type->kind == hir::TypeKind::Array ||
                                inner_type->kind == hir::TypeKind::Pointer) {
                                inner_type = inner_type->element_type;
                            }
                        }
                    }
                    current_type = inner_type;
                } else {
                    // 単一インデックス（後方互換性）
                    LocalId idx = lower_expression(*(*index)->index, ctx);
                    place.projections.push_back(PlaceProjection::index(idx));
                    // 次の型を取得（配列またはポインタの要素型）
                    if (inner_type && inner_type->element_type) {
                        if (inner_type->kind == hir::TypeKind::Array ||
                            inner_type->kind == hir::TypeKind::Pointer) {
                            current_type = inner_type->element_type;
                        }
                    }
                }
                return true;
            } else if (auto* unary = std::get_if<std::unique_ptr<hir::HirUnary>>(&expr->kind)) {
                // デリファレンス: *ptr
                if ((*unary)->op == hir::HirUnaryOp::Deref) {
                    hir::TypePtr inner_type;
                    if (!build_lvalue_place((*unary)->operand.get(), place, inner_type)) {
                        // 通常のポインタ式の場合
                        LocalId ptr = lower_expression(*(*unary)->operand, ctx);
                        place.local = ptr;
                        place.projections.push_back(PlaceProjection::deref());

                        // 要素型を取得
                        if ((*unary)->operand->type &&
                            (*unary)->operand->type->kind == hir::TypeKind::Pointer &&
                            (*unary)->operand->type->element_type) {
                            current_type = (*unary)->operand->type->element_type;
                        }
                        return true;
                    }

                    // ネストした左辺値の場合
                    place.projections.push_back(PlaceProjection::deref());

                    // 要素型を取得
                    if (inner_type && inner_type->kind == hir::TypeKind::Pointer &&
                        inner_type->element_type) {
                        current_type = inner_type->element_type;
                    }
                    return true;
                }
            }
            return false;
        };

        // 左辺値のMirPlaceを構築
        MirPlace place{0};
        hir::TypePtr current_type;

        if (build_lvalue_place(bin.lhs.get(), place, current_type)) {
            ctx.push_statement(
                MirStatement::assign(place, MirRvalue::use(MirOperand::copy(MirPlace{rhs_value}))));
            return rhs_value;
        }

        // その他の左辺（エラー）は評価済みの右辺値を返す
        return rhs_value;
    }

    // 論理演算 (AND/OR) - 短絡評価を実装
    if (bin.op == hir::HirBinaryOp::And) {
        // AND演算の短絡評価
        // 左辺を評価
        LocalId lhs = lower_expression(*bin.lhs, ctx);

        // 結果を格納する変数
        LocalId result = ctx.new_temp(hir::make_bool());

        // ブロックを作成
        BlockId eval_rhs = ctx.new_block();  // 右辺を評価するブロック
        BlockId skip_rhs = ctx.new_block();  // 右辺をスキップするブロック（結果はfalse）
        BlockId merge = ctx.new_block();  // 結果を統合するブロック

        // 左辺がtrueなら右辺を評価、falseならスキップ
        ctx.set_terminator(
            MirTerminator::switch_int(MirOperand::copy(MirPlace{lhs}), {{1, eval_rhs}}, skip_rhs));

        // 右辺を評価するブロック
        ctx.switch_to_block(eval_rhs);
        LocalId rhs = lower_expression(*bin.rhs, ctx);
        // 結果は右辺の値（左辺は既にtrue）
        ctx.push_statement(MirStatement::assign(MirPlace{result},
                                                MirRvalue::use(MirOperand::copy(MirPlace{rhs}))));
        ctx.set_terminator(MirTerminator::goto_block(merge));

        // 右辺をスキップするブロック（左辺がfalse）
        ctx.switch_to_block(skip_rhs);
        // 結果はfalse
        MirConstant false_const;
        false_const.type = hir::make_bool();
        false_const.value = false;
        ctx.push_statement(MirStatement::assign(MirPlace{result},
                                                MirRvalue::use(MirOperand::constant(false_const))));
        ctx.set_terminator(MirTerminator::goto_block(merge));

        // マージブロック
        ctx.switch_to_block(merge);

        return result;
    }

    if (bin.op == hir::HirBinaryOp::Or) {
        // OR演算の短絡評価
        // 左辺を評価
        LocalId lhs = lower_expression(*bin.lhs, ctx);

        // 結果を格納する変数
        LocalId result = ctx.new_temp(hir::make_bool());

        // ブロックを作成
        BlockId skip_rhs = ctx.new_block();  // 右辺をスキップするブロック（結果はtrue）
        BlockId eval_rhs = ctx.new_block();  // 右辺を評価するブロック
        BlockId merge = ctx.new_block();     // 結果を統合するブロック

        // 左辺がtrueならスキップ、falseなら右辺を評価
        ctx.set_terminator(
            MirTerminator::switch_int(MirOperand::copy(MirPlace{lhs}), {{1, skip_rhs}}, eval_rhs));

        // 右辺をスキップするブロック（左辺がtrue）
        ctx.switch_to_block(skip_rhs);
        // 結果はtrue
        MirConstant true_const;
        true_const.type = hir::make_bool();
        true_const.value = true;
        ctx.push_statement(MirStatement::assign(MirPlace{result},
                                                MirRvalue::use(MirOperand::constant(true_const))));
        ctx.set_terminator(MirTerminator::goto_block(merge));

        // 右辺を評価するブロック
        ctx.switch_to_block(eval_rhs);
        LocalId rhs = lower_expression(*bin.rhs, ctx);
        // 結果は右辺の値（左辺は既にfalse）
        ctx.push_statement(MirStatement::assign(MirPlace{result},
                                                MirRvalue::use(MirOperand::copy(MirPlace{rhs}))));
        ctx.set_terminator(MirTerminator::goto_block(merge));

        // マージブロック
        ctx.switch_to_block(merge);

        return result;
    }

    // 通常の二項演算
    // 左辺と右辺をlowering
    LocalId lhs = lower_expression(*bin.lhs, ctx);
    LocalId rhs = lower_expression(*bin.rhs, ctx);

    // 構造体の比較演算子の特別処理（with による自動実装）
    if (bin.op == hir::HirBinaryOp::Eq || bin.op == hir::HirBinaryOp::Ne) {
        // 左辺が構造体型かチェック
        if (bin.lhs->type && bin.lhs->type->kind == hir::TypeKind::Struct) {
            std::string type_name = bin.lhs->type->name;

            // impl_info で Eq が実装されているかチェック
            auto& current_impl_info = get_impl_info();
            auto type_it = current_impl_info.find(type_name);
            if (type_it != current_impl_info.end()) {
                // 任意のインターフェース（Eq）で op_eq が実装されているかチェック
                std::string op_func_name;
                for (const auto& [iface_name, func_name] : type_it->second) {
                    // Eq インターフェースの実装を探す
                    if (iface_name == "Eq" || func_name.find("__op_eq") != std::string::npos) {
                        op_func_name = type_name + "__op_eq";
                        break;
                    }
                }

                if (!op_func_name.empty()) {
                    // 自動生成された演算子関数を呼び出す
                    // Point__op_eq(self, other) - 両方とも値渡し

                    // 結果用変数
                    LocalId result = ctx.new_temp(hir::make_bool());
                    BlockId success_block = ctx.new_block();

                    // 引数を準備（両方とも値渡し）
                    std::vector<MirOperandPtr> args;
                    args.push_back(MirOperand::copy(MirPlace{lhs}));  // self (値)
                    args.push_back(MirOperand::copy(MirPlace{rhs}));  // other (値)

                    // 関数呼び出し
                    auto func_operand = MirOperand::function_ref(op_func_name);
                    auto call_term = std::make_unique<MirTerminator>();
                    call_term->kind = MirTerminator::Call;
                    call_term->data = MirTerminator::CallData{std::move(func_operand),
                                                              std::move(args),
                                                              MirPlace{result},
                                                              success_block,
                                                              std::nullopt,
                                                              "",
                                                              "",
                                                              false};  // 通常の関数呼び出し
                    ctx.set_terminator(std::move(call_term));
                    ctx.switch_to_block(success_block);

                    // != の場合は結果を反転
                    if (bin.op == hir::HirBinaryOp::Ne) {
                        LocalId neg_result = ctx.new_temp(hir::make_bool());
                        auto unary_rvalue = std::make_unique<MirRvalue>();
                        unary_rvalue->kind = MirRvalue::UnaryOp;
                        unary_rvalue->data = MirRvalue::UnaryOpData{
                            MirUnaryOp::Not, MirOperand::copy(MirPlace{result})};
                        ctx.push_statement(
                            MirStatement::assign(MirPlace{neg_result}, std::move(unary_rvalue)));
                        return neg_result;
                    }

                    return result;
                }
            }
        }
    }

    // 構造体の順序演算子の特別処理（with Ord による自動実装）
    if (bin.op == hir::HirBinaryOp::Lt || bin.op == hir::HirBinaryOp::Le ||
        bin.op == hir::HirBinaryOp::Gt || bin.op == hir::HirBinaryOp::Ge) {
        // 左辺が構造体型かチェック
        if (bin.lhs->type && bin.lhs->type->kind == hir::TypeKind::Struct) {
            std::string type_name = bin.lhs->type->name;

            // impl_info で Ord が実装されているかチェック
            auto& current_impl_info = get_impl_info();
            auto type_it = current_impl_info.find(type_name);
            if (type_it != current_impl_info.end()) {
                // Ord インターフェースで op_lt が実装されているかチェック
                std::string op_func_name;
                for (const auto& [iface_name, func_name] : type_it->second) {
                    if (iface_name == "Ord" || func_name.find("__op_lt") != std::string::npos) {
                        op_func_name = type_name + "__op_lt";
                        break;
                    }
                }

                if (!op_func_name.empty()) {
                    LocalId result = ctx.new_temp(hir::make_bool());
                    BlockId success_block = ctx.new_block();

                    // < と > では引数の順序を変える
                    // <= と >= では結果を反転する
                    std::vector<MirOperandPtr> args;
                    if (bin.op == hir::HirBinaryOp::Lt || bin.op == hir::HirBinaryOp::Le) {
                        // a < b または a <= b: __op_lt(a, b)
                        args.push_back(MirOperand::copy(MirPlace{lhs}));
                        args.push_back(MirOperand::copy(MirPlace{rhs}));
                    } else {
                        // a > b または a >= b: __op_lt(b, a)
                        args.push_back(MirOperand::copy(MirPlace{rhs}));
                        args.push_back(MirOperand::copy(MirPlace{lhs}));
                    }

                    auto func_operand = MirOperand::function_ref(op_func_name);
                    auto call_term = std::make_unique<MirTerminator>();
                    call_term->kind = MirTerminator::Call;
                    call_term->data = MirTerminator::CallData{std::move(func_operand),
                                                              std::move(args),
                                                              MirPlace{result},
                                                              success_block,
                                                              std::nullopt,
                                                              "",
                                                              "",
                                                              false};  // 通常の関数呼び出し
                    ctx.set_terminator(std::move(call_term));
                    ctx.switch_to_block(success_block);

                    // <= と >= は !(b < a) と !(a < b) を計算
                    if (bin.op == hir::HirBinaryOp::Le || bin.op == hir::HirBinaryOp::Ge) {
                        // a <= b は !(b < a) → 結果を反転
                        LocalId neg_result = ctx.new_temp(hir::make_bool());
                        auto unary_rvalue = std::make_unique<MirRvalue>();
                        unary_rvalue->kind = MirRvalue::UnaryOp;
                        unary_rvalue->data = MirRvalue::UnaryOpData{
                            MirUnaryOp::Not, MirOperand::copy(MirPlace{result})};
                        ctx.push_statement(
                            MirStatement::assign(MirPlace{neg_result}, std::move(unary_rvalue)));
                        return neg_result;
                    }

                    return result;
                }
            }
        }
    }

    // 文字列連結の特別処理
    if (bin.op == hir::HirBinaryOp::Add) {
        bool lhs_is_string = bin.lhs->type && bin.lhs->type->kind == hir::TypeKind::String;
        bool rhs_is_string = bin.rhs->type && bin.rhs->type->kind == hir::TypeKind::String;

        // どちらかが文字列型の場合、文字列連結として処理
        if (lhs_is_string || rhs_is_string) {
            std::vector<MirOperandPtr> args;

            // 左辺を文字列に変換（必要な場合）
            if (lhs_is_string) {
                args.push_back(MirOperand::copy(MirPlace{lhs}));
            } else {
                LocalId str_lhs = convert_to_string(lhs, bin.lhs->type, ctx);
                args.push_back(MirOperand::copy(MirPlace{str_lhs}));
            }

            // 右辺を文字列に変換（必要な場合）
            if (rhs_is_string) {
                args.push_back(MirOperand::copy(MirPlace{rhs}));
            } else {
                LocalId str_rhs = convert_to_string(rhs, bin.rhs->type, ctx);
                args.push_back(MirOperand::copy(MirPlace{str_rhs}));
            }

            // 文字列連結
            LocalId result = ctx.new_temp(hir::make_string());
            BlockId concat_success = ctx.new_block();

            auto concat_func_operand = MirOperand::function_ref("cm_string_concat");

            auto concat_call_term = std::make_unique<MirTerminator>();
            concat_call_term->kind = MirTerminator::Call;
            concat_call_term->data = MirTerminator::CallData{std::move(concat_func_operand),
                                                             std::move(args),
                                                             MirPlace{result},
                                                             concat_success,
                                                             std::nullopt,
                                                             "",
                                                             "",
                                                             false};  // 通常の関数呼び出し
            ctx.set_terminator(std::move(concat_call_term));
            ctx.switch_to_block(concat_success);

            return result;
        }
    }

    // MIRの二項演算子に変換
    MirBinaryOp mir_op;
    switch (bin.op) {
        case hir::HirBinaryOp::Add:
            mir_op = MirBinaryOp::Add;
            break;
        case hir::HirBinaryOp::Sub:
            mir_op = MirBinaryOp::Sub;
            break;
        case hir::HirBinaryOp::Mul:
            mir_op = MirBinaryOp::Mul;
            break;
        case hir::HirBinaryOp::Div:
            mir_op = MirBinaryOp::Div;
            break;
        case hir::HirBinaryOp::Mod:
            mir_op = MirBinaryOp::Mod;
            break;
        case hir::HirBinaryOp::BitAnd:
            mir_op = MirBinaryOp::BitAnd;
            break;
        case hir::HirBinaryOp::BitOr:
            mir_op = MirBinaryOp::BitOr;
            break;
        case hir::HirBinaryOp::BitXor:
            mir_op = MirBinaryOp::BitXor;
            break;
        case hir::HirBinaryOp::Shl:
            mir_op = MirBinaryOp::Shl;
            break;
        case hir::HirBinaryOp::Shr:
            mir_op = MirBinaryOp::Shr;
            break;
        case hir::HirBinaryOp::Eq:
            mir_op = MirBinaryOp::Eq;
            break;
        case hir::HirBinaryOp::Ne:
            mir_op = MirBinaryOp::Ne;
            break;
        case hir::HirBinaryOp::Lt:
            mir_op = MirBinaryOp::Lt;
            break;
        case hir::HirBinaryOp::Le:
            mir_op = MirBinaryOp::Le;
            break;
        case hir::HirBinaryOp::Gt:
            mir_op = MirBinaryOp::Gt;
            break;
        case hir::HirBinaryOp::Ge:
            mir_op = MirBinaryOp::Ge;
            break;
        default:
            // 未実装の演算子
            mir_op = MirBinaryOp::Add;  // プレースホルダー
    }

    // 結果型を決定
    // 比較演算子 -> bool
    // 算術演算子 -> 左辺の型（または型昇格）
    hir::TypePtr result_type;
    bool is_comparison =
        (mir_op == MirBinaryOp::Eq || mir_op == MirBinaryOp::Ne || mir_op == MirBinaryOp::Lt ||
         mir_op == MirBinaryOp::Le || mir_op == MirBinaryOp::Gt || mir_op == MirBinaryOp::Ge);

    if (is_comparison) {
        result_type = hir::make_bool();
    } else {
        // 算術演算の型昇格
        // float + double -> double, int + double -> double, etc.
        auto lhs_type = bin.lhs->type;
        auto rhs_type = bin.rhs->type;

        // HIRの型が利用できない、またはエラー型の場合、ローカル変数から型を取得
        // （operator実装内の式など、型チェッカーが型を設定しない場合に対応）
        if ((!lhs_type || lhs_type->is_error()) && lhs < ctx.func->locals.size()) {
            lhs_type = ctx.func->locals[lhs].type;
        }
        if ((!rhs_type || rhs_type->is_error()) && rhs < ctx.func->locals.size()) {
            rhs_type = ctx.func->locals[rhs].type;
        }

        if (lhs_type && rhs_type) {
            // doubleがあればdouble
            if (lhs_type->kind == hir::TypeKind::Double ||
                rhs_type->kind == hir::TypeKind::Double) {
                result_type = hir::make_double();
            }
            // floatがあればfloat
            else if (lhs_type->kind == hir::TypeKind::Float ||
                     rhs_type->kind == hir::TypeKind::Float) {
                result_type = hir::make_float();
            }
            // longがあればlong
            else if (lhs_type->kind == hir::TypeKind::Long ||
                     rhs_type->kind == hir::TypeKind::Long ||
                     lhs_type->kind == hir::TypeKind::ULong ||
                     rhs_type->kind == hir::TypeKind::ULong) {
                result_type = hir::make_long();
            }
            // それ以外は左辺の型を使用
            else {
                result_type = lhs_type;
            }
        } else if (lhs_type) {
            result_type = lhs_type;
        } else if (rhs_type) {
            result_type = rhs_type;
        } else {
            result_type = hir::make_int();
        }
    }

    // 結果用の一時変数
    LocalId result = ctx.new_temp(result_type);

    // BinaryOp Rvalueを作成（ポインタ演算の場合は型情報を含める）
    auto bin_rvalue = std::make_unique<MirRvalue>();
    bin_rvalue->kind = MirRvalue::BinaryOp;
    bin_rvalue->data = MirRvalue::BinaryOpData{mir_op, MirOperand::copy(MirPlace{lhs}),
                                               MirOperand::copy(MirPlace{rhs}), result_type};

    ctx.push_statement(MirStatement::assign(MirPlace{result}, std::move(bin_rvalue)));

    return result;
}

// 単項演算のlowering
LocalId ExprLowering::lower_unary(const hir::HirUnary& unary, LoweringContext& ctx) {
    // インクリメント/デクリメント演算子の処理
    if (unary.op == hir::HirUnaryOp::PreInc || unary.op == hir::HirUnaryOp::PostInc ||
        unary.op == hir::HirUnaryOp::PreDec || unary.op == hir::HirUnaryOp::PostDec) {
        // 変数参照を取得
        if (auto var_ref = std::get_if<std::unique_ptr<hir::HirVarRef>>(&unary.operand->kind)) {
            auto local_opt = ctx.resolve_variable((*var_ref)->name);
            if (local_opt) {
                LocalId var_id = *local_opt;

                // PostInc/PostDecの場合、まず現在の値を保存
                LocalId result = var_id;
                if (unary.op == hir::HirUnaryOp::PostInc || unary.op == hir::HirUnaryOp::PostDec) {
                    result = ctx.new_temp(unary.operand->type);
                    ctx.push_statement(MirStatement::assign(
                        MirPlace{result}, MirRvalue::use(MirOperand::copy(MirPlace{var_id}))));
                }

                // 1を加算または減算
                LocalId one = ctx.new_temp(hir::make_int());
                MirConstant one_const;
                one_const.type = hir::make_int();
                one_const.value = int64_t(1);
                ctx.push_statement(MirStatement::assign(
                    MirPlace{one}, MirRvalue::use(MirOperand::constant(one_const))));

                LocalId new_value = ctx.new_temp(unary.operand->type);
                MirBinaryOp op =
                    (unary.op == hir::HirUnaryOp::PreInc || unary.op == hir::HirUnaryOp::PostInc)
                        ? MirBinaryOp::Add
                        : MirBinaryOp::Sub;

                // BinaryOp Rvalueを作成
                auto bin_rvalue = std::make_unique<MirRvalue>();
                bin_rvalue->kind = MirRvalue::BinaryOp;
                bin_rvalue->data =
                    MirRvalue::BinaryOpData{op, MirOperand::copy(MirPlace{var_id}),
                                            MirOperand::copy(MirPlace{one}), unary.operand->type};

                ctx.push_statement(
                    MirStatement::assign(MirPlace{new_value}, std::move(bin_rvalue)));

                // 変数を更新
                ctx.push_statement(MirStatement::assign(
                    MirPlace{var_id}, MirRvalue::use(MirOperand::copy(MirPlace{new_value}))));

                // PreInc/PreDecの場合は新しい値を返す
                if (unary.op == hir::HirUnaryOp::PreInc || unary.op == hir::HirUnaryOp::PreDec) {
                    return new_value;
                }

                return result;
            }
        }

        // 変数でない場合はエラー（一時的に0を返す）
        LocalId temp = ctx.new_temp(unary.operand->type);
        MirConstant zero_const;
        zero_const.type = unary.operand->type;
        zero_const.value = int64_t(0);
        ctx.push_statement(
            MirStatement::assign(MirPlace{temp}, MirRvalue::use(MirOperand::constant(zero_const))));
        return temp;
    }

    // アドレス取得 (&x)
    if (unary.op == hir::HirUnaryOp::AddrOf) {
        // 変数参照の場合、そのアドレスを取得
        if (auto var_ref = std::get_if<std::unique_ptr<hir::HirVarRef>>(&unary.operand->kind)) {
            // 関数参照の場合、関数ポインタとして処理
            if ((*var_ref)->is_function_ref) {
                // 関数ポインタ型を設定 - operandの型を使用
                hir::TypePtr func_ptr_type = unary.operand->type;
                if (!func_ptr_type) {
                    func_ptr_type = hir::make_function_ptr(hir::make_int(), {});
                }

                LocalId result = ctx.new_temp(func_ptr_type);
                ctx.push_statement(MirStatement::assign(
                    MirPlace{result}, MirRvalue::use(MirOperand::function_ref((*var_ref)->name))));
                return result;
            }

            auto local_opt = ctx.resolve_variable((*var_ref)->name);
            if (local_opt) {
                LocalId var_id = *local_opt;

                // 関数型の場合、アドレスを取得するのではなくそのまま関数ポインタとして扱う
                if (unary.operand->type && unary.operand->type->kind == hir::TypeKind::Function) {
                    // 関数ポインタ型の値をそのまま返す
                    LocalId result = ctx.new_temp(unary.operand->type);
                    ctx.push_statement(MirStatement::assign(
                        MirPlace{result}, MirRvalue::use(MirOperand::copy(MirPlace{var_id}))));
                    return result;
                }

                // ポインタ型を作成
                hir::TypePtr ptr_type = hir::make_pointer(unary.operand->type);
                LocalId result = ctx.new_temp(ptr_type);

                // Ref Rvalueを作成（変数への参照）
                auto ref_rvalue = std::make_unique<MirRvalue>();
                ref_rvalue->kind = MirRvalue::Ref;
                ref_rvalue->data = MirRvalue::RefData{BorrowKind::Mutable, MirPlace{var_id}};

                ctx.push_statement(MirStatement::assign(MirPlace{result}, std::move(ref_rvalue)));
                return result;
            }
        }

        // オペランドが既に関数ポインタ型の場合、そのまま返す
        if (unary.operand->type && unary.operand->type->kind == hir::TypeKind::Function) {
            return lower_expression(*unary.operand, ctx);
        }

        // インデックスアクセスの場合（&arr[i]）
        if (auto index = std::get_if<std::unique_ptr<hir::HirIndex>>(&unary.operand->kind)) {
            // オブジェクトの型を確認
            hir::TypePtr obj_type = (*index)->object ? (*index)->object->type : nullptr;

            // ポインタ型へのインデックスアクセスの場合（&ptr[i] → ptr + i）
            // これは this.data[idx] のようなケースで、dataがポインタ型の場合
            if (obj_type && obj_type->kind == hir::TypeKind::Pointer) {
                // ポインタ値を取得
                LocalId ptr_val = lower_expression(*(*index)->object, ctx);
                // インデックス値を取得
                LocalId idx_val = lower_expression(*(*index)->index, ctx);

                // 結果の型（元のポインタ型と同じ）
                hir::TypePtr result_type = obj_type;
                LocalId result = ctx.new_temp(result_type);

                // ポインタ算術 (ptr + idx) を生成
                auto ptr_op = std::make_unique<MirOperand>();
                ptr_op->kind = MirOperand::Copy;
                ptr_op->data = MirPlace{ptr_val};

                auto idx_op = std::make_unique<MirOperand>();
                idx_op->kind = MirOperand::Copy;
                idx_op->data = MirPlace{idx_val};

                auto add_rvalue = std::make_unique<MirRvalue>();
                add_rvalue->kind = MirRvalue::BinaryOp;
                add_rvalue->data = MirRvalue::BinaryOpData{MirBinaryOp::Add, std::move(ptr_op),
                                                           std::move(idx_op), result_type};

                ctx.push_statement(MirStatement::assign(MirPlace{result}, std::move(add_rvalue)));
                return result;
            }

            // 配列型の場合は従来の処理
            // 配列を取得
            LocalId array;
            if (auto* var_ref =
                    std::get_if<std::unique_ptr<hir::HirVarRef>>(&(*index)->object->kind)) {
                auto var_id = ctx.resolve_variable((*var_ref)->name);
                if (var_id) {
                    array = *var_id;
                } else {
                    array = lower_expression(*(*index)->object, ctx);
                }
            } else {
                array = lower_expression(*(*index)->object, ctx);
            }

            // インデックスを取得
            LocalId idx = lower_expression(*(*index)->index, ctx);

            // ポインタ型を作成
            hir::TypePtr elem_type = hir::make_int();
            if ((*index)->object && (*index)->object->type &&
                (*index)->object->type->kind == hir::TypeKind::Array &&
                (*index)->object->type->element_type) {
                elem_type = (*index)->object->type->element_type;
            }
            hir::TypePtr ptr_type = hir::make_pointer(elem_type);
            LocalId result = ctx.new_temp(ptr_type);

            // プロジェクション付きPlaceへの参照
            MirPlace place{array};
            place.projections.push_back(PlaceProjection::index(idx));

            auto ref_rvalue = std::make_unique<MirRvalue>();
            ref_rvalue->kind = MirRvalue::Ref;
            ref_rvalue->data = MirRvalue::RefData{BorrowKind::Mutable, place};

            ctx.push_statement(MirStatement::assign(MirPlace{result}, std::move(ref_rvalue)));
            return result;
        }

        // メンバーアクセスの場合（&obj.field）
        if (auto member = std::get_if<std::unique_ptr<hir::HirMember>>(&unary.operand->kind)) {
            // オブジェクトを取得
            LocalId obj;
            if (auto* var_ref =
                    std::get_if<std::unique_ptr<hir::HirVarRef>>(&(*member)->object->kind)) {
                auto var_id = ctx.resolve_variable((*var_ref)->name);
                if (var_id) {
                    obj = *var_id;
                } else {
                    obj = lower_expression(*(*member)->object, ctx);
                }
            } else {
                obj = lower_expression(*(*member)->object, ctx);
            }

            // フィールドインデックスを取得
            hir::TypePtr obj_type = (*member)->object->type;
            if (!obj_type || obj_type->kind != hir::TypeKind::Struct) {
                // フォールバック
                LocalId operand = lower_expression(*unary.operand, ctx);
                hir::TypePtr ptr_type = hir::make_pointer(unary.operand->type);
                LocalId result = ctx.new_temp(ptr_type);
                auto ref_rvalue = std::make_unique<MirRvalue>();
                ref_rvalue->kind = MirRvalue::Ref;
                ref_rvalue->data = MirRvalue::RefData{BorrowKind::Mutable, MirPlace{operand}};
                ctx.push_statement(MirStatement::assign(MirPlace{result}, std::move(ref_rvalue)));
                return result;
            }

            auto field_idx = ctx.get_field_index(obj_type->name, (*member)->member);
            if (!field_idx) {
                // フォールバック
                LocalId operand = lower_expression(*unary.operand, ctx);
                hir::TypePtr ptr_type = hir::make_pointer(unary.operand->type);
                LocalId result = ctx.new_temp(ptr_type);
                auto ref_rvalue = std::make_unique<MirRvalue>();
                ref_rvalue->kind = MirRvalue::Ref;
                ref_rvalue->data = MirRvalue::RefData{BorrowKind::Mutable, MirPlace{operand}};
                ctx.push_statement(MirStatement::assign(MirPlace{result}, std::move(ref_rvalue)));
                return result;
            }

            // ポインタ型を作成
            hir::TypePtr ptr_type = hir::make_pointer(unary.operand->type);
            LocalId result = ctx.new_temp(ptr_type);

            // プロジェクション付きPlaceへの参照
            MirPlace place{obj};
            place.projections.push_back(PlaceProjection::field(*field_idx));

            auto ref_rvalue = std::make_unique<MirRvalue>();
            ref_rvalue->kind = MirRvalue::Ref;
            ref_rvalue->data = MirRvalue::RefData{BorrowKind::Mutable, place};

            ctx.push_statement(MirStatement::assign(MirPlace{result}, std::move(ref_rvalue)));
            return result;
        }

        // フォールバック：通常の評価
        LocalId operand = lower_expression(*unary.operand, ctx);
        hir::TypePtr ptr_type = hir::make_pointer(unary.operand->type);
        LocalId result = ctx.new_temp(ptr_type);

        auto ref_rvalue = std::make_unique<MirRvalue>();
        ref_rvalue->kind = MirRvalue::Ref;
        ref_rvalue->data = MirRvalue::RefData{BorrowKind::Mutable, MirPlace{operand}};

        ctx.push_statement(MirStatement::assign(MirPlace{result}, std::move(ref_rvalue)));
        return result;
    }

    // デリファレンス (*p)
    if (unary.op == hir::HirUnaryOp::Deref) {
        // ポインタをlowering
        LocalId ptr = lower_expression(*unary.operand, ctx);

        // 要素型を取得
        hir::TypePtr elem_type = hir::make_int();  // デフォルト
        if (unary.operand->type && unary.operand->type->kind == hir::TypeKind::Pointer &&
            unary.operand->type->element_type) {
            elem_type = unary.operand->type->element_type;
        }

        LocalId result = ctx.new_temp(elem_type);

        // Derefプロジェクションを使用
        MirPlace place{ptr};
        place.projections.push_back(PlaceProjection::deref());

        ctx.push_statement(
            MirStatement::assign(MirPlace{result}, MirRvalue::use(MirOperand::copy(place))));
        return result;
    }

    // オペランドをlowering
    LocalId operand = lower_expression(*unary.operand, ctx);

    // MIRの単項演算子に変換
    MirUnaryOp mir_op;
    switch (unary.op) {
        case hir::HirUnaryOp::Neg:
            mir_op = MirUnaryOp::Neg;
            break;
        case hir::HirUnaryOp::Not:
            mir_op = MirUnaryOp::Not;
            break;
        default:
            mir_op = MirUnaryOp::Neg;  // プレースホルダー
    }

    // 結果用の一時変数（NOT の場合は bool、NEG の場合は元の型）
    hir::TypePtr operand_type = unary.operand->type;
    // HIRの型が利用できない、またはエラー型の場合、ローカル変数から型を取得
    if ((!operand_type || operand_type->is_error()) && operand < ctx.func->locals.size()) {
        operand_type = ctx.func->locals[operand].type;
    }
    if (!operand_type || operand_type->is_error()) {
        operand_type = hir::make_int();  // デフォルト
    }

    hir::TypePtr result_type = (unary.op == hir::HirUnaryOp::Not) ? hir::make_bool() : operand_type;
    LocalId result = ctx.new_temp(result_type);
    // UnaryOp Rvalueを作成
    auto unary_rvalue = std::make_unique<MirRvalue>();
    unary_rvalue->kind = MirRvalue::UnaryOp;
    unary_rvalue->data = MirRvalue::UnaryOpData{mir_op, MirOperand::copy(MirPlace{operand})};

    ctx.push_statement(MirStatement::assign(MirPlace{result}, std::move(unary_rvalue)));

    return result;
}

// フォーマット文字列から変数名を抽出し、引数を自動生成

}  // namespace cm::mir
