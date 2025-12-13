#pragma once

#include "constant_folding.hpp"
#include "copy_propagation.hpp"
#include "dead_code_elimination.hpp"
#include "optimization_pass.hpp"

#include <iostream>

namespace cm::mir::opt {

// 標準的な最適化パスを作成する関数
inline std::vector<std::unique_ptr<OptimizationPass>> create_standard_passes(
    int optimization_level) {
    std::vector<std::unique_ptr<OptimizationPass>> passes;

    if (optimization_level >= 1) {
        // -O1: 基本的な最適化
        passes.push_back(std::make_unique<ConstantFolding>());
        passes.push_back(std::make_unique<CopyPropagation>());
        passes.push_back(std::make_unique<DeadCodeElimination>());
    }

    if (optimization_level >= 2) {
        // -O2: より積極的な最適化
        // 最適化パスを複数回実行
        passes.push_back(std::make_unique<ConstantFolding>());
        passes.push_back(std::make_unique<CopyPropagation>());
        // TODO: インライン化、ループ最適化など
    }

    if (optimization_level >= 3) {
        // -O3: 最大限の最適化
        // TODO: ベクトル化、アンロールなど
    }

    return passes;
}

// ============================================================
// 簡易的なブロック統合最適化
// ============================================================
class SimplifyControlFlow : public OptimizationPass {
   public:
    std::string name() const override { return "Simplify Control Flow"; }

    bool run(MirFunction& func) override {
        bool changed = false;

        // 単一のGotoを持つブロックを統合
        for (size_t i = 0; i < func.basic_blocks.size(); ++i) {
            auto& block = func.basic_blocks[i];
            if (!block)
                continue;

            // ブロックが単一のGotoのみを持つ場合
            if (block->statements.empty() && block->terminator &&
                block->terminator->kind == MirTerminator::Goto) {
                auto& goto_data = std::get<MirTerminator::GotoData>(block->terminator->data);
                BlockId target = goto_data.target;

                // このブロックへのジャンプを直接targetへ変更
                for (auto& other_block : func.basic_blocks) {
                    if (!other_block || other_block == block)
                        continue;

                    if (other_block->terminator) {
                        changed |= redirect_jumps(*other_block->terminator, i, target);
                    }
                }
            }
        }

        if (changed) {
            func.build_cfg();
        }

        return changed;
    }

   private:
    bool redirect_jumps(MirTerminator& term, BlockId from, BlockId to) {
        bool changed = false;

        switch (term.kind) {
            case MirTerminator::Goto: {
                auto& goto_data = std::get<MirTerminator::GotoData>(term.data);
                if (goto_data.target == from) {
                    goto_data.target = to;
                    changed = true;
                }
                break;
            }
            case MirTerminator::SwitchInt: {
                auto& switch_data = std::get<MirTerminator::SwitchIntData>(term.data);
                for (auto& [value, target] : switch_data.targets) {
                    if (target == from) {
                        target = to;
                        changed = true;
                    }
                }
                if (switch_data.otherwise == from) {
                    switch_data.otherwise = to;
                    changed = true;
                }
                break;
            }
            case MirTerminator::Call: {
                auto& call_data = std::get<MirTerminator::CallData>(term.data);
                if (call_data.success == from) {
                    call_data.success = to;
                    changed = true;
                }
                if (call_data.unwind && *call_data.unwind == from) {
                    call_data.unwind = to;
                    changed = true;
                }
                break;
            }
            default:
                break;
        }

        return changed;
    }
};

}  // namespace cm::mir::opt