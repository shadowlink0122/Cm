// expr.cpp - 式のLowering エントリーポイント
// lower_expression: HIR式のvariant visitからloweringメソッドへディスパッチ

#include "expr.hpp"

#include "../../common/debug.hpp"

namespace cm::mir {

// 式のlowering（結果を一時変数に格納して返す）
LocalId ExprLowering::lower_expression(const hir::HirExpr& expr, LoweringContext& ctx) {
    // variant訪問用のvisitor
    return std::visit(
        [&](const auto& expr_ptr) -> LocalId {
            using T = std::decay_t<decltype(expr_ptr)>;

            if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirLiteral>>) {
                return lower_literal(*expr_ptr, expr.type, ctx);
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
                return ctx.new_temp(hir::make_error());
            } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirCast>>) {
                return lower_cast(*expr_ptr, ctx);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirEnumConstruct>>) {
                return lower_enum_construct(*expr_ptr, ctx);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirEnumPayload>>) {
                return lower_enum_payload(*expr_ptr, ctx);
            } else {
                // 未実装の式種別
                return ctx.new_temp(hir::make_error());
            }
        },
        expr.kind);
}

}  // namespace cm::mir
