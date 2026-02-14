#pragma once

#include "../core/base.hpp"

#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace cm::mir::opt {

// ============================================================
// デッドストア除去（Dead Store Elimination）- ローカル版
// ============================================================
class DeadStoreElimination : public OptimizationPass {
   public:
    std::string name() const override { return "Dead Store Elimination"; }

    bool run(MirFunction& func) override;

   private:
    // ブロック処理
    bool process_block(BasicBlock& block, const MirFunction& func);

    // 使用変数の収集
    void collect_uses(const MirStatement& stmt, std::unordered_set<LocalId>& used,
                      bool& uses_deref);
    void collect_rvalue_uses(const MirRvalue& rvalue, std::unordered_set<LocalId>& used,
                             bool& uses_deref);
    void collect_operand_uses(const MirOperand& op, std::unordered_set<LocalId>& used,
                              bool& uses_deref);
    void collect_terminator_uses(const MirTerminator& term, std::unordered_set<LocalId>& used,
                                 bool& uses_deref);

    // 副作用チェック
    bool has_side_effects(const MirRvalue& rvalue) const;
};

}  // namespace cm::mir::opt
