#pragma once

#include "../../nodes.hpp"
#include "../core/base.hpp"

#include <memory>
#include <string>
#include <vector>

namespace cm::mir::opt {

// ============================================================
// 末尾再帰最適化（Tail Call Elimination）
// ============================================================
class TailCallElimination : public OptimizationPass {
   public:
    std::string name() const override { return "TailCallElimination"; }

    bool run(MirFunction& func) override;

   private:
    // 自己呼び出しかどうかチェック
    bool is_self_call(const MirFunction& func, const MirTerminator::CallData& call_data);

    // 末尾位置かどうかチェック
    bool is_tail_position(const MirFunction& func, const MirTerminator::CallData& call_data);

    // 末尾再帰をループに変換
    bool transform_to_loop(MirFunction& func, BasicBlock& call_block,
                           MirTerminator::CallData& call_data);

    // オペランドをクローン
    MirOperandPtr clone_operand(const MirOperand& src);
};

}  // namespace cm::mir::opt
