#pragma once

#include "../core/base.hpp"

#include <set>
#include <unordered_map>
#include <unordered_set>

namespace cm::mir::opt {

// ============================================================
// コピー伝播最適化
// ============================================================
class CopyPropagation : public OptimizationPass {
   public:
    std::string name() const override { return "Copy Propagation"; }

    bool run(MirFunction& func) override;

   private:
    // 複数回代入される変数を検出
    std::unordered_set<LocalId> detect_multi_assigned(const MirFunction& func);

    // 型比較
    bool same_type(const hir::TypePtr& a, const hir::TypePtr& b) const;

    // ブロック処理
    bool process_block(BasicBlock& block, std::unordered_map<LocalId, LocalId>& copies,
                       const MirFunction& func, const std::unordered_set<LocalId>& multiAssigned);

    // コピーチェーン解決
    LocalId resolve_copy_chain(LocalId local, const std::unordered_map<LocalId, LocalId>& copies);

    // コピー伝播
    bool propagate_in_rvalue(MirRvalue& rvalue, const std::unordered_map<LocalId, LocalId>& copies);
    bool propagate_in_operand(MirOperand& operand,
                              const std::unordered_map<LocalId, LocalId>& copies);
    bool propagate_in_place(MirPlace& place, const std::unordered_map<LocalId, LocalId>& copies);
    bool propagate_in_terminator(MirTerminator& term,
                                 const std::unordered_map<LocalId, LocalId>& copies);
};

}  // namespace cm::mir::opt