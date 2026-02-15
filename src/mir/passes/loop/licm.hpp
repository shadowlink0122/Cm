#pragma once

#include "../../analysis/dominators.hpp"
#include "../../analysis/loop_analysis.hpp"
#include "../../nodes.hpp"
#include "../core/base.hpp"

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
    bool has_memory_access(const MirRvalue& rvalue);
};

}  // namespace cm::mir::opt
