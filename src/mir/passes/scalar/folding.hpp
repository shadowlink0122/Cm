#pragma once

#include "../core/base.hpp"

#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace cm::mir::opt {

// ============================================================
// 定数畳み込み最適化
// ============================================================
class ConstantFolding : public OptimizationPass {
   public:
    std::string name() const override { return "Constant Folding"; }

    bool run(MirFunction& func) override;

   private:
    // 複数回代入される変数を検出
    std::unordered_set<LocalId> detect_multi_assigned(const MirFunction& func);

    // ブロック処理
    bool process_block(const MirFunction& func, BasicBlock& block,
                       std::unordered_map<LocalId, MirConstant>& constants,
                       const std::unordered_set<LocalId>& multiAssigned);

    // Rvalue/Operand評価
    std::optional<MirConstant> evaluate_rvalue(
        const MirRvalue& rvalue, const std::unordered_map<LocalId, MirConstant>& constants);
    std::optional<MirConstant> evaluate_operand(
        const MirOperand& operand, const std::unordered_map<LocalId, MirConstant>& constants);

    // 演算評価
    std::optional<MirConstant> eval_binary_op(MirBinaryOp op, const MirConstant& lhs,
                                              const MirConstant& rhs,
                                              const hir::TypePtr& result_type = nullptr);
    std::optional<MirConstant> eval_unary_op(MirUnaryOp op, const MirConstant& operand);
    std::optional<MirConstant> eval_cast(const MirConstant& operand,
                                         const hir::TypePtr& target_type);
};

}  // namespace cm::mir::opt