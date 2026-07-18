#include "generator.hpp"

namespace cm::mir {

// ============================================================
// 組み込みEq演算子（==）の自動実装
// ============================================================
void AutoImplGenerator::generate_builtin_eq_operator(const hir::HirStruct& st) {
    std::string func_name = st.name + "__op_eq";

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

        for (size_t i = 0; i < st.fields.size(); ++i) {
            const auto& field = st.fields[i];

            LocalId cmp_result =
                mir_func->add_local("_cmp" + std::to_string(i), hir::make_bool(), true, false);
            cmp_results.push_back(cmp_result);

            LocalId self_field =
                mir_func->add_local("_self_f" + std::to_string(i), field.type, true, false);
            auto self_place = MirPlace(self_local, {PlaceProjection::field(i)});
            block->statements.push_back(MirStatement::assign(
                MirPlace(self_field), MirRvalue::use(MirOperand::copy(self_place))));

            LocalId other_field =
                mir_func->add_local("_other_f" + std::to_string(i), field.type, true, false);
            auto other_place = MirPlace(other_local, {PlaceProjection::field(i)});
            block->statements.push_back(MirStatement::assign(
                MirPlace(other_field), MirRvalue::use(MirOperand::copy(other_place))));

            block->statements.push_back(MirStatement::assign(
                MirPlace(cmp_result),
                MirRvalue::binary(MirBinaryOp::Eq, MirOperand::copy(MirPlace(self_field)),
                                  MirOperand::copy(MirPlace(other_field)))));
        }

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

    ctx_.impl_info[st.name]["Eq"] = func_name;
    ctx_.program.functions.push_back(std::move(mir_func));
}

// ============================================================
// モノモーフィゼーション版Eq演算子
// ============================================================
void AutoImplGenerator::generate_builtin_eq_operator_for_monomorphized(const MirStruct& st) {
    std::string func_name = st.name + "__op_eq";

    // 既に生成されている場合はスキップ
    for (const auto& func : ctx_.program.functions) {
        if (func && func->name == func_name)
            return;
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

            // フィールド型がstructなら再帰呼び出し
            if (field.type && field.type->kind == hir::TypeKind::Struct) {
                std::string field_op_eq = field.type->name + "__op_eq";
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
            } else {
                cur_block->statements.push_back(MirStatement::assign(
                    MirPlace(cmp_result),
                    MirRvalue::binary(MirBinaryOp::Eq, MirOperand::copy(MirPlace(self_field)),
                                      MirOperand::copy(MirPlace(other_field)))));
            }
        }

        auto* final_block = mir_func->get_block(current_block);

        if (cmp_results.size() == 1) {
            final_block->statements.push_back(
                MirStatement::assign(MirPlace(mir_func->return_local),
                                     MirRvalue::use(MirOperand::copy(MirPlace(cmp_results[0])))));
        } else {
            LocalId acc = cmp_results[0];
            for (size_t i = 1; i < cmp_results.size(); ++i) {
                LocalId new_acc =
                    mir_func->add_local("_acc" + std::to_string(i), hir::make_bool(), true, false);
                final_block->statements.push_back(MirStatement::assign(
                    MirPlace(new_acc),
                    MirRvalue::binary(MirBinaryOp::And, MirOperand::copy(MirPlace(acc)),
                                      MirOperand::copy(MirPlace(cmp_results[i])))));
                acc = new_acc;
            }
            final_block->statements.push_back(MirStatement::assign(
                MirPlace(mir_func->return_local), MirRvalue::use(MirOperand::copy(MirPlace(acc)))));
        }

        final_block->terminator = MirTerminator::return_value();
    }

    ctx_.impl_info[st.name]["Eq"] = func_name;
    ctx_.program.functions.push_back(std::move(mir_func));
}

}  // namespace cm::mir
