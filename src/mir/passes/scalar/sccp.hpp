#pragma once

#include "../core/base.hpp"

#include <optional>
#include <queue>
#include <vector>

namespace cm::mir::opt {

// ============================================================
// 疎条件定数伝播（SCCP）
// ============================================================
class SparseConditionalConstantPropagation : public OptimizationPass {
   public:
    std::string name() const override { return "Sparse Conditional Constant Propagation"; }

    bool run(MirFunction& func) override;

   private:
    enum class LatticeKind { Undefined, Constant, Overdefined };

    struct LatticeValue {
        LatticeKind kind = LatticeKind::Undefined;
        MirConstant constant{};
    };

    // CFG再構築
    void rebuild_cfg(MirFunction& func);

    // 型比較
    bool same_type(const hir::TypePtr& a, const hir::TypePtr& b) const;
    bool equal_constant(const MirConstant& a, const MirConstant& b) const;
    bool equal_value(const LatticeValue& a, const LatticeValue& b) const;

    // ラティス演算
    LatticeValue meet(const LatticeValue& a, const LatticeValue& b) const;

    // データフロー解析
    void analyze(const MirFunction& func, std::vector<std::vector<LatticeValue>>& in_states,
                 std::vector<std::vector<LatticeValue>>& out_states, std::vector<bool>& reachable);
    std::vector<LatticeValue> merge_predecessors(
        const MirFunction& func, BlockId block_id,
        const std::vector<std::vector<LatticeValue>>& out_states,
        const std::vector<bool>& reachable);
    std::vector<LatticeValue> transfer_block(const MirFunction& func, const BasicBlock& block,
                                             const std::vector<LatticeValue>& in_state);
    std::vector<BlockId> compute_successors(const MirFunction& func, const BasicBlock& block,
                                            const std::vector<LatticeValue>& state);
    bool states_equal(const std::vector<LatticeValue>& a, const std::vector<LatticeValue>& b) const;

    // 評価
    LatticeValue eval_operand(const MirFunction& func, const MirOperand& operand,
                              const std::vector<LatticeValue>& state);
    LatticeValue eval_rvalue(const MirFunction& func, const MirRvalue& rvalue,
                             const std::vector<LatticeValue>& state);
    bool can_bind_constant(const MirFunction& func, LocalId local,
                           const MirConstant& constant) const;
    std::optional<MirConstant> eval_binary_op(MirBinaryOp op, const MirConstant& lhs,
                                              const MirConstant& rhs);
    std::optional<MirConstant> eval_unary_op(MirUnaryOp op, const MirConstant& operand);

    // 定数適用・書き換え
    bool apply_constants(MirFunction& func,
                         const std::vector<std::vector<LatticeValue>>& in_states);
    bool rewrite_rvalue(const MirFunction& func, MirRvalue& rvalue,
                        const std::vector<LatticeValue>& state);
    bool rewrite_terminator(const MirFunction& func, MirTerminator& term,
                            const std::vector<LatticeValue>& state);
    bool rewrite_operand(const MirFunction& func, MirOperand& operand,
                         const std::vector<LatticeValue>& state);
    bool replace_with_constant(MirRvaluePtr& rvalue, const MirConstant& constant);

    // CFG簡略化
    std::optional<BlockId> simplify_switch(const MirFunction& func,
                                           const MirTerminator::SwitchIntData& switch_data,
                                           const std::vector<LatticeValue>& state);
    bool simplify_cfg(MirFunction& func);
    bool remove_unreachable_blocks(MirFunction& func);
};

}  // namespace cm::mir::opt
