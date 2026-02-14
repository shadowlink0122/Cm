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
    void lower_statement(const hir::HirStmt& stmt, LoweringContext& ctx);

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

    // must {} ブロックのlowering
    void lower_must_block(const hir::HirMustBlock& must_block, LoweringContext& ctx);
};

}  // namespace cm::mir