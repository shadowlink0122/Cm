#pragma once

#include "base.hpp"
#include "expr.hpp"

namespace cm::mir {

// 前方宣言
class ExprLowering;

// ============================================================
// 文のLowering
// HIRの各種文をMIRに変換
// ============================================================
class StmtLowering : public MirLoweringBase {
   private:
    ExprLowering* expr_lowering;

   public:
    void set_expr_lowering(ExprLowering* el) { expr_lowering = el; }

    // 文のlowering
    void lower_statement(const hir::HirStmt& stmt, LoweringContext& ctx) {
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
                }
            },
            stmt.kind);
    }

   private:
    // let文のlowering
    void lower_let(const hir::HirLet& let, LoweringContext& ctx);

    // 代入文のlowering
    void lower_assign(const hir::HirAssign& assign, LoweringContext& ctx);

    // return文のlowering
    void lower_return(const hir::HirReturn& ret, LoweringContext& ctx);

    // if文のlowering
    void lower_if(const hir::HirIf& if_stmt, LoweringContext& ctx);

    // while文のlowering
    void lower_while(const hir::HirWhile& while_stmt, LoweringContext& ctx);

    // for文のlowering
    void lower_for(const hir::HirFor& for_stmt, LoweringContext& ctx);

    // loop文のlowering
    void lower_loop(const hir::HirLoop& loop_stmt, LoweringContext& ctx);

    // switch文のlowering
    void lower_switch(const hir::HirSwitch& switch_stmt, LoweringContext& ctx);

    // defer文のlowering
    void lower_defer(const hir::HirDefer& defer_stmt, LoweringContext& ctx);

    // ブロック文のlowering
    void lower_block(const hir::HirBlock& block, LoweringContext& ctx);

    // スコープ終了時のデストラクタ呼び出しを生成
    void emit_scope_destructors(LoweringContext& ctx);

    // インラインアセンブリのlowering
    void lower_asm(const hir::HirAsm& asm_stmt, LoweringContext& ctx);
};

}  // namespace cm::mir