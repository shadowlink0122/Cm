// ============================================================
// 自動実装 - 比較演算子（==/<）の生成と書き換え
// ============================================================

#include "internal/base/debug.hpp"
#include "internal/syntax/ast/typekey.hpp"
#include "lowering.hpp"

#include <algorithm>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cm::mir {

// モノモーフィゼーションされた構造体用のEq演算子を生成
void MirLowering::generate_builtin_eq_operator_for_monomorphized(const MirStruct& st) {
    std::string func_name = ast::typekey::spec_fn_prefix(st.name) + "__op_eq";

    // 既に生成されている場合はスキップ
    for (const auto& func : mir_program.functions) {
        if (func && func->name == func_name)
            return;
    }

    // ネストしたstruct型フィールドの比較関数を先に生成（再帰的自動実装）
    for (const auto& field : st.fields) {
        if (field.type && field.type->kind == hir::TypeKind::Struct) {
            std::string nested_func_name =
                ast::typekey::spec_fn_prefix(field.type->name) + "__op_eq";
            bool exists = false;
            for (const auto& func : mir_program.functions) {
                if (func && func->name == nested_func_name) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                for (const auto& mir_st : mir_program.structs) {
                    if (mir_st && mir_st->name == field.type->name) {
                        generate_builtin_eq_operator_for_monomorphized(*mir_st);
                        break;
                    }
                }
            }
        }
    }

    auto mir_func = std::make_unique<MirFunction>();
    mir_func->name = func_name;

    mir_func->return_local = mir_func->add_local("_0", hir::make_bool(), true, false);

    auto struct_type = hir::make_named(st.name);
    LocalId self_local = mir_func->add_local("self", struct_type, false, true);
    LocalId other_local = mir_func->add_local("other", struct_type, false, true);
    mir_func->arg_locals.push_back(self_local);
    mir_func->arg_locals.push_back(other_local);

    BlockId entry_block = mir_func->add_block();
    auto* block = mir_func->get_block(entry_block);

    if (st.fields.empty()) {
        auto const_true = std::make_unique<MirOperand>();
        const_true->kind = MirOperand::Constant;
        MirConstant c;
        c.value = true;
        c.type = hir::make_bool();
        const_true->data = c;

        block->statements.push_back(MirStatement::assign(MirPlace(mir_func->return_local),
                                                         MirRvalue::use(std::move(const_true))));
        block->terminator = MirTerminator::return_value();
    } else {
        std::vector<LocalId> cmp_results;
        BlockId current_block = entry_block;

        for (size_t i = 0; i < st.fields.size(); ++i) {
            const auto& field = st.fields[i];
            auto* cur_block = mir_func->get_block(current_block);

            LocalId cmp_result =
                mir_func->add_local("_cmp" + std::to_string(i), hir::make_bool(), true, false);
            cmp_results.push_back(cmp_result);

            LocalId self_field =
                mir_func->add_local("_self_f" + std::to_string(i), field.type, true, false);
            auto self_place = MirPlace(self_local, {PlaceProjection::field(i)});
            cur_block->statements.push_back(MirStatement::assign(
                MirPlace(self_field), MirRvalue::use(MirOperand::copy(self_place))));

            LocalId other_field =
                mir_func->add_local("_other_f" + std::to_string(i), field.type, true, false);
            auto other_place = MirPlace(other_local, {PlaceProjection::field(i)});
            cur_block->statements.push_back(MirStatement::assign(
                MirPlace(other_field), MirRvalue::use(MirOperand::copy(other_place))));

            // フィールド型がstructの場合は__op_eq関数を呼び出す（再帰的比較）
            if (field.type && field.type->kind == hir::TypeKind::Struct) {
                std::string field_op_eq =
                    ast::typekey::spec_fn_prefix(field.type->name) + "__op_eq";

                BlockId eq_call_success = mir_func->add_block();

                std::vector<MirOperandPtr> eq_args;
                eq_args.push_back(MirOperand::copy(MirPlace(self_field)));
                eq_args.push_back(MirOperand::copy(MirPlace(other_field)));

                auto eq_call_term = std::make_unique<MirTerminator>();
                eq_call_term->kind = MirTerminator::Call;
                eq_call_term->data = MirTerminator::CallData{MirOperand::function_ref(field_op_eq),
                                                             std::move(eq_args),
                                                             MirPlace(cmp_result),
                                                             eq_call_success,
                                                             std::nullopt,
                                                             "",
                                                             "",
                                                             false};
                cur_block->terminator = std::move(eq_call_term);
                current_block = eq_call_success;
            } else if (field.type && field.type->kind == hir::TypeKind::Array &&
                       (field.type->array_size.has_value() || field.type->dimensions.size() == 1)) {
                // 固定長1次元配列は要素ごとに比較してANDで畳み込む（コンパイル時展開）
                uint32_t n = field.type->array_size.has_value() ? *field.type->array_size
                                                                : field.type->dimensions[0];
                auto elem_type =
                    field.type->element_type ? field.type->element_type : hir::make_int();

                auto make_idx_const = [&](int64_t v) {
                    auto op = std::make_unique<MirOperand>();
                    op->kind = MirOperand::Constant;
                    MirConstant c;
                    c.value = v;
                    c.type = hir::make_int();
                    op->data = c;
                    return op;
                };

                LocalId elem_acc = 0;
                for (uint32_t j = 0; j < n; ++j) {
                    const std::string etag = std::to_string(i) + "_" + std::to_string(j);
                    LocalId idx = mir_func->add_local("_idx" + etag, hir::make_int(), true, false);
                    cur_block->statements.push_back(MirStatement::assign(
                        MirPlace(idx), MirRvalue::use(make_idx_const(int64_t(j)))));

                    LocalId self_elem =
                        mir_func->add_local("_self_e" + etag, elem_type, true, false);
                    cur_block->statements.push_back(MirStatement::assign(
                        MirPlace(self_elem), MirRvalue::use(MirOperand::copy(MirPlace(
                                                 self_local, {PlaceProjection::field(i),
                                                              PlaceProjection::index(idx)})))));

                    LocalId other_elem =
                        mir_func->add_local("_other_e" + etag, elem_type, true, false);
                    cur_block->statements.push_back(MirStatement::assign(
                        MirPlace(other_elem), MirRvalue::use(MirOperand::copy(MirPlace(
                                                  other_local, {PlaceProjection::field(i),
                                                                PlaceProjection::index(idx)})))));

                    LocalId elem_cmp =
                        mir_func->add_local("_ecmp" + etag, hir::make_bool(), true, false);
                    cur_block->statements.push_back(MirStatement::assign(
                        MirPlace(elem_cmp),
                        MirRvalue::binary(MirBinaryOp::Eq, MirOperand::copy(MirPlace(self_elem)),
                                          MirOperand::copy(MirPlace(other_elem)))));

                    if (j == 0) {
                        elem_acc = elem_cmp;
                    } else {
                        LocalId new_acc =
                            mir_func->add_local("_eacc" + etag, hir::make_bool(), true, false);
                        cur_block->statements.push_back(MirStatement::assign(
                            MirPlace(new_acc),
                            MirRvalue::binary(MirBinaryOp::And,
                                              MirOperand::copy(MirPlace(elem_acc)),
                                              MirOperand::copy(MirPlace(elem_cmp)))));
                        elem_acc = new_acc;
                    }
                }

                cur_block->statements.push_back(MirStatement::assign(
                    MirPlace(cmp_result), MirRvalue::use(MirOperand::copy(MirPlace(elem_acc)))));
            } else {
                // プリミティブ型は直接比較
                cur_block->statements.push_back(MirStatement::assign(
                    MirPlace(cmp_result),
                    MirRvalue::binary(MirBinaryOp::Eq, MirOperand::copy(MirPlace(self_field)),
                                      MirOperand::copy(MirPlace(other_field)))));
            }
        }

        auto* block = mir_func->get_block(current_block);

        if (cmp_results.size() == 1) {
            block->statements.push_back(
                MirStatement::assign(MirPlace(mir_func->return_local),
                                     MirRvalue::use(MirOperand::copy(MirPlace(cmp_results[0])))));
        } else {
            LocalId acc = cmp_results[0];
            for (size_t i = 1; i < cmp_results.size(); ++i) {
                LocalId new_acc =
                    mir_func->add_local("_acc" + std::to_string(i), hir::make_bool(), true, false);
                block->statements.push_back(MirStatement::assign(
                    MirPlace(new_acc),
                    MirRvalue::binary(MirBinaryOp::And, MirOperand::copy(MirPlace(acc)),
                                      MirOperand::copy(MirPlace(cmp_results[i])))));
                acc = new_acc;
            }
            block->statements.push_back(MirStatement::assign(
                MirPlace(mir_func->return_local), MirRvalue::use(MirOperand::copy(MirPlace(acc)))));
        }

        block->terminator = MirTerminator::return_value();
    }

    impl_info[st.name]["Eq"] = func_name;
    mir_program.functions.push_back(std::move(mir_func));
}

// モノモーフィゼーションされた構造体用のOrd演算子を生成
void MirLowering::generate_builtin_lt_operator_for_monomorphized(const MirStruct& st) {
    std::string func_name = ast::typekey::spec_fn_prefix(st.name) + "__op_lt";

    for (const auto& func : mir_program.functions) {
        if (func && func->name == func_name)
            return;
    }

    // ネストしたstruct型フィールドの比較関数を先に生成（再帰的自動実装）
    for (const auto& field : st.fields) {
        if (field.type && field.type->kind == hir::TypeKind::Struct) {
            // そのstruct用の比較関数が既に存在するかチェック
            std::string nested_func_name = field.type->name + "__op_lt";
            bool exists = false;
            for (const auto& func : mir_program.functions) {
                if (func && func->name == nested_func_name) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                // ネスト構造体のMirStructを取得して再帰生成
                for (const auto& mir_st : mir_program.structs) {
                    if (mir_st && mir_st->name == field.type->name) {
                        generate_builtin_lt_operator_for_monomorphized(*mir_st);
                        break;
                    }
                }
            }
        }
    }

    auto mir_func = std::make_unique<MirFunction>();
    mir_func->name = func_name;

    mir_func->return_local = mir_func->add_local("_0", hir::make_bool(), true, false);

    auto struct_type = hir::make_named(st.name);
    LocalId self_local = mir_func->add_local("self", struct_type, false, true);
    LocalId other_local = mir_func->add_local("other", struct_type, false, true);
    mir_func->arg_locals.push_back(self_local);
    mir_func->arg_locals.push_back(other_local);

    BlockId entry_block = mir_func->add_block();
    auto* block = mir_func->get_block(entry_block);

    if (st.fields.empty()) {
        auto const_false = std::make_unique<MirOperand>();
        const_false->kind = MirOperand::Constant;
        MirConstant c;
        c.value = false;
        c.type = hir::make_bool();
        const_false->data = c;

        block->statements.push_back(MirStatement::assign(MirPlace(mir_func->return_local),
                                                         MirRvalue::use(std::move(const_false))));
        block->terminator = MirTerminator::return_value();
    } else {
        BlockId false_block = mir_func->add_block();

        std::vector<BlockId> field_blocks;
        for (size_t i = 0; i < st.fields.size(); ++i) {
            field_blocks.push_back(mir_func->add_block());
        }

        block->terminator = MirTerminator::goto_block(field_blocks[0]);

        for (size_t i = 0; i < st.fields.size(); ++i) {
            const auto& field = st.fields[i];
            auto* field_block = mir_func->get_block(field_blocks[i]);

            LocalId self_field =
                mir_func->add_local("_self_f" + std::to_string(i), field.type, true, false);
            auto self_place = MirPlace(self_local, {PlaceProjection::field(i)});
            field_block->statements.push_back(MirStatement::assign(
                MirPlace(self_field), MirRvalue::use(MirOperand::copy(self_place))));

            LocalId other_field =
                mir_func->add_local("_other_f" + std::to_string(i), field.type, true, false);
            auto other_place = MirPlace(other_local, {PlaceProjection::field(i)});
            field_block->statements.push_back(MirStatement::assign(
                MirPlace(other_field), MirRvalue::use(MirOperand::copy(other_place))));

            LocalId lt_result =
                mir_func->add_local("_lt" + std::to_string(i), hir::make_bool(), true, false);

            // フィールド型がstructの場合は__op_lt関数を呼び出す（再帰的比較）
            if (field.type && field.type->kind == hir::TypeKind::Struct) {
                std::string field_op_lt = field.type->name + "__op_lt";

                // 関数呼び出しのためのブロックを作成
                BlockId lt_call_success = mir_func->add_block();

                std::vector<MirOperandPtr> lt_args;
                lt_args.push_back(MirOperand::copy(MirPlace(self_field)));
                lt_args.push_back(MirOperand::copy(MirPlace(other_field)));

                auto lt_call_term = std::make_unique<MirTerminator>();
                lt_call_term->kind = MirTerminator::Call;
                lt_call_term->data = MirTerminator::CallData{MirOperand::function_ref(field_op_lt),
                                                             std::move(lt_args),
                                                             MirPlace(lt_result),
                                                             lt_call_success,
                                                             std::nullopt,
                                                             "",
                                                             "",
                                                             false};
                field_block->terminator = std::move(lt_call_term);
                field_block = mir_func->get_block(lt_call_success);
            } else {
                // プリミティブ型は直接比較
                field_block->statements.push_back(MirStatement::assign(
                    MirPlace(lt_result),
                    MirRvalue::binary(MirBinaryOp::Lt, MirOperand::copy(MirPlace(self_field)),
                                      MirOperand::copy(MirPlace(other_field)))));
            }

            BlockId lt_true_block = mir_func->add_block();
            BlockId lt_false_check_block = mir_func->add_block();
            field_block->terminator = MirTerminator::switch_int(
                MirOperand::copy(MirPlace(lt_result)), {{1, lt_true_block}}, lt_false_check_block);

            auto* true_ret_block = mir_func->get_block(lt_true_block);
            auto const_true = std::make_unique<MirOperand>();
            const_true->kind = MirOperand::Constant;
            MirConstant c_true;
            c_true.value = true;
            c_true.type = hir::make_bool();
            const_true->data = c_true;
            true_ret_block->statements.push_back(MirStatement::assign(
                MirPlace(mir_func->return_local), MirRvalue::use(std::move(const_true))));
            true_ret_block->terminator = MirTerminator::return_value();

            auto* gt_check_block = mir_func->get_block(lt_false_check_block);
            LocalId gt_result =
                mir_func->add_local("_gt" + std::to_string(i), hir::make_bool(), true, false);

            // フィールド型がstructの場合はGT比較も関数呼び出し（引数入れ替え）
            if (field.type && field.type->kind == hir::TypeKind::Struct) {
                std::string field_op_lt = field.type->name + "__op_lt";

                BlockId gt_call_success = mir_func->add_block();

                // GT = other < self なので引数を逆にする
                std::vector<MirOperandPtr> gt_args;
                gt_args.push_back(MirOperand::copy(MirPlace(other_field)));
                gt_args.push_back(MirOperand::copy(MirPlace(self_field)));

                auto gt_call_term = std::make_unique<MirTerminator>();
                gt_call_term->kind = MirTerminator::Call;
                gt_call_term->data = MirTerminator::CallData{MirOperand::function_ref(field_op_lt),
                                                             std::move(gt_args),
                                                             MirPlace(gt_result),
                                                             gt_call_success,
                                                             std::nullopt,
                                                             "",
                                                             "",
                                                             false};
                gt_check_block->terminator = std::move(gt_call_term);
                gt_check_block = mir_func->get_block(gt_call_success);
            } else {
                // プリミティブ型は直接比較
                gt_check_block->statements.push_back(MirStatement::assign(
                    MirPlace(gt_result),
                    MirRvalue::binary(MirBinaryOp::Gt, MirOperand::copy(MirPlace(self_field)),
                                      MirOperand::copy(MirPlace(other_field)))));
            }

            BlockId next_block = (i + 1 < st.fields.size()) ? field_blocks[i + 1] : false_block;
            gt_check_block->terminator = MirTerminator::switch_int(
                MirOperand::copy(MirPlace(gt_result)), {{1, false_block}}, next_block);
        }

        auto* final_false_block = mir_func->get_block(false_block);
        auto const_false = std::make_unique<MirOperand>();
        const_false->kind = MirOperand::Constant;
        MirConstant c_false;
        c_false.value = false;
        c_false.type = hir::make_bool();
        const_false->data = c_false;
        final_false_block->statements.push_back(MirStatement::assign(
            MirPlace(mir_func->return_local), MirRvalue::use(std::move(const_false))));
        final_false_block->terminator = MirTerminator::return_value();
    }

    impl_info[st.name]["Ord"] = func_name;
    mir_program.functions.push_back(std::move(mir_func));
}

// 構造体の比較演算子を関数呼び出しに変換するパス
void MirLowering::rewrite_struct_comparison_operators() {
    for (auto& func : mir_program.functions) {
        if (!func)
            continue;

        // 各ブロックを走査
        for (size_t block_idx = 0; block_idx < func->basic_blocks.size(); ++block_idx) {
            auto* block = func->get_block(block_idx);
            if (!block)
                continue;

            // 各文を走査
            for (size_t stmt_idx = 0; stmt_idx < block->statements.size(); ++stmt_idx) {
                auto& stmt = block->statements[stmt_idx];
                if (!stmt || stmt->kind != MirStatement::Assign)
                    continue;

                auto* assign_data = std::get_if<MirStatement::AssignData>(&stmt->data);
                if (!assign_data || !assign_data->rvalue)
                    continue;

                auto& rvalue = assign_data->rvalue;
                if (rvalue->kind != MirRvalue::BinaryOp)
                    continue;

                auto& bin_data = std::get<MirRvalue::BinaryOpData>(rvalue->data);

                // ==, !=, <, <=, >, >= の比較演算子をチェック
                bool is_comparison =
                    (bin_data.op == MirBinaryOp::Eq || bin_data.op == MirBinaryOp::Ne ||
                     bin_data.op == MirBinaryOp::Lt || bin_data.op == MirBinaryOp::Le ||
                     bin_data.op == MirBinaryOp::Gt || bin_data.op == MirBinaryOp::Ge);
                // +, -, *, /, % の算術演算子をチェック
                bool is_arithmetic =
                    (bin_data.op == MirBinaryOp::Add || bin_data.op == MirBinaryOp::Sub ||
                     bin_data.op == MirBinaryOp::Mul || bin_data.op == MirBinaryOp::Div ||
                     bin_data.op == MirBinaryOp::Mod);
                // &, |, ^, <<, >> のビット演算子をチェック
                bool is_bitwise =
                    (bin_data.op == MirBinaryOp::BitAnd || bin_data.op == MirBinaryOp::BitOr ||
                     bin_data.op == MirBinaryOp::BitXor || bin_data.op == MirBinaryOp::Shl ||
                     bin_data.op == MirBinaryOp::Shr);
                if (!is_comparison && !is_arithmetic && !is_bitwise) {
                    continue;
                }

                // オペランドのローカル変数を取得
                if (bin_data.lhs->kind != MirOperand::Copy &&
                    bin_data.lhs->kind != MirOperand::Move) {
                    continue;
                }

                auto& lhs_place = std::get<MirPlace>(bin_data.lhs->data);
                LocalId lhs_local = lhs_place.local;

                // ローカル変数の型をチェック
                const auto& local_info = func->locals[lhs_local];
                if (!local_info.type || local_info.type->kind != hir::TypeKind::Struct) {
                    continue;
                }

                std::string type_name = local_info.type->name;

                // impl_info で対応する演算子関数が実装されているかチェック
                std::string op_func_name;
                bool need_negate = false;

                if (bin_data.op == MirBinaryOp::Eq || bin_data.op == MirBinaryOp::Ne) {
                    if (impl_info.count(type_name) && impl_info[type_name].count("Eq")) {
                        op_func_name = ast::typekey::spec_fn_prefix(type_name) + "__op_eq";
                        need_negate = (bin_data.op == MirBinaryOp::Ne);
                    }
                } else if (bin_data.op == MirBinaryOp::Lt || bin_data.op == MirBinaryOp::Le ||
                           bin_data.op == MirBinaryOp::Gt || bin_data.op == MirBinaryOp::Ge) {
                    if (impl_info.count(type_name) && impl_info[type_name].count("Ord")) {
                        op_func_name = ast::typekey::spec_fn_prefix(type_name) + "__op_lt";
                        // > は < の引数を入れ替え、>= と <= は結果を反転
                    }
                } else if (bin_data.op == MirBinaryOp::Add) {
                    if (impl_info.count(type_name) && impl_info[type_name].count("Add")) {
                        op_func_name = type_name + "__op_add";
                    }
                } else if (bin_data.op == MirBinaryOp::Sub) {
                    if (impl_info.count(type_name) && impl_info[type_name].count("Sub")) {
                        op_func_name = type_name + "__op_sub";
                    }
                } else if (bin_data.op == MirBinaryOp::Mul) {
                    if (impl_info.count(type_name) && impl_info[type_name].count("Mul")) {
                        op_func_name = type_name + "__op_mul";
                    }
                } else if (bin_data.op == MirBinaryOp::Div) {
                    if (impl_info.count(type_name) && impl_info[type_name].count("Div")) {
                        op_func_name = type_name + "__op_div";
                    }
                } else if (bin_data.op == MirBinaryOp::Mod) {
                    if (impl_info.count(type_name) && impl_info[type_name].count("Mod")) {
                        op_func_name = type_name + "__op_mod";
                    }
                } else if (bin_data.op == MirBinaryOp::BitAnd) {
                    if (impl_info.count(type_name) && impl_info[type_name].count("BitAnd")) {
                        op_func_name = type_name + "__op_bitand";
                    }
                } else if (bin_data.op == MirBinaryOp::BitOr) {
                    if (impl_info.count(type_name) && impl_info[type_name].count("BitOr")) {
                        op_func_name = type_name + "__op_bitor";
                    }
                } else if (bin_data.op == MirBinaryOp::BitXor) {
                    if (impl_info.count(type_name) && impl_info[type_name].count("BitXor")) {
                        op_func_name = type_name + "__op_bitxor";
                    }
                } else if (bin_data.op == MirBinaryOp::Shl) {
                    if (impl_info.count(type_name) && impl_info[type_name].count("Shl")) {
                        op_func_name = type_name + "__op_shl";
                    }
                } else if (bin_data.op == MirBinaryOp::Shr) {
                    if (impl_info.count(type_name) && impl_info[type_name].count("Shr")) {
                        op_func_name = type_name + "__op_shr";
                    }
                }

                if (op_func_name.empty())
                    continue;

                // 比較演算を関数呼び出しに変換
                // 現在の文を関数呼び出しのターミネータに置き換え、新しいブロックを作成

                // 結果を格納する場所
                MirPlace result_place = assign_data->place;

                // 演算子種別を退避（この後のresizeで文が破棄され、bin_dataは無効になるため）
                const MirBinaryOp bin_op = bin_data.op;

                // 引数を準備
                std::vector<MirOperandPtr> args;
                auto lhs_op = std::make_unique<MirOperand>();
                lhs_op->kind = bin_data.lhs->kind;
                lhs_op->data = bin_data.lhs->data;

                auto rhs_op = std::make_unique<MirOperand>();
                rhs_op->kind = bin_data.rhs->kind;
                rhs_op->data = bin_data.rhs->data;

                // a > b は b < a、a <= b は !(b < a) なので Gt と Le で引数を入れ替える
                if (bin_op == MirBinaryOp::Gt || bin_op == MirBinaryOp::Le) {
                    args.push_back(std::move(rhs_op));
                    args.push_back(std::move(lhs_op));
                } else {
                    args.push_back(std::move(lhs_op));
                    args.push_back(std::move(rhs_op));
                }

                // 継続ブロックを作成
                BlockId cont_block = func->add_block();

                // 残りの文を継続ブロックに移動
                auto* cont = func->get_block(cont_block);
                for (size_t i = stmt_idx + 1; i < block->statements.size(); ++i) {
                    cont->statements.push_back(std::move(block->statements[i]));
                }
                cont->terminator = std::move(block->terminator);

                // 現在のブロックの文を切り詰め
                block->statements.resize(stmt_idx);

                // <= と >= は < の呼び出し結果の反転で導出する
                // （a <= b は !(b < a)、a >= b は !(a < b)。引数の入れ替えは上で実施済み）

                if (bin_op == MirBinaryOp::Le || bin_op == MirBinaryOp::Ge) {
                    // 一時変数を作成して結果を反転
                    LocalId temp_result = func->add_local("_lt_tmp", hir::make_bool(), true, false);

                    // 関数呼び出しターミネータを設定
                    auto func_op = MirOperand::function_ref(op_func_name);
                    BlockId negate_block = func->add_block();

                    auto call_term = std::make_unique<MirTerminator>();
                    call_term->kind = MirTerminator::Call;
                    call_term->data = MirTerminator::CallData{std::move(func_op),
                                                              std::move(args),
                                                              MirPlace(temp_result),
                                                              negate_block,
                                                              std::nullopt,
                                                              "",
                                                              "",
                                                              false};  // 通常の関数呼び出し
                    block->terminator = std::move(call_term);

                    // 反転ブロック
                    auto* neg_block = func->get_block(negate_block);
                    auto unary_rvalue = std::make_unique<MirRvalue>();
                    unary_rvalue->kind = MirRvalue::UnaryOp;
                    unary_rvalue->data = MirRvalue::UnaryOpData{
                        MirUnaryOp::Not, MirOperand::copy(MirPlace(temp_result))};
                    neg_block->statements.push_back(
                        MirStatement::assign(result_place, std::move(unary_rvalue)));
                    neg_block->terminator = MirTerminator::goto_block(cont_block);
                } else if (need_negate) {
                    // != の場合
                    LocalId temp_result = func->add_local("_eq_tmp", hir::make_bool(), true, false);

                    auto func_op = MirOperand::function_ref(op_func_name);
                    BlockId negate_block = func->add_block();

                    auto call_term = std::make_unique<MirTerminator>();
                    call_term->kind = MirTerminator::Call;
                    call_term->data = MirTerminator::CallData{std::move(func_op),
                                                              std::move(args),
                                                              MirPlace(temp_result),
                                                              negate_block,
                                                              std::nullopt,
                                                              "",
                                                              "",
                                                              false};  // 通常の関数呼び出し
                    block->terminator = std::move(call_term);

                    auto* neg_block = func->get_block(negate_block);
                    auto unary_rvalue = std::make_unique<MirRvalue>();
                    unary_rvalue->kind = MirRvalue::UnaryOp;
                    unary_rvalue->data = MirRvalue::UnaryOpData{
                        MirUnaryOp::Not, MirOperand::copy(MirPlace(temp_result))};
                    neg_block->statements.push_back(
                        MirStatement::assign(result_place, std::move(unary_rvalue)));
                    neg_block->terminator = MirTerminator::goto_block(cont_block);
                } else {
                    // ==, < の場合はそのまま
                    auto func_op = MirOperand::function_ref(op_func_name);

                    auto call_term = std::make_unique<MirTerminator>();
                    call_term->kind = MirTerminator::Call;
                    call_term->data = MirTerminator::CallData{std::move(func_op),
                                                              std::move(args),
                                                              result_place,
                                                              cont_block,
                                                              std::nullopt,
                                                              "",
                                                              "",
                                                              false};  // 通常の関数呼び出し
                    block->terminator = std::move(call_term);
                }

                // ブロックが変更されたのでこのブロックの走査を終了
                break;
            }
        }
    }
}

// 組み込みEq演算子（==）の自動実装を生成
void MirLowering::generate_builtin_eq_operator(const hir::HirStruct& st) {
    // 関数名: TypeName__op_eq
    std::string func_name = ast::typekey::spec_fn_prefix(st.name) + "__op_eq";

    auto mir_func = std::make_unique<MirFunction>();
    mir_func->name = func_name;

    // 戻り値: bool (_0)
    mir_func->return_local = mir_func->add_local("_0", hir::make_bool(), true, false);

    // 引数: self (値), other (値) - 両方とも値渡し
    auto struct_type = hir::make_named(st.name);

    LocalId self_local = mir_func->add_local("self", struct_type, false, true);
    LocalId other_local = mir_func->add_local("other", struct_type, false, true);
    mir_func->arg_locals.push_back(self_local);
    mir_func->arg_locals.push_back(other_local);

    // エントリブロックを作成
    BlockId entry_block = mir_func->add_block();
    auto* block = mir_func->get_block(entry_block);

    // フィールド比較のロジックを生成
    if (st.fields.empty()) {
        // フィールドがない場合は常にtrue
        auto const_true = std::make_unique<MirOperand>();
        const_true->kind = MirOperand::Constant;
        MirConstant c;
        c.value = true;
        c.type = hir::make_bool();
        const_true->data = c;

        block->statements.push_back(MirStatement::assign(MirPlace(mir_func->return_local),
                                                         MirRvalue::use(std::move(const_true))));
        block->terminator = MirTerminator::return_value();
    } else {
        // 各フィールドを比較
        std::vector<LocalId> cmp_results;

        for (size_t i = 0; i < st.fields.size(); ++i) {
            const auto& field = st.fields[i];

            LocalId cmp_result =
                mir_func->add_local("_cmp" + std::to_string(i), hir::make_bool(), true, false);
            cmp_results.push_back(cmp_result);

            // self.field を読み込む
            LocalId self_field =
                mir_func->add_local("_self_f" + std::to_string(i), field.type, true, false);
            auto self_place = MirPlace(self_local, {PlaceProjection::field(i)});
            block->statements.push_back(MirStatement::assign(
                MirPlace(self_field), MirRvalue::use(MirOperand::copy(self_place))));

            // other.field を読み込む
            LocalId other_field =
                mir_func->add_local("_other_f" + std::to_string(i), field.type, true, false);
            auto other_place = MirPlace(other_local, {PlaceProjection::field(i)});
            block->statements.push_back(MirStatement::assign(
                MirPlace(other_field), MirRvalue::use(MirOperand::copy(other_place))));

            if (field.type && field.type->kind == hir::TypeKind::Array &&
                (field.type->array_size.has_value() || field.type->dimensions.size() == 1)) {
                // 固定長1次元配列は要素ごとに比較してANDで畳み込む（コンパイル時展開）
                uint32_t n = field.type->array_size.has_value() ? *field.type->array_size
                                                                : field.type->dimensions[0];
                auto elem_type =
                    field.type->element_type ? field.type->element_type : hir::make_int();

                auto make_idx_const = [&](int64_t v) {
                    auto op = std::make_unique<MirOperand>();
                    op->kind = MirOperand::Constant;
                    MirConstant c;
                    c.value = v;
                    c.type = hir::make_int();
                    op->data = c;
                    return op;
                };

                LocalId elem_acc = 0;
                for (uint32_t j = 0; j < n; ++j) {
                    const std::string etag = std::to_string(i) + "_" + std::to_string(j);
                    LocalId idx = mir_func->add_local("_idx" + etag, hir::make_int(), true, false);
                    block->statements.push_back(MirStatement::assign(
                        MirPlace(idx), MirRvalue::use(make_idx_const(int64_t(j)))));

                    LocalId self_elem =
                        mir_func->add_local("_self_e" + etag, elem_type, true, false);
                    block->statements.push_back(MirStatement::assign(
                        MirPlace(self_elem), MirRvalue::use(MirOperand::copy(MirPlace(
                                                 self_local, {PlaceProjection::field(i),
                                                              PlaceProjection::index(idx)})))));

                    LocalId other_elem =
                        mir_func->add_local("_other_e" + etag, elem_type, true, false);
                    block->statements.push_back(MirStatement::assign(
                        MirPlace(other_elem), MirRvalue::use(MirOperand::copy(MirPlace(
                                                  other_local, {PlaceProjection::field(i),
                                                                PlaceProjection::index(idx)})))));

                    LocalId elem_cmp =
                        mir_func->add_local("_ecmp" + etag, hir::make_bool(), true, false);
                    block->statements.push_back(MirStatement::assign(
                        MirPlace(elem_cmp),
                        MirRvalue::binary(MirBinaryOp::Eq, MirOperand::copy(MirPlace(self_elem)),
                                          MirOperand::copy(MirPlace(other_elem)))));

                    if (j == 0) {
                        elem_acc = elem_cmp;
                    } else {
                        LocalId new_acc =
                            mir_func->add_local("_eacc" + etag, hir::make_bool(), true, false);
                        block->statements.push_back(MirStatement::assign(
                            MirPlace(new_acc),
                            MirRvalue::binary(MirBinaryOp::And,
                                              MirOperand::copy(MirPlace(elem_acc)),
                                              MirOperand::copy(MirPlace(elem_cmp)))));
                        elem_acc = new_acc;
                    }
                }

                block->statements.push_back(MirStatement::assign(
                    MirPlace(cmp_result), MirRvalue::use(MirOperand::copy(MirPlace(elem_acc)))));
            } else {
                // 比較結果を格納
                block->statements.push_back(MirStatement::assign(
                    MirPlace(cmp_result),
                    MirRvalue::binary(MirBinaryOp::Eq, MirOperand::copy(MirPlace(self_field)),
                                      MirOperand::copy(MirPlace(other_field)))));
            }
        }

        // 全ての比較結果をANDで結合
        if (cmp_results.size() == 1) {
            block->statements.push_back(
                MirStatement::assign(MirPlace(mir_func->return_local),
                                     MirRvalue::use(MirOperand::copy(MirPlace(cmp_results[0])))));
        } else {
            LocalId acc = cmp_results[0];
            for (size_t i = 1; i < cmp_results.size(); ++i) {
                LocalId new_acc =
                    mir_func->add_local("_acc" + std::to_string(i), hir::make_bool(), true, false);
                block->statements.push_back(MirStatement::assign(
                    MirPlace(new_acc),
                    MirRvalue::binary(MirBinaryOp::And, MirOperand::copy(MirPlace(acc)),
                                      MirOperand::copy(MirPlace(cmp_results[i])))));
                acc = new_acc;
            }
            block->statements.push_back(MirStatement::assign(
                MirPlace(mir_func->return_local), MirRvalue::use(MirOperand::copy(MirPlace(acc)))));
        }

        block->terminator = MirTerminator::return_value();
    }

    // impl_info に登録
    impl_info[st.name]["Eq"] = func_name;

    // MIRプログラムに追加
    mir_program.functions.push_back(std::move(mir_func));
}

// 組み込みOrd演算子（<）の自動実装を生成
void MirLowering::generate_builtin_lt_operator(const hir::HirStruct& st) {
    // 関数名: TypeName__op_lt
    std::string func_name = ast::typekey::spec_fn_prefix(st.name) + "__op_lt";

    auto mir_func = std::make_unique<MirFunction>();
    mir_func->name = func_name;

    // 戻り値: bool (_0)
    mir_func->return_local = mir_func->add_local("_0", hir::make_bool(), true, false);

    // 引数: self (値), other (値)
    auto struct_type = hir::make_named(st.name);

    LocalId self_local = mir_func->add_local("self", struct_type, false, true);
    LocalId other_local = mir_func->add_local("other", struct_type, false, true);
    mir_func->arg_locals.push_back(self_local);
    mir_func->arg_locals.push_back(other_local);

    // エントリブロックを作成
    BlockId entry_block = mir_func->add_block();
    auto* block = mir_func->get_block(entry_block);

    if (st.fields.empty()) {
        // フィールドがない場合は常にfalse（同じなので < ではない）
        auto const_false = std::make_unique<MirOperand>();
        const_false->kind = MirOperand::Constant;
        MirConstant c;
        c.value = false;
        c.type = hir::make_bool();
        const_false->data = c;

        block->statements.push_back(MirStatement::assign(MirPlace(mir_func->return_local),
                                                         MirRvalue::use(std::move(const_false))));
        block->terminator = MirTerminator::return_value();
    } else {
        // 辞書式順序で比較
        // フィールドを順番に比較し、最初の異なるフィールドで判定
        // self.f0 < other.f0 -> true
        // self.f0 > other.f0 -> false
        // self.f0 == other.f0 -> 次のフィールドへ

        // 各フィールドについてブロックを作成
        std::vector<BlockId> field_blocks;
        for (size_t i = 0; i < st.fields.size(); ++i) {
            field_blocks.push_back(mir_func->add_block());
        }
        BlockId false_block = mir_func->add_block();

        // 最初のフィールドのブロックへジャンプ
        block->terminator = MirTerminator::goto_block(field_blocks[0]);

        for (size_t i = 0; i < st.fields.size(); ++i) {
            const auto& field = st.fields[i];
            auto* field_block = mir_func->get_block(field_blocks[i]);

            // フィールド値を読み込み
            LocalId self_field =
                mir_func->add_local("_self_f" + std::to_string(i), field.type, true, false);
            auto self_place = MirPlace(self_local, {PlaceProjection::field(i)});
            field_block->statements.push_back(MirStatement::assign(
                MirPlace(self_field), MirRvalue::use(MirOperand::copy(self_place))));

            LocalId other_field =
                mir_func->add_local("_other_f" + std::to_string(i), field.type, true, false);
            auto other_place = MirPlace(other_local, {PlaceProjection::field(i)});
            field_block->statements.push_back(MirStatement::assign(
                MirPlace(other_field), MirRvalue::use(MirOperand::copy(other_place))));

            // self.field < other.field をチェック
            LocalId lt_result =
                mir_func->add_local("_lt" + std::to_string(i), hir::make_bool(), true, false);

            // フィールド型がstructの場合は__op_lt関数を呼び出す（再帰的比較）
            if (field.type && field.type->kind == hir::TypeKind::Struct) {
                std::string field_op_lt = field.type->name + "__op_lt";

                // ネスト構造体用の比較関数を先に生成（必要であれば）
                std::string nested_func_name = field_op_lt;
                bool exists = false;
                for (const auto& func : mir_program.functions) {
                    if (func && func->name == nested_func_name) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    // ネスト構造体のHirStructを取得して再帰生成
                    for (const auto& mir_st : mir_program.structs) {
                        if (mir_st && mir_st->name == field.type->name) {
                            generate_builtin_lt_operator_for_monomorphized(*mir_st);
                            break;
                        }
                    }
                }

                BlockId lt_call_success = mir_func->add_block();

                std::vector<MirOperandPtr> lt_args;
                lt_args.push_back(MirOperand::copy(MirPlace(self_field)));
                lt_args.push_back(MirOperand::copy(MirPlace(other_field)));

                auto lt_call_term = std::make_unique<MirTerminator>();
                lt_call_term->kind = MirTerminator::Call;
                lt_call_term->data = MirTerminator::CallData{MirOperand::function_ref(field_op_lt),
                                                             std::move(lt_args),
                                                             MirPlace(lt_result),
                                                             lt_call_success,
                                                             std::nullopt,
                                                             "",
                                                             "",
                                                             false};
                field_block->terminator = std::move(lt_call_term);
                field_block = mir_func->get_block(lt_call_success);
            } else {
                field_block->statements.push_back(MirStatement::assign(
                    MirPlace(lt_result),
                    MirRvalue::binary(MirBinaryOp::Lt, MirOperand::copy(MirPlace(self_field)),
                                      MirOperand::copy(MirPlace(other_field)))));
            }

            // lt_result が true なら true を返す
            BlockId lt_true_block = mir_func->add_block();
            BlockId lt_false_check_block = mir_func->add_block();
            field_block->terminator = MirTerminator::switch_int(
                MirOperand::copy(MirPlace(lt_result)), {{1, lt_true_block}}, lt_false_check_block);

            // self < other なので true を返す
            auto* true_ret_block = mir_func->get_block(lt_true_block);
            auto const_true = std::make_unique<MirOperand>();
            const_true->kind = MirOperand::Constant;
            MirConstant c_true;
            c_true.value = true;
            c_true.type = hir::make_bool();
            const_true->data = c_true;
            true_ret_block->statements.push_back(MirStatement::assign(
                MirPlace(mir_func->return_local), MirRvalue::use(std::move(const_true))));
            true_ret_block->terminator = MirTerminator::return_value();

            // self.field >= other.field の場合
            auto* gt_check_block = mir_func->get_block(lt_false_check_block);

            // self.field > other.field をチェック
            LocalId gt_result =
                mir_func->add_local("_gt" + std::to_string(i), hir::make_bool(), true, false);

            // フィールド型がstructの場合はGT比較も関数呼び出し（引数入れ替え）
            if (field.type && field.type->kind == hir::TypeKind::Struct) {
                std::string field_op_lt = field.type->name + "__op_lt";

                BlockId gt_call_success = mir_func->add_block();

                // GT = other < self なので引数を逆にする
                std::vector<MirOperandPtr> gt_args;
                gt_args.push_back(MirOperand::copy(MirPlace(other_field)));
                gt_args.push_back(MirOperand::copy(MirPlace(self_field)));

                auto gt_call_term = std::make_unique<MirTerminator>();
                gt_call_term->kind = MirTerminator::Call;
                gt_call_term->data = MirTerminator::CallData{MirOperand::function_ref(field_op_lt),
                                                             std::move(gt_args),
                                                             MirPlace(gt_result),
                                                             gt_call_success,
                                                             std::nullopt,
                                                             "",
                                                             "",
                                                             false};
                gt_check_block->terminator = std::move(gt_call_term);
                gt_check_block = mir_func->get_block(gt_call_success);
            } else {
                gt_check_block->statements.push_back(MirStatement::assign(
                    MirPlace(gt_result),
                    MirRvalue::binary(MirBinaryOp::Gt, MirOperand::copy(MirPlace(self_field)),
                                      MirOperand::copy(MirPlace(other_field)))));
            }

            // gt_result が true なら false を返す、そうでなければ次のフィールドへ
            BlockId next_block = (i + 1 < st.fields.size()) ? field_blocks[i + 1] : false_block;
            gt_check_block->terminator = MirTerminator::switch_int(
                MirOperand::copy(MirPlace(gt_result)), {{1, false_block}}, next_block);
        }

        // 全フィールドが等しい場合は false
        auto* final_false_block = mir_func->get_block(false_block);
        auto const_false = std::make_unique<MirOperand>();
        const_false->kind = MirOperand::Constant;
        MirConstant c_false;
        c_false.value = false;
        c_false.type = hir::make_bool();
        const_false->data = c_false;
        final_false_block->statements.push_back(MirStatement::assign(
            MirPlace(mir_func->return_local), MirRvalue::use(std::move(const_false))));
        final_false_block->terminator = MirTerminator::return_value();
    }

    // impl_info に登録
    impl_info[st.name]["Ord"] = func_name;

    // MIRプログラムに追加
    mir_program.functions.push_back(std::move(mir_func));
}

}  // namespace cm::mir
