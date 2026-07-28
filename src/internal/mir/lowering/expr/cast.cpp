// MIR lowering - 変換式（文字列変換ヘルパーとキャスト）

#include "internal/base/debug.hpp"
#include "internal/mir/lowering/expr.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cm::mir {

// 値を文字列に変換するヘルパー（文字列連結用）
LocalId ExprLowering::convert_to_string(LocalId value, const hir::TypePtr& type,
                                        LoweringContext& ctx) {
    // 変換関数名を型に基づいて決定
    std::string conv_func;
    if (!type) {
        conv_func = "cm_int_to_string";  // デフォルト
    } else {
        switch (type->kind) {
            case hir::TypeKind::Int:
            case hir::TypeKind::Short:
            case hir::TypeKind::Tiny:
                conv_func = "cm_int_to_string";
                break;
            case hir::TypeKind::Long:
            case hir::TypeKind::ISize:
                // 64ビット値はi32関数へ渡すとtruncで壊れるため専用関数を使う
                conv_func = "cm_long_to_string";
                break;
            case hir::TypeKind::UInt:
            case hir::TypeKind::UShort:
            case hir::TypeKind::UTiny:
                conv_func = "cm_uint_to_string";
                break;
            case hir::TypeKind::ULong:
            case hir::TypeKind::USize:
                conv_func = "cm_ulong_to_string";
                break;
            case hir::TypeKind::Float:
            case hir::TypeKind::Double:
                conv_func = "cm_double_to_string";
                break;
            case hir::TypeKind::Bool:
                conv_func = "cm_bool_to_string";
                break;
            case hir::TypeKind::Char:
                conv_func = "cm_char_to_string";
                break;
            case hir::TypeKind::String:
                // 既に文字列なので変換不要
                return value;
            default:
                conv_func = "cm_int_to_string";  // フォールバック
                break;
        }
    }

    // 変換関数を呼び出す
    LocalId str_result = ctx.new_temp(hir::make_string());
    std::vector<MirOperandPtr> conv_args;
    conv_args.push_back(MirOperand::copy(MirPlace{value}));

    BlockId conv_success = ctx.new_block();

    auto conv_func_operand = MirOperand::function_ref(conv_func);

    auto conv_call_term = std::make_unique<MirTerminator>();
    conv_call_term->kind = MirTerminator::Call;
    conv_call_term->data = MirTerminator::CallData{std::move(conv_func_operand),
                                                   std::move(conv_args),
                                                   MirPlace{str_result},
                                                   conv_success,
                                                   std::nullopt,
                                                   "",
                                                   "",
                                                   false};  // 通常の関数呼び出し
    ctx.set_terminator(std::move(conv_call_term));
    ctx.switch_to_block(conv_success);

    // 変換結果は新規確保された無名一時。文末のdropパス対象として登録する（C12）
    ctx.note_string_temp(str_result);

    return str_result;
}

// キャスト式のlowering
LocalId ExprLowering::lower_cast(const hir::HirCast& cast, LoweringContext& ctx) {
    debug_msg("MIR", "Lowering cast expression");

    // オペランドをlowering
    LocalId operand = lower_expression(*cast.operand, ctx);

    // typedefエイリアス（Shape = Circle | Rect 等）を解決してからCastを発行する。
    // 未解決のままだとバックエンドがユニオン構築/タグ検査を認識できない
    hir::TypePtr target_type = ctx.resolve_typedef(cast.target_type);
    if (!target_type) {
        target_type = cast.target_type;
    }

    // ユニオン型の実行時型判別 (expr is Type): タグ比較のboolを返す
    if (cast.check_only) {
        LocalId result = ctx.new_temp(hir::make_bool());
        ctx.push_statement(MirStatement::assign(
            MirPlace{result}, MirRvalue::cast(MirOperand::copy(MirPlace{operand}), target_type,
                                              /*check_only=*/true)));
        return result;
    }

    // 配列→ポインタ型キャストの場合、array-to-pointer decay（暗黙的Ref）を挿入
    // Bug#9修正: パーサーは &b as void* を &(b as void*) として解析する
    // b as void* で配列全体がコピーされるのを防ぐため、配列のアドレスを取得してからポインタキャストを行う
    if (target_type &&
        (target_type->kind == hir::TypeKind::Pointer ||
         target_type->kind == hir::TypeKind::Reference) &&
        operand < ctx.func->locals.size()) {
        auto& operand_local = ctx.func->locals[operand];
        if (operand_local.type && operand_local.type->kind == hir::TypeKind::Array &&
            operand_local.type->array_size.has_value()) {
            // 固定サイズ配列→ポインタ: 暗黙的に&arrを挿入
            hir::TypePtr ptr_type = hir::make_pointer(operand_local.type);
            LocalId ref_temp = ctx.new_temp(ptr_type);

            auto ref_rvalue = std::make_unique<MirRvalue>();
            ref_rvalue->kind = MirRvalue::Ref;
            ref_rvalue->data = MirRvalue::RefData{BorrowKind::Mutable, MirPlace{operand}};

            ctx.push_statement(MirStatement::assign(MirPlace{ref_temp}, std::move(ref_rvalue)));

            // Ref結果をポインタキャスト
            LocalId result = ctx.new_temp(target_type);
            ctx.push_statement(MirStatement::assign(
                MirPlace{result},
                MirRvalue::cast(MirOperand::copy(MirPlace{ref_temp}), target_type)));

            return result;
        }
    }

    // ターゲット型で結果変数を作成
    LocalId result = ctx.new_temp(target_type);

    // キャスト命令を生成
    ctx.push_statement(MirStatement::assign(
        MirPlace{result}, MirRvalue::cast(MirOperand::copy(MirPlace{operand}), target_type)));

    return result;
}

}  // namespace cm::mir
