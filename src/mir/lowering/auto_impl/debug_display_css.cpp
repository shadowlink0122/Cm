#include "generator.hpp"

#include <algorithm>
#include <numeric>

namespace cm::mir {

// ============================================================
// ヘルパー関数: kebab-case変換（CSS用）
// ============================================================
static std::string to_local_kebab_case(const std::string& name) {
    std::string result;
    result.reserve(name.size());
    for (char c : name) {
        result += (c == '_') ? '-' : c;
    }
    return result;
}

// ============================================================
// 組み込みDebugメソッドの自動実装
// 出力形式: "TypeName { field1: value1, field2: value2, ... }"
// ============================================================
void AutoImplGenerator::generate_builtin_debug_method(const hir::HirStruct& st) {
    std::string func_name = st.name + "__debug";

    auto mir_func = std::make_unique<MirFunction>();
    mir_func->name = func_name;

    auto struct_type = hir::make_named(st.name);
    mir_func->return_local = mir_func->add_local("_0", hir::make_string(), true, false);

    LocalId self_local = mir_func->add_local("self", struct_type, false, true);
    mir_func->arg_locals.push_back(self_local);

    BlockId entry_block = mir_func->add_block();
    auto* block = mir_func->get_block(entry_block);

    // 初期値: "TypeName { "
    std::string initial_str = st.name + " { ";
    LocalId result = mir_func->add_local("_result", hir::make_string(), true, false);

    auto const_init = std::make_unique<MirOperand>();
    const_init->kind = MirOperand::Constant;
    MirConstant c_init;
    c_init.value = initial_str;
    c_init.type = hir::make_string();
    const_init->data = c_init;
    block->statements.push_back(
        MirStatement::assign(MirPlace(result), MirRvalue::use(std::move(const_init))));

    for (size_t i = 0; i < st.fields.size(); ++i) {
        const auto& field = st.fields[i];

        // "field_name: " を追加
        std::string field_prefix = field.name + ": ";
        LocalId prefix_str =
            mir_func->add_local("_prefix" + std::to_string(i), hir::make_string(), true, false);

        auto const_prefix = std::make_unique<MirOperand>();
        const_prefix->kind = MirOperand::Constant;
        MirConstant c_prefix;
        c_prefix.value = field_prefix;
        c_prefix.type = hir::make_string();
        const_prefix->data = c_prefix;
        block->statements.push_back(
            MirStatement::assign(MirPlace(prefix_str), MirRvalue::use(std::move(const_prefix))));

        // result = result + field_prefix
        LocalId concat1 =
            mir_func->add_local("_concat1_" + std::to_string(i), hir::make_string(), true, false);
        block->statements.push_back(MirStatement::assign(
            MirPlace(concat1),
            MirRvalue::binary(MirBinaryOp::Add, MirOperand::copy(MirPlace(result)),
                              MirOperand::copy(MirPlace(prefix_str)))));
        result = concat1;

        // フィールド値を取得
        LocalId field_val =
            mir_func->add_local("_field" + std::to_string(i), field.type, true, false);
        auto field_place = MirPlace(self_local, {PlaceProjection::field(i)});
        block->statements.push_back(MirStatement::assign(
            MirPlace(field_val), MirRvalue::use(MirOperand::copy(field_place))));

        // 文字列に変換
        LocalId field_str =
            mir_func->add_local("_fstr" + std::to_string(i), hir::make_string(), true, false);

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
            convert_func = field.type->name + "__debug";
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

        // result = result + field_str
        LocalId concat2 =
            mir_func->add_local("_concat2_" + std::to_string(i), hir::make_string(), true, false);
        block->statements.push_back(MirStatement::assign(
            MirPlace(concat2),
            MirRvalue::binary(MirBinaryOp::Add, MirOperand::copy(MirPlace(result)),
                              MirOperand::copy(MirPlace(field_str)))));
        result = concat2;

        // ", " を追加（最後以外）
        if (i + 1 < st.fields.size()) {
            std::string sep = ", ";
            LocalId sep_str =
                mir_func->add_local("_sep" + std::to_string(i), hir::make_string(), true, false);

            auto const_sep = std::make_unique<MirOperand>();
            const_sep->kind = MirOperand::Constant;
            MirConstant c_sep;
            c_sep.value = sep;
            c_sep.type = hir::make_string();
            const_sep->data = c_sep;
            block->statements.push_back(
                MirStatement::assign(MirPlace(sep_str), MirRvalue::use(std::move(const_sep))));

            LocalId concat3 = mir_func->add_local("_concat3_" + std::to_string(i),
                                                  hir::make_string(), true, false);
            block->statements.push_back(MirStatement::assign(
                MirPlace(concat3),
                MirRvalue::binary(MirBinaryOp::Add, MirOperand::copy(MirPlace(result)),
                                  MirOperand::copy(MirPlace(sep_str)))));
            result = concat3;
        }
    }

    // 末尾の " }" を追加
    std::string closing = st.fields.empty() ? "}" : " }";
    LocalId close_str = mir_func->add_local("_close", hir::make_string(), true, false);

    auto const_close = std::make_unique<MirOperand>();
    const_close->kind = MirOperand::Constant;
    MirConstant c_close;
    c_close.value = closing;
    c_close.type = hir::make_string();
    const_close->data = c_close;
    block->statements.push_back(
        MirStatement::assign(MirPlace(close_str), MirRvalue::use(std::move(const_close))));

    LocalId final_result = mir_func->add_local("_final", hir::make_string(), true, false);
    block->statements.push_back(
        MirStatement::assign(MirPlace(final_result),
                             MirRvalue::binary(MirBinaryOp::Add, MirOperand::copy(MirPlace(result)),
                                               MirOperand::copy(MirPlace(close_str)))));

    block->statements.push_back(
        MirStatement::assign(MirPlace(mir_func->return_local),
                             MirRvalue::use(MirOperand::copy(MirPlace(final_result)))));

    block->terminator = MirTerminator::return_value();

    ctx_.impl_info[st.name]["Debug"] = func_name;
    ctx_.program.functions.push_back(std::move(mir_func));
}

// ============================================================
// 組み込みDisplayメソッドの自動実装
// 出力形式: "(value1, value2, ...)"
// ============================================================
void AutoImplGenerator::generate_builtin_display_method(const hir::HirStruct& st) {
    std::string func_name = st.name + "__toString";

    auto mir_func = std::make_unique<MirFunction>();
    mir_func->name = func_name;

    auto struct_type = hir::make_named(st.name);
    mir_func->return_local = mir_func->add_local("_0", hir::make_string(), true, false);

    LocalId self_local = mir_func->add_local("self", struct_type, false, true);
    mir_func->arg_locals.push_back(self_local);

    BlockId entry_block = mir_func->add_block();
    auto* block = mir_func->get_block(entry_block);

    // 初期値: "("
    LocalId result = mir_func->add_local("_result", hir::make_string(), true, false);

    auto const_init = std::make_unique<MirOperand>();
    const_init->kind = MirOperand::Constant;
    MirConstant c_init;
    c_init.value = std::string("(");
    c_init.type = hir::make_string();
    const_init->data = c_init;
    block->statements.push_back(
        MirStatement::assign(MirPlace(result), MirRvalue::use(std::move(const_init))));

    for (size_t i = 0; i < st.fields.size(); ++i) {
        const auto& field = st.fields[i];

        LocalId field_val =
            mir_func->add_local("_field" + std::to_string(i), field.type, true, false);
        auto field_place = MirPlace(self_local, {PlaceProjection::field(i)});
        block->statements.push_back(MirStatement::assign(
            MirPlace(field_val), MirRvalue::use(MirOperand::copy(field_place))));

        LocalId field_str =
            mir_func->add_local("_fstr" + std::to_string(i), hir::make_string(), true, false);

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
            convert_func = field.type->name + "__toString";
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

        LocalId concat =
            mir_func->add_local("_concat" + std::to_string(i), hir::make_string(), true, false);
        block->statements.push_back(MirStatement::assign(
            MirPlace(concat),
            MirRvalue::binary(MirBinaryOp::Add, MirOperand::copy(MirPlace(result)),
                              MirOperand::copy(MirPlace(field_str)))));
        result = concat;

        if (i + 1 < st.fields.size()) {
            std::string sep = ", ";
            LocalId sep_str =
                mir_func->add_local("_sep" + std::to_string(i), hir::make_string(), true, false);

            auto const_sep = std::make_unique<MirOperand>();
            const_sep->kind = MirOperand::Constant;
            MirConstant c_sep;
            c_sep.value = sep;
            c_sep.type = hir::make_string();
            const_sep->data = c_sep;
            block->statements.push_back(
                MirStatement::assign(MirPlace(sep_str), MirRvalue::use(std::move(const_sep))));

            LocalId concat2 = mir_func->add_local("_concat2_" + std::to_string(i),
                                                  hir::make_string(), true, false);
            block->statements.push_back(MirStatement::assign(
                MirPlace(concat2),
                MirRvalue::binary(MirBinaryOp::Add, MirOperand::copy(MirPlace(result)),
                                  MirOperand::copy(MirPlace(sep_str)))));
            result = concat2;
        }
    }

    LocalId close_str = mir_func->add_local("_close", hir::make_string(), true, false);
    auto const_close = std::make_unique<MirOperand>();
    const_close->kind = MirOperand::Constant;
    MirConstant c_close;
    c_close.value = std::string(")");
    c_close.type = hir::make_string();
    const_close->data = c_close;
    block->statements.push_back(
        MirStatement::assign(MirPlace(close_str), MirRvalue::use(std::move(const_close))));

    LocalId final_result = mir_func->add_local("_final", hir::make_string(), true, false);
    block->statements.push_back(
        MirStatement::assign(MirPlace(final_result),
                             MirRvalue::binary(MirBinaryOp::Add, MirOperand::copy(MirPlace(result)),
                                               MirOperand::copy(MirPlace(close_str)))));

    block->statements.push_back(
        MirStatement::assign(MirPlace(mir_func->return_local),
                             MirRvalue::use(MirOperand::copy(MirPlace(final_result)))));

    block->terminator = MirTerminator::return_value();

    ctx_.impl_info[st.name]["Display"] = func_name;
    ctx_.program.functions.push_back(std::move(mir_func));
}

// ============================================================
// 組み込みCssメソッドの自動実装
// ============================================================
void AutoImplGenerator::generate_builtin_css_method(const hir::HirStruct& st) {
    std::string func_name = st.name + "__css";

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

    // フィールドをkebab-case順にソート
    std::vector<size_t> field_order(st.fields.size());
    std::iota(field_order.begin(), field_order.end(), 0);
    std::sort(field_order.begin(), field_order.end(), [&](size_t a, size_t b) {
        return to_local_kebab_case(st.fields[a].name) < to_local_kebab_case(st.fields[b].name);
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
        const std::string key = to_local_kebab_case(field.name);

        LocalId field_val =
            mir_func->add_local("_field" + std::to_string(order_index), field.type, true, false);
        auto field_place = MirPlace(self_local, {PlaceProjection::field(field_order[order_index])});
        block->statements.push_back(MirStatement::assign(
            MirPlace(field_val), MirRvalue::use(MirOperand::copy(field_place))));

        // Bool型は特殊処理（trueの場合のみ出力）
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

    ctx_.impl_info[st.name]["Css"] = func_name;
    ctx_.program.functions.push_back(std::move(mir_func));
}

// ============================================================
// 組み込みto_cssメソッドの自動実装（css()のエイリアス）
// __to_css は __css を呼び出すラッパー関数
// ============================================================
void AutoImplGenerator::generate_builtin_to_css_method(const hir::HirStruct& st) {
    std::string func_name = st.name + "__to_css";
    std::string css_func_name = st.name + "__css";

    auto mir_func = std::make_unique<MirFunction>();
    mir_func->name = func_name;

    auto struct_type = hir::make_named(st.name);
    mir_func->return_local = mir_func->add_local("_0", hir::make_string(), true, false);

    LocalId self_local = mir_func->add_local("self", struct_type, false, true);
    mir_func->arg_locals.push_back(self_local);

    BlockId entry_block = mir_func->add_block();
    auto* block = mir_func->get_block(entry_block);

    // __css(self) を呼び出して結果を返す
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

    ctx_.program.functions.push_back(std::move(mir_func));
}

// ============================================================
// 組み込みisCssメソッドの自動実装
// ============================================================
void AutoImplGenerator::generate_builtin_is_css_method(const hir::HirStruct& st) {
    std::string func_name = st.name + "__isCss";

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

    ctx_.impl_info[st.name]["Css"] = func_name;
    ctx_.program.functions.push_back(std::move(mir_func));
}

// ============================================================
// モノモーフィゼーション版Debug
// ============================================================
void AutoImplGenerator::generate_builtin_debug_method_for_monomorphized(const MirStruct& st) {
    std::string func_name = st.name + "__debug";

    for (const auto& func : ctx_.program.functions) {
        if (func && func->name == func_name)
            return;
    }

    // 簡略化: モノモーフィ版はフィールドなし扱い
    auto mir_func = std::make_unique<MirFunction>();
    mir_func->name = func_name;

    auto struct_type = hir::make_named(st.name);
    mir_func->return_local = mir_func->add_local("_0", hir::make_string(), true, false);

    LocalId self_local = mir_func->add_local("self", struct_type, false, true);
    mir_func->arg_locals.push_back(self_local);

    BlockId entry_block = mir_func->add_block();
    auto* block = mir_func->get_block(entry_block);

    auto const_name = std::make_unique<MirOperand>();
    const_name->kind = MirOperand::Constant;
    MirConstant c;
    c.value = st.name + " { ... }";
    c.type = hir::make_string();
    const_name->data = c;
    block->statements.push_back(MirStatement::assign(MirPlace(mir_func->return_local),
                                                     MirRvalue::use(std::move(const_name))));

    block->terminator = MirTerminator::return_value();

    ctx_.impl_info[st.name]["Debug"] = func_name;
    ctx_.program.functions.push_back(std::move(mir_func));
}

// ============================================================
// モノモーフィゼーション版Display
// ============================================================
void AutoImplGenerator::generate_builtin_display_method_for_monomorphized(const MirStruct& st) {
    std::string func_name = st.name + "__toString";

    for (const auto& func : ctx_.program.functions) {
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

    auto const_name = std::make_unique<MirOperand>();
    const_name->kind = MirOperand::Constant;
    MirConstant c;
    c.value = std::string("(...)");
    c.type = hir::make_string();
    const_name->data = c;
    block->statements.push_back(MirStatement::assign(MirPlace(mir_func->return_local),
                                                     MirRvalue::use(std::move(const_name))));

    block->terminator = MirTerminator::return_value();

    ctx_.impl_info[st.name]["Display"] = func_name;
    ctx_.program.functions.push_back(std::move(mir_func));
}

// ============================================================
// モノモーフィゼーション版Css
// ============================================================
void AutoImplGenerator::generate_builtin_css_method_for_monomorphized(const MirStruct& st) {
    std::string func_name = st.name + "__css";

    for (const auto& func : ctx_.program.functions) {
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

    auto const_empty = std::make_unique<MirOperand>();
    const_empty->kind = MirOperand::Constant;
    MirConstant c;
    c.value = std::string();
    c.type = hir::make_string();
    const_empty->data = c;
    block->statements.push_back(MirStatement::assign(MirPlace(mir_func->return_local),
                                                     MirRvalue::use(std::move(const_empty))));

    block->terminator = MirTerminator::return_value();

    ctx_.impl_info[st.name]["Css"] = func_name;
    ctx_.program.functions.push_back(std::move(mir_func));
}

// ============================================================
// モノモーフィゼーション版to_css（css()のエイリアス）
// ============================================================
void AutoImplGenerator::generate_builtin_to_css_method_for_monomorphized(const MirStruct& st) {
    std::string func_name = st.name + "__to_css";
    std::string css_func_name = st.name + "__css";

    for (const auto& func : ctx_.program.functions) {
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

    // __css(self) を呼び出して結果を返す
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

    ctx_.program.functions.push_back(std::move(mir_func));
}

// ============================================================
// モノモーフィゼーション版isCss
// ============================================================
void AutoImplGenerator::generate_builtin_is_css_method_for_monomorphized(const MirStruct& st) {
    std::string func_name = st.name + "__isCss";

    for (const auto& func : ctx_.program.functions) {
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

    ctx_.impl_info[st.name]["Css"] = func_name;
    ctx_.program.functions.push_back(std::move(mir_func));
}

}  // namespace cm::mir
