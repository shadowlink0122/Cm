#pragma once

#include "internal/mir/analysis/dominators.hpp"
#include "internal/mir/analysis/loop.hpp"
#include "internal/mir/nodes.hpp"
#include "internal/mir/passes/core/base.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace cm::mir::opt {

// ============================================================
// ループ不変式外移動（Loop Invariant Code Motion）
// ============================================================
class LoopInvariantCodeMotion : public OptimizationPass {
   public:
    std::string name() const override { return "LoopInvariantCodeMotion"; }

    bool run(MirFunction& func) override;

   private:
    bool process_loop(MirFunction& func, cm::mir::Loop* loop);
    BlockId get_or_create_pre_header(MirFunction& func, cm::mir::Loop* loop);
    bool is_invariant(const MirRvalue& rvalue, const std::set<LocalId>& modified_locals);
    bool is_invariant(const MirOperand& operand, const std::set<LocalId>& modified_locals);

    // 現在処理中の関数（is_invariantでグローバル/静的変数を判定するため。W4）
    const MirFunction* current_func_ = nullptr;
    bool has_memory_access(const MirRvalue& rvalue);
};

}  // namespace cm::mir::opt
