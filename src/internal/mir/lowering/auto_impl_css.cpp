// ============================================================
// 自動実装 - CSS生成メソッド（css/to_css/is_css）
// ============================================================

#include "internal/base/debug.hpp"
#include "lowering.hpp"

#include <algorithm>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cm::mir {

// Debug自動実装: "StructName { field1: value1, field2: value2, ... }"
std::string MirLowering::to_kebab_case(const std::string& name) {
    std::string result;
    result.reserve(name.size());
    for (char c : name) {
        result += (c == '_') ? '-' : c;
    }
    return result;
}

void MirLowering::generate_builtin_css_method_for_monomorphized(const MirStruct& st) {
    std::string func_name = st.name + "__css";

    for (const auto& func : mir_program.functions) {
        if (func && func->name == func_name)
            return;
    }

    auto mir_func = std::make_unique<MirFunction>();
    mir_func->name = func_name;

    auto struct_type = hir::make_named(st.name);
    mir_func->return_local = mir_func->add_local("_0", hir::make_string(), true, false);

    LocalId self_local = mir_func->add_local("self", struct_type, false, true);
    mir_func->arg_locals.push_back(self_local);

    BlockId entry_block = mir_func->add_block();
    auto* block = mir_func->get_block(entry_block);

    LocalId result = mir_func->add_local("_result", hir::make_string(), true, false);
    {
        auto const_init = std::make_unique<MirOperand>();
        const_init->kind = MirOperand::Constant;
        MirConstant c_init;
        c_init.value = std::string();
        c_init.type = hir::make_string();
        const_init->data = c_init;
        block->statements.push_back(
            MirStatement::assign(MirPlace(result), MirRvalue::use(std::move(const_init))));
    }

    std::vector<size_t> field_order(st.fields.size());
    std::iota(field_order.begin(), field_order.end(), 0);
    std::sort(field_order.begin(), field_order.end(), [&](size_t a, size_t b) {
        return to_kebab_case(st.fields[a].name) < to_kebab_case(st.fields[b].name);
    });

    int append_index = 0;
    auto append_literal = [&](BasicBlock* current_block, LocalId& acc, const std::string& literal) {
        LocalId literal_local = mir_func->add_local("_css_lit" + std::to_string(append_index),
                                                    hir::make_string(), true, false);
        auto const_lit = std::make_unique<MirOperand>();
        const_lit->kind = MirOperand::Constant;
        MirConstant c_lit;
        c_lit.value = literal;
        c_lit.type = hir::make_string();
        const_lit->data = c_lit;
        current_block->statements.push_back(
            MirStatement::assign(MirPlace(literal_local), MirRvalue::use(std::move(const_lit))));

        LocalId concat_local = mir_func->add_local("_css_concat" + std::to_string(append_index),
                                                   hir::make_string(), true, false);
        current_block->statements.push_back(MirStatement::assign(
            MirPlace(concat_local),
            MirRvalue::binary(MirBinaryOp::Add, MirOperand::copy(MirPlace(acc)),
                              MirOperand::copy(MirPlace(literal_local)))));
        acc = concat_local;
        append_index++;
    };

    auto append_operand = [&](BasicBlock* current_block, LocalId& acc, LocalId value) {
        LocalId concat_local = mir_func->add_local("_css_val" + std::to_string(append_index),
                                                   hir::make_string(), true, false);
        current_block->statements.push_back(MirStatement::assign(
            MirPlace(concat_local),
            MirRvalue::binary(MirBinaryOp::Add, MirOperand::copy(MirPlace(acc)),
                              MirOperand::copy(MirPlace(value)))));
        acc = concat_local;
        append_index++;
    };

    for (size_t order_index = 0; order_index < field_order.size(); ++order_index) {
        const auto& field = st.fields[field_order[order_index]];
        const std::string key = to_kebab_case(field.name);

        LocalId field_val =
            mir_func->add_local("_field" + std::to_string(order_index), field.type, true, false);
        auto field_place = MirPlace(self_local, {PlaceProjection::field(field_order[order_index])});
        block->statements.push_back(MirStatement::assign(
            MirPlace(field_val), MirRvalue::use(MirOperand::copy(field_place))));

        if (field.type->kind == hir::TypeKind::Bool) {
            BlockId append_block = mir_func->add_block();
            BlockId next_block = mir_func->add_block();
            block->terminator = MirTerminator::switch_int(
                MirOperand::copy(MirPlace(field_val)),
                std::vector<std::pair<int64_t, BlockId>>{{1, append_block}}, next_block);

            block = mir_func->get_block(append_block);
            append_literal(block, result, key + "; ");
            block->terminator = MirTerminator::goto_block(next_block);
            block = mir_func->get_block(next_block);
            continue;
        }

        const bool is_nested_css = field.type->kind == hir::TypeKind::Struct;
        if (is_nested_css) {
            append_literal(block, result, key + " { ");
        } else {
            append_literal(block, result, key + ": ");
        }

        LocalId field_str = mir_func->add_local("_fstr" + std::to_string(order_index),
                                                hir::make_string(), true, false);
        std::string convert_func;
        if (field.type->kind == hir::TypeKind::Int) {
            convert_func = "cm_format_int";
        } else if (field.type->kind == hir::TypeKind::UInt) {
            convert_func = "cm_format_uint";
        } else if (field.type->kind == hir::TypeKind::Bool) {
            convert_func = "cm_format_bool";
        } else if (field.type->kind == hir::TypeKind::Float ||
                   field.type->kind == hir::TypeKind::Double) {
            convert_func = "cm_format_double";
        } else if (field.type->kind == hir::TypeKind::String) {
            block->statements.push_back(MirStatement::assign(
                MirPlace(field_str), MirRvalue::use(MirOperand::copy(MirPlace(field_val)))));
            convert_func = "";
        } else if (field.type->kind == hir::TypeKind::Char) {
            convert_func = "cm_format_char";
        } else if (field.type->kind == hir::TypeKind::Struct) {
            convert_func = field.type->name + "__css";
        } else {
            convert_func = "cm_format_int";
        }

        if (!convert_func.empty()) {
            std::vector<MirOperandPtr> args;
            args.push_back(MirOperand::copy(MirPlace(field_val)));

            BlockId next_block = mir_func->add_block();
            auto call_term = std::make_unique<MirTerminator>();
            call_term->kind = MirTerminator::Call;
            call_term->data = MirTerminator::CallData{MirOperand::function_ref(convert_func),
                                                      std::move(args),
                                                      MirPlace(field_str),
                                                      next_block,
                                                      std::nullopt,
                                                      std::string(),
                                                      std::string(),
                                                      false};
            block->terminator = std::move(call_term);
            block = mir_func->get_block(next_block);
        }

        append_operand(block, result, field_str);
        if (is_nested_css) {
            append_literal(block, result, " } ");
        } else {
            append_literal(block, result, "; ");
        }
    }

    block->statements.push_back(MirStatement::assign(
        MirPlace(mir_func->return_local), MirRvalue::use(MirOperand::copy(MirPlace(result)))));
    block->terminator = MirTerminator::return_value();

    impl_info[st.name]["Css"] = func_name;
    mir_program.functions.push_back(std::move(mir_func));
}

// to_css() は css() のエイリアス（モノモーフィゼーション版）
void MirLowering::generate_builtin_to_css_method_for_monomorphized(const MirStruct& st) {
    std::string func_name = st.name + "__to_css";
    std::string css_func_name = st.name + "__css";

    for (const auto& func : mir_program.functions) {
        if (func && func->name == func_name)
            return;
    }

    auto mir_func = std::make_unique<MirFunction>();
    mir_func->name = func_name;

    auto struct_type = hir::make_named(st.name);
    mir_func->return_local = mir_func->add_local("_0", hir::make_string(), true, false);

    LocalId self_local = mir_func->add_local("self", struct_type, false, true);
    mir_func->arg_locals.push_back(self_local);

    BlockId entry_block = mir_func->add_block();
    auto* block = mir_func->get_block(entry_block);

    BlockId return_block = mir_func->add_block();
    std::vector<MirOperandPtr> args;
    args.push_back(MirOperand::copy(MirPlace(self_local)));

    auto call_term = std::make_unique<MirTerminator>();
    call_term->kind = MirTerminator::Call;
    call_term->data = MirTerminator::CallData{MirOperand::function_ref(css_func_name),
                                              std::move(args),
                                              MirPlace(mir_func->return_local),
                                              return_block,
                                              std::nullopt,
                                              std::string(),
                                              std::string(),
                                              false};
    block->terminator = std::move(call_term);

    auto* ret_block = mir_func->get_block(return_block);
    ret_block->terminator = MirTerminator::return_value();

    mir_program.functions.push_back(std::move(mir_func));
}

void MirLowering::generate_builtin_is_css_method_for_monomorphized(const MirStruct& st) {
    std::string func_name = st.name + "__isCss";

    for (const auto& func : mir_program.functions) {
        if (func && func->name == func_name)
            return;
    }

    auto mir_func = std::make_unique<MirFunction>();
    mir_func->name = func_name;

    auto struct_type = hir::make_named(st.name);
    mir_func->return_local = mir_func->add_local("_0", hir::make_bool(), true, false);

    LocalId self_local = mir_func->add_local("self", struct_type, false, true);
    mir_func->arg_locals.push_back(self_local);

    BlockId entry_block = mir_func->add_block();
    auto* block = mir_func->get_block(entry_block);

    auto const_true = std::make_unique<MirOperand>();
    const_true->kind = MirOperand::Constant;
    MirConstant c_true;
    c_true.value = true;
    c_true.type = hir::make_bool();
    const_true->data = c_true;
    block->statements.push_back(MirStatement::assign(MirPlace(mir_func->return_local),
                                                     MirRvalue::use(std::move(const_true))));

    block->terminator = MirTerminator::return_value();

    impl_info[st.name]["Css"] = func_name;
    mir_program.functions.push_back(std::move(mir_func));
}

}  // namespace cm::mir
