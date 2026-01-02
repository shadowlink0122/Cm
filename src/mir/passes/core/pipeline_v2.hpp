#pragma once

#include "../convergence/manager.hpp"
#include "base.hpp"

#include <iostream>

namespace cm::mir::opt {

// 改良版の最適化パイプライン
class OptimizationPipelineV2 : public OptimizationPipeline {
   private:
    bool debug_output = false;  // デバッグ出力用フラグ

    // インストラクション数をカウント
    int count_instructions(const MirProgram& program) const {
        int count = 0;
        for (const auto& func : program.functions) {
            if (!func)
                continue;
            for (const auto& block : func->basic_blocks) {
                if (!block)
                    continue;
                count += block->statements.size();
            }
        }
        return count;
    }

    // 基本ブロック数をカウント
    int count_blocks(const MirProgram& program) const {
        int count = 0;
        for (const auto& func : program.functions) {
            if (!func)
                continue;
            count += func->basic_blocks.size();
        }
        return count;
    }

   public:
    // デバッグ出力を有効化
    void enable_debug_output(bool enable = true) {
        debug_output = enable;
        OptimizationPipeline::enable_debug_output(enable);
    }

    // 収束するまで繰り返し実行（改良版）
    void run_until_fixpoint_v2(MirProgram& program, int max_iterations = 10) {
        using namespace cm::mir::optimizations;

        ConvergenceManager convergence_mgr;

        for (int i = 0; i < max_iterations; ++i) {
            ConvergenceManager::ChangeMetrics metrics;

            if (debug_output) {
                std::cout << "[OPT] 反復 " << (i + 1) << "/" << max_iterations << "\n";
            }

            // 実行前の状態を記録
            int prev_inst_count = count_instructions(program);
            int prev_block_count = count_blocks(program);

            // 一時的に変更検出用の仕組みを用意
            int before_count = count_instructions(program);
            OptimizationPipeline::run(program);
            int after_count = count_instructions(program);

            bool any_changed = (before_count != after_count);

            if (any_changed) {
                // 変更の影響度を計測
                int curr_inst_count = count_instructions(program);
                int curr_block_count = count_blocks(program);

                metrics.instructions_changed = std::abs(curr_inst_count - prev_inst_count);
                metrics.blocks_changed = std::abs(curr_block_count - prev_block_count);

                // CFG変更の可能性がある場合（ヒューリスティック）
                if (metrics.blocks_changed > 0) {
                    metrics.cfg_changed = true;
                }

                if (debug_output) {
                    std::cout << "[OPT]   変更を実行"
                              << " (inst: " << metrics.instructions_changed
                              << ", blocks: " << metrics.blocks_changed << ")\n";
                }
            }

            // 収束状態をチェック
            auto state = convergence_mgr.update_and_check(program, metrics);

            switch (state) {
                case ConvergenceManager::ConvergenceState::CONVERGED:
                    if (debug_output) {
                        std::cout << "[OPT] ✓ 完全収束: " << (i + 1) << " 回の反復で収束\n";
                    }
                    return;

                case ConvergenceManager::ConvergenceState::PRACTICALLY_CONVERGED:
                    if (debug_output) {
                        std::cout << "[OPT] ✓ 実用的収束: 軽微な変更のみ（反復 " << (i + 1)
                                  << "）\n";
                    }
                    return;

                case ConvergenceManager::ConvergenceState::CYCLE_DETECTED:
                    std::cerr << "[OPT] ⚠ 警告: 最適化の循環を検出しました\n";
                    if (debug_output) {
                        std::cerr << convergence_mgr.get_statistics();
                    }
                    return;

                case ConvergenceManager::ConvergenceState::NOT_CONVERGED:
                    // 継続
                    break;
            }
        }

        std::cerr << "[OPT] ⚠ 警告: 最大反復回数（" << max_iterations << "）に達しました\n";
        if (debug_output) {
            std::cerr << convergence_mgr.get_statistics();
        }
    }
};

}  // namespace cm::mir::opt
