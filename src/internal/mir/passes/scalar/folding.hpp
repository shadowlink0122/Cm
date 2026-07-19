#pragma once

#include "internal/mir/passes/core/base.hpp"

#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace cm::mir::opt {

// ============================================================
// 定数畳み込み最適化
// ============================================================
class ConstantFolding : public OptimizationPass {
   public:
    // fold_terminators=false は文の書き換えのみ行い、SwitchInt→Gotoの制御フロー変更を行わない（SVバックエンド等、CFG形状を保ちたい用途向け）
    explicit ConstantFolding(bool fold_terminators = true) : fold_terminators_(fold_terminators) {}

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

    // 代数的恒等式の簡約（x*1, x+0, x*0, x%1, x>>0 等。整数型のみ）。
    // 文数を変えずrvalueをUse(オペランド)または定数へ書き換える
    bool simplify_identity(MirStatement::AssignData& assign_data,
                           const std::unordered_map<LocalId, MirConstant>& constants);

    bool fold_terminators_ = true;
};

}  // namespace cm::mir::opt