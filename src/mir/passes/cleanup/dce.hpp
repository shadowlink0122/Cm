#pragma once

#include "../../../common/debug.hpp"
#include "../core/base.hpp"

#include <algorithm>
#include <queue>
#include <set>

namespace cm::mir::opt {

// ============================================================
// デッドコード除去最適化
// ============================================================
class DeadCodeElimination : public OptimizationPass {
   public:
    std::string name() const override { return "Dead Code Elimination"; }

    bool run(MirFunction& func) override;

   private:
    // 到達不可能ブロックを除去
    bool remove_unreachable_blocks(MirFunction& func);

    // 使用されていないローカル変数への代入を除去
    bool remove_dead_stores(MirFunction& func);

    // 副作用のない未使用文を除去
    bool remove_dead_statements(MirFunction& func);

    // 使用されているローカル変数を収集
    void collect_used_locals(const MirFunction& func, std::set<LocalId>& used);
    void collect_used_locals_in_statement(const MirStatement& stmt, std::set<LocalId>& used);
    void collect_used_locals_in_rvalue(const MirRvalue& rvalue, std::set<LocalId>& used);
    void collect_used_locals_in_operand(const MirOperand& op, std::set<LocalId>& used);
    void collect_used_locals_in_terminator(const MirTerminator& term, std::set<LocalId>& used);

    // 副作用チェック
    bool has_side_effects(const MirRvalue* rvalue) const;
};

}  // namespace cm::mir::opt
