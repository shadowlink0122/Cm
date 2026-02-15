#pragma once

#include "../core/base.hpp"

#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace cm::mir::opt {

// ============================================================
// 共通部分式削除 (CSE) / Global Value Numbering (GVN)
// 現在はLocal CSEのみ実装
// ============================================================
class GVN : public OptimizationPass {
   public:
    std::string name() const override { return "GVN/CSE"; }

    bool run(MirFunction& func) override;

   private:
    bool process_block(BasicBlock& block);

    void invalidate_exprs_using(
        LocalId local, std::unordered_map<std::string, LocalId>& available_exprs,
        std::unordered_map<LocalId, std::unordered_set<std::string>>& var_to_exprs);

    void collect_dependencies(const MirRvalue& rvalue, std::unordered_set<LocalId>& deps);
    void collect_operand_deps(const MirOperand& op, std::unordered_set<LocalId>& deps);
    std::string stringify_rvalue(const MirRvalue& rvalue);
    std::string stringify_operand(const MirOperand& op);
};

}  // namespace cm::mir::opt
