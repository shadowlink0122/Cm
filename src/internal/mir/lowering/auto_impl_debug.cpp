// ============================================================
// 自動実装 - Debug/Display メソッドの生成
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

void MirLowering::generate_builtin_debug_method(const hir::HirStruct& st) {
    std::string func_name = st.name + "__debug";

    auto mir_func = std::make_unique<MirFunction>();
    mir_func->name = func_name;

    auto struct_type = hir::make_named(st.name);

    // 戻り値: string (_0)
    mir_func->return_local = mir_func->add_local("_0", hir::make_string(), true, false);

    // 引数: self (値)
    LocalId self_local = mir_func->add_local("self", struct_type, false, true);
    mir_func->arg_locals.push_back(self_local);

    // エントリブロック
    BlockId entry_block = mir_func->add_block();
    auto* block = mir_func->get_block(entry_block);

    // 結果文字列を構築: "TypeName { f1: v1, f2: v2, ... }"
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

        // フィールド値を取得して文字列に変換
        LocalId field_val =
            mir_func->add_local("_field" + std::to_string(i), field.type, true, false);
        auto field_place = MirPlace(self_local, {PlaceProjection::field(i)});
        block->statements.push_back(MirStatement::assign(
            MirPlace(field_val), MirRvalue::use(MirOperand::copy(field_place))));

        // フィールド値を文字列に変換（型に応じた組み込み関数を呼び出し）
        LocalId field_str =
            mir_func->add_local("_fstr" + std::to_string(i), hir::make_string(), true, false);

        // 型に応じた変換関数を決定
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
            // 文字列はそのままコピー
            block->statements.push_back(MirStatement::assign(
                MirPlace(field_str), MirRvalue::use(MirOperand::copy(MirPlace(field_val)))));
            convert_func = "";  // 変換不要
        } else if (field.type->kind == hir::TypeKind::Char) {
            convert_func = "cm_format_char";
        } else if (field.type->kind == hir::TypeKind::Struct) {
            // ネストした構造体: 再帰的にdebug()を呼び出す
            convert_func = field.type->name + "__debug";
        } else {
            // その他の型は整数として扱う
            convert_func = "cm_format_int";
        }

        if (!convert_func.empty()) {
            // 変換関数呼び出し
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

    // 戻り値に設定
    block->statements.push_back(
        MirStatement::assign(MirPlace(mir_func->return_local),
                             MirRvalue::use(MirOperand::copy(MirPlace(final_result)))));

    block->terminator = MirTerminator::return_value();

    impl_info[st.name]["Debug"] = func_name;
    mir_program.functions.push_back(std::move(mir_func));
}

// Display自動実装: "(field1, field2, ...)" 形式
void MirLowering::generate_builtin_display_method(const hir::HirStruct& st) {
    std::string func_name = st.name + "__toString";

    auto mir_func = std::make_unique<MirFunction>();
    mir_func->name = func_name;

    auto struct_type = hir::make_named(st.name);

    // 戻り値: string (_0)
    mir_func->return_local = mir_func->add_local("_0", hir::make_string(), true, false);

    // 引数: self (値)
    LocalId self_local = mir_func->add_local("self", struct_type, false, true);
    mir_func->arg_locals.push_back(self_local);

    // エントリブロック
    BlockId entry_block = mir_func->add_block();
    auto* block = mir_func->get_block(entry_block);

    // 結果文字列を構築: "(v1, v2, ...)"
    std::string initial_str = "(";
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

        // フィールド値を取得して文字列に変換
        LocalId field_val =
            mir_func->add_local("_field" + std::to_string(i), field.type, true, false);
        auto field_place = MirPlace(self_local, {PlaceProjection::field(i)});
        block->statements.push_back(MirStatement::assign(
            MirPlace(field_val), MirRvalue::use(MirOperand::copy(field_place))));

        // フィールド値を文字列に変換
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

        // result = result + field_str
        LocalId concat =
            mir_func->add_local("_concat" + std::to_string(i), hir::make_string(), true, false);
        block->statements.push_back(MirStatement::assign(
            MirPlace(concat),
            MirRvalue::binary(MirBinaryOp::Add, MirOperand::copy(MirPlace(result)),
                              MirOperand::copy(MirPlace(field_str)))));
        result = concat;

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

            LocalId concat2 = mir_func->add_local("_concat2_" + std::to_string(i),
                                                  hir::make_string(), true, false);
            block->statements.push_back(MirStatement::assign(
                MirPlace(concat2),
                MirRvalue::binary(MirBinaryOp::Add, MirOperand::copy(MirPlace(result)),
                                  MirOperand::copy(MirPlace(sep_str)))));
            result = concat2;
        }
    }

    // 末尾の ")" を追加
    std::string closing = ")";
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

    impl_info[st.name]["Display"] = func_name;
    mir_program.functions.push_back(std::move(mir_func));
}

}  // namespace cm::mir
