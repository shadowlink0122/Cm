#pragma once

#include "base.hpp"

namespace cm::mir {

// ============================================================
// 式のLowering
// HIRの各種式をMIRのローカル変数に変換
// ============================================================
class ExprLowering : public MirLoweringBase {
   public:
    // 式のlowering（結果を一時変数に格納して返す）
    LocalId lower_expression(const hir::HirExpr& expr, LoweringContext& ctx) {
        // variant訪問用のvisitor
        return std::visit(
            [&](const auto& expr_ptr) -> LocalId {
                using T = std::decay_t<decltype(expr_ptr)>;

                if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirLiteral>>) {
                    return lower_literal(*expr_ptr, ctx);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirVarRef>>) {
                    return lower_var_ref(*expr_ptr, expr.type, ctx);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirBinary>>) {
                    return lower_binary(*expr_ptr, ctx);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirUnary>>) {
                    return lower_unary(*expr_ptr, ctx);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirCall>>) {
                    return lower_call(*expr_ptr, expr.type, ctx);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirIndex>>) {
                    return lower_index(*expr_ptr, ctx);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirMember>>) {
                    return lower_member(*expr_ptr, ctx);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirTernary>>) {
                    return lower_ternary(*expr_ptr, ctx);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirStructLiteral>>) {
                    return lower_struct_literal(*expr_ptr, ctx);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirArrayLiteral>>) {
                    return lower_array_literal(*expr_ptr, expr.type, ctx);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirLambda>>) {
                    // ラムダはHIR段階で関数参照に変換されるため、ここには来ない
                    // 万が一来た場合はエラー
                    return ctx.new_temp(hir::make_error());
                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirCast>>) {
                    return lower_cast(*expr_ptr, ctx);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirEnumConstruct>>) {
                    return lower_enum_construct(*expr_ptr, ctx);
                } else {
                    // 未実装の式種別
                    return ctx.new_temp(hir::make_error());
                }
            },
            expr.kind);
    }

    // 各式タイプのlowering
    LocalId lower_literal(const hir::HirLiteral& lit, LoweringContext& ctx);
    LocalId lower_var_ref(const hir::HirVarRef& var, const hir::TypePtr& expr_type,
                          LoweringContext& ctx);
    LocalId lower_binary(const hir::HirBinary& bin, LoweringContext& ctx);
    LocalId lower_unary(const hir::HirUnary& un, LoweringContext& ctx);
    LocalId lower_call(const hir::HirCall& call, const hir::TypePtr& result_type,
                       LoweringContext& ctx);
    LocalId lower_index(const hir::HirIndex& idx, LoweringContext& ctx);
    LocalId lower_member(const hir::HirMember& mem, LoweringContext& ctx);
    LocalId lower_ternary(const hir::HirTernary& ternary, LoweringContext& ctx);
    LocalId lower_struct_literal(const hir::HirStructLiteral& lit, LoweringContext& ctx);
    LocalId lower_array_literal(const hir::HirArrayLiteral& lit, const hir::TypePtr& expected_type,
                                LoweringContext& ctx);
    LocalId lower_cast(const hir::HirCast& cast, LoweringContext& ctx);
    LocalId lower_enum_construct(const hir::HirEnumConstruct& ec, LoweringContext& ctx);

    // メンバアクセスからMirPlaceを取得（コピーせずに参照を取得）
    // 成功時はtrue、失敗時はfalseを返す
    bool get_member_place(const hir::HirMember& mem, LoweringContext& ctx, MirPlace& out_place,
                          hir::TypePtr& out_type);

    // 文字列補間の処理
    LocalId process_string_interpolation(const std::string& format_str,
                                         const std::vector<LocalId>& args, LoweringContext& ctx);

    // println/print用の特別処理
    LocalId handle_println_interpolation(const hir::HirCall& call, LoweringContext& ctx);

    // フォーマット文字列から名前付きプレースホルダを抽出
    std::pair<std::vector<std::string>, std::string> extract_named_placeholders(
        const std::string& format_str, LoweringContext& ctx);

   protected:
    // HIR二項演算子をMIRに変換
    MirBinaryOp convert_binary_op(hir::HirBinaryOp op);

    // HIR単項演算子をMIRに変換
    MirUnaryOp convert_unary_op(hir::HirUnaryOp op);

    // 値を文字列に変換するヘルパー（文字列連結用）
    LocalId convert_to_string(LocalId value, const hir::TypePtr& type, LoweringContext& ctx);
};

}  // namespace cm::mir