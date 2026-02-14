#pragma once

#include "../core/base.hpp"

#include <queue>
#include <unordered_set>
#include <vector>

namespace cm::mir::opt {

// ============================================================
// CFG簡約化 (Control Flow Graph Simplification)
// ============================================================
class SimplifyControlFlow : public OptimizationPass {
   public:
    std::string name() const override { return "Simplify Control Flow"; }

    bool run(MirFunction& func) override;

   private:
    bool remove_unreachable(MirFunction& func);
    bool merge_blocks(MirFunction& func);
    bool remove_empty_blocks(MirFunction& func);
    void redirect_jumps(MirTerminator& term, BlockId from, BlockId to);
};

}  // namespace cm::mir::opt
