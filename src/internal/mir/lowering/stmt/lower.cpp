// MIR lowering - 文のディスパッチ（HirStmt variantを各lower_*へ振り分け）

#include "internal/base/debug.hpp"
#include "internal/mir/lowering/stmt.hpp"
#include "internal/mir/passes/scalar/const_eval.hpp"

#include <cinttypes>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cm::mir {

// 文のlowering（variant dispatch）
void StmtLowering::lower_statement(const hir::HirStmt& stmt, LoweringContext& ctx) {
    // variant訪問用のvisitor
    std::visit(
        [&](const auto& stmt_ptr) {
            using T = std::decay_t<decltype(stmt_ptr)>;

            if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirLet>>) {
                lower_let(*stmt_ptr, ctx);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirAssign>>) {
                lower_assign(*stmt_ptr, ctx);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirReturn>>) {
                lower_return(*stmt_ptr, ctx);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirIf>>) {
                lower_if(*stmt_ptr, ctx);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirWhile>>) {
                lower_while(*stmt_ptr, ctx);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirFor>>) {
                lower_for(*stmt_ptr, ctx);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirLoop>>) {
                lower_loop(*stmt_ptr, ctx);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirSwitch>>) {
                lower_switch(*stmt_ptr, ctx);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirBlock>>) {
                lower_block(*stmt_ptr, ctx);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirBreak>>) {
                // break文の処理
                if (auto* loop = ctx.current_loop()) {
                    // defer文を実行してからbreak
                    auto defers = ctx.get_defer_stmts();
                    for (const auto* defer_stmt : defers) {
                        lower_statement(*defer_stmt, ctx);
                    }
                    auto term = MirTerminator::goto_block(loop->exit);
                    ctx.set_terminator(std::move(term));
                    ctx.switch_to_block(ctx.new_block());
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirContinue>>) {
                // continue文の処理
                if (auto* loop = ctx.current_loop()) {
                    // defer文を実行してからcontinue
                    auto defers = ctx.get_defer_stmts();
                    for (const auto* defer_stmt : defers) {
                        lower_statement(*defer_stmt, ctx);
                    }
                    // forループの場合は更新ブロックへ、whileループの場合はヘッダーへ
                    auto term = MirTerminator::goto_block(loop->update);
                    ctx.set_terminator(std::move(term));
                    ctx.switch_to_block(ctx.new_block());
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirDefer>>) {
                lower_defer(*stmt_ptr, ctx);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirExprStmt>>) {
                // 式文
                if (stmt_ptr->expr) {
                    expr_lowering->lower_expression(*stmt_ptr->expr, ctx);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirAsm>>) {
                // インラインアセンブリ
                lower_asm(*stmt_ptr, ctx);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<hir::HirMustBlock>>) {
                // must {} ブロック（最適化禁止）
                lower_must_block(*stmt_ptr, ctx);
            }
        },
        stmt.kind);
}

}  // namespace cm::mir
