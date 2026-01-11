#include "generator.hpp"

namespace cm::mir {

// ============================================================
// 組み込みCloneメソッドの自動実装
// ============================================================
void AutoImplGenerator::generate_builtin_clone_method(const hir::HirStruct& st) {
    std::string func_name = st.name + "__clone";

    auto mir_func = std::make_unique<MirFunction>();
    mir_func->name = func_name;

    auto struct_type = hir::make_named(st.name);
    mir_func->return_local = mir_func->add_local("_0", struct_type, true, false);

    LocalId self_local = mir_func->add_local("self", struct_type, false, true);
    mir_func->arg_locals.push_back(self_local);

    BlockId entry_block = mir_func->add_block();
    auto* block = mir_func->get_block(entry_block);

    block->statements.push_back(MirStatement::assign(
        MirPlace(mir_func->return_local), MirRvalue::use(MirOperand::copy(MirPlace(self_local)))));

    block->terminator = MirTerminator::return_value();

    ctx_.impl_info[st.name]["Clone"] = func_name;
    ctx_.program.functions.push_back(std::move(mir_func));
}

// ============================================================
// モノモーフィゼーション版Clone
// ============================================================
void AutoImplGenerator::generate_builtin_clone_method_for_monomorphized(const MirStruct& st) {
    std::string func_name = st.name + "__clone";

    for (const auto& func : ctx_.program.functions) {
        if (func && func->name == func_name)
            return;
    }

    auto mir_func = std::make_unique<MirFunction>();
    mir_func->name = func_name;

    auto struct_type = hir::make_named(st.name);
    mir_func->return_local = mir_func->add_local("_0", struct_type, true, false);

    LocalId self_local = mir_func->add_local("self", struct_type, false, true);
    mir_func->arg_locals.push_back(self_local);

    BlockId entry_block = mir_func->add_block();
    auto* block = mir_func->get_block(entry_block);

    block->statements.push_back(MirStatement::assign(
        MirPlace(mir_func->return_local), MirRvalue::use(MirOperand::copy(MirPlace(self_local)))));

    block->terminator = MirTerminator::return_value();

    ctx_.impl_info[st.name]["Clone"] = func_name;
    ctx_.program.functions.push_back(std::move(mir_func));
}

// ============================================================
// 組み込みHashメソッドの自動実装
// ============================================================
void AutoImplGenerator::generate_builtin_hash_method(const hir::HirStruct& st) {
    std::string func_name = st.name + "__hash";

    auto mir_func = std::make_unique<MirFunction>();
    mir_func->name = func_name;

    auto struct_type = hir::make_named(st.name);
    mir_func->return_local = mir_func->add_local("_0", hir::make_int(), true, false);

    LocalId self_local = mir_func->add_local("self", struct_type, false, true);
    mir_func->arg_locals.push_back(self_local);

    BlockId entry_block = mir_func->add_block();
    auto* block = mir_func->get_block(entry_block);

    if (st.fields.empty()) {
        auto const_zero = std::make_unique<MirOperand>();
        const_zero->kind = MirOperand::Constant;
        MirConstant c;
        c.value = int64_t(0);
        c.type = hir::make_int();
        const_zero->data = c;

        block->statements.push_back(MirStatement::assign(MirPlace(mir_func->return_local),
                                                         MirRvalue::use(std::move(const_zero))));
    } else {
        LocalId acc = mir_func->add_local("_hash_acc", hir::make_int(), true, false);

        auto const_zero = std::make_unique<MirOperand>();
        const_zero->kind = MirOperand::Constant;
        MirConstant c;
        c.value = int64_t(0);
        c.type = hir::make_int();
        const_zero->data = c;

        block->statements.push_back(
            MirStatement::assign(MirPlace(acc), MirRvalue::use(std::move(const_zero))));

        for (size_t i = 0; i < st.fields.size(); ++i) {
            const auto& field = st.fields[i];

            LocalId field_val =
                mir_func->add_local("_f" + std::to_string(i), field.type, true, false);
            auto field_place = MirPlace(self_local, {PlaceProjection::field(i)});
            block->statements.push_back(MirStatement::assign(
                MirPlace(field_val), MirRvalue::use(MirOperand::copy(field_place))));

            LocalId new_acc =
                mir_func->add_local("_acc" + std::to_string(i), hir::make_int(), true, false);
            block->statements.push_back(MirStatement::assign(
                MirPlace(new_acc),
                MirRvalue::binary(MirBinaryOp::Add, MirOperand::copy(MirPlace(acc)),
                                  MirOperand::copy(MirPlace(field_val)))));
            acc = new_acc;
        }

        block->statements.push_back(MirStatement::assign(
            MirPlace(mir_func->return_local), MirRvalue::use(MirOperand::copy(MirPlace(acc)))));
    }

    block->terminator = MirTerminator::return_value();

    ctx_.impl_info[st.name]["Hash"] = func_name;
    ctx_.program.functions.push_back(std::move(mir_func));
}

// ============================================================
// モノモーフィゼーション版Hash
// ============================================================
void AutoImplGenerator::generate_builtin_hash_method_for_monomorphized(const MirStruct& st) {
    std::string func_name = st.name + "__hash";

    for (const auto& func : ctx_.program.functions) {
        if (func && func->name == func_name)
            return;
    }

    auto mir_func = std::make_unique<MirFunction>();
    mir_func->name = func_name;

    auto struct_type = hir::make_named(st.name);
    mir_func->return_local = mir_func->add_local("_0", hir::make_int(), true, false);

    LocalId self_local = mir_func->add_local("self", struct_type, false, true);
    mir_func->arg_locals.push_back(self_local);

    BlockId entry_block = mir_func->add_block();
    auto* block = mir_func->get_block(entry_block);

    if (st.fields.empty()) {
        auto const_zero = std::make_unique<MirOperand>();
        const_zero->kind = MirOperand::Constant;
        MirConstant c;
        c.value = int64_t(0);
        c.type = hir::make_int();
        const_zero->data = c;

        block->statements.push_back(MirStatement::assign(MirPlace(mir_func->return_local),
                                                         MirRvalue::use(std::move(const_zero))));
    } else {
        LocalId acc = mir_func->add_local("_hash_acc", hir::make_int(), true, false);

        auto const_zero = std::make_unique<MirOperand>();
        const_zero->kind = MirOperand::Constant;
        MirConstant c;
        c.value = int64_t(0);
        c.type = hir::make_int();
        const_zero->data = c;

        block->statements.push_back(
            MirStatement::assign(MirPlace(acc), MirRvalue::use(std::move(const_zero))));

        for (size_t i = 0; i < st.fields.size(); ++i) {
            const auto& field = st.fields[i];

            LocalId field_val =
                mir_func->add_local("_f" + std::to_string(i), field.type, true, false);
            auto field_place = MirPlace(self_local, {PlaceProjection::field(i)});
            block->statements.push_back(MirStatement::assign(
                MirPlace(field_val), MirRvalue::use(MirOperand::copy(field_place))));

            LocalId new_acc =
                mir_func->add_local("_acc" + std::to_string(i), hir::make_int(), true, false);
            block->statements.push_back(MirStatement::assign(
                MirPlace(new_acc),
                MirRvalue::binary(MirBinaryOp::Add, MirOperand::copy(MirPlace(acc)),
                                  MirOperand::copy(MirPlace(field_val)))));
            acc = new_acc;
        }

        block->statements.push_back(MirStatement::assign(
            MirPlace(mir_func->return_local), MirRvalue::use(MirOperand::copy(MirPlace(acc)))));
    }

    block->terminator = MirTerminator::return_value();

    ctx_.impl_info[st.name]["Hash"] = func_name;
    ctx_.program.functions.push_back(std::move(mir_func));
}

}  // namespace cm::mir
