#include "generator.hpp"

namespace cm::mir {

// ============================================================
// 組み込みOrd演算子（<）の自動実装
// ============================================================
void AutoImplGenerator::generate_builtin_lt_operator(const hir::HirStruct& st) {
    std::string func_name = st.name + "__op_lt";

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
        std::vector<BlockId> field_blocks;
        for (size_t i = 0; i < st.fields.size(); ++i) {
            field_blocks.push_back(mir_func->add_block());
        }
        BlockId false_block = mir_func->add_block();

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

            field_block->statements.push_back(MirStatement::assign(
                MirPlace(lt_result),
                MirRvalue::binary(MirBinaryOp::Lt, MirOperand::copy(MirPlace(self_field)),
                                  MirOperand::copy(MirPlace(other_field)))));

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

            gt_check_block->statements.push_back(MirStatement::assign(
                MirPlace(gt_result),
                MirRvalue::binary(MirBinaryOp::Gt, MirOperand::copy(MirPlace(self_field)),
                                  MirOperand::copy(MirPlace(other_field)))));

            BlockId next_block = (i + 1 < st.fields.size()) ? field_blocks[i + 1] : false_block;
            gt_check_block->terminator = MirTerminator::switch_int(
                MirOperand::copy(MirPlace(gt_result)), {{1, false_block}}, next_block);
        }

        auto* final_false_block = mir_func->get_block(false_block);
        auto const_false_final = std::make_unique<MirOperand>();
        const_false_final->kind = MirOperand::Constant;
        MirConstant c_false;
        c_false.value = false;
        c_false.type = hir::make_bool();
        const_false_final->data = c_false;
        final_false_block->statements.push_back(MirStatement::assign(
            MirPlace(mir_func->return_local), MirRvalue::use(std::move(const_false_final))));
        final_false_block->terminator = MirTerminator::return_value();
    }

    ctx_.impl_info[st.name]["Ord"] = func_name;
    ctx_.program.functions.push_back(std::move(mir_func));
}

// ============================================================
// モノモーフィゼーション版Ord演算子
// ============================================================
void AutoImplGenerator::generate_builtin_lt_operator_for_monomorphized(const MirStruct& st) {
    std::string func_name = st.name + "__op_lt";

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

            field_block->statements.push_back(MirStatement::assign(
                MirPlace(lt_result),
                MirRvalue::binary(MirBinaryOp::Lt, MirOperand::copy(MirPlace(self_field)),
                                  MirOperand::copy(MirPlace(other_field)))));

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

            gt_check_block->statements.push_back(MirStatement::assign(
                MirPlace(gt_result),
                MirRvalue::binary(MirBinaryOp::Gt, MirOperand::copy(MirPlace(self_field)),
                                  MirOperand::copy(MirPlace(other_field)))));

            BlockId next_block = (i + 1 < st.fields.size()) ? field_blocks[i + 1] : false_block;
            gt_check_block->terminator = MirTerminator::switch_int(
                MirOperand::copy(MirPlace(gt_result)), {{1, false_block}}, next_block);
        }

        auto* final_false_block = mir_func->get_block(false_block);
        auto const_false_final = std::make_unique<MirOperand>();
        const_false_final->kind = MirOperand::Constant;
        MirConstant c_false;
        c_false.value = false;
        c_false.type = hir::make_bool();
        const_false_final->data = c_false;
        final_false_block->statements.push_back(MirStatement::assign(
            MirPlace(mir_func->return_local), MirRvalue::use(std::move(const_false_final))));
        final_false_block->terminator = MirTerminator::return_value();
    }

    ctx_.impl_info[st.name]["Ord"] = func_name;
    ctx_.program.functions.push_back(std::move(mir_func));
}

}  // namespace cm::mir
