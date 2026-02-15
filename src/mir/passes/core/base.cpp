#include "base.hpp"

#include <chrono>

namespace cm::mir::opt {

int OptimizationPipeline::count_instructions(const MirProgram& program) const {
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

int OptimizationPipeline::count_blocks(const MirProgram& program) const {
    int count = 0;
    for (const auto& func : program.functions) {
        if (!func)
            continue;
        count += func->basic_blocks.size();
    }
    return count;
}

void OptimizationPipeline::run(MirProgram& program) {
    if (debug_output) {
        std::cout << "[OPT] Starting optimization pipeline\n" << std::flush;
    }

    for (auto& pass : passes) {
        if (debug_output) {
            std::cout << "[OPT] Running pass: " << pass->name() << "\n" << std::flush;
        }

        bool changed = pass->run_on_program(program);

        if (debug_output) {
            std::cout << "[OPT] Pass " << pass->name()
                      << (changed ? " made changes" : " made no changes") << "\n"
                      << std::flush;
        }
    }

    if (debug_output) {
        std::cout << "[OPT] Optimization pipeline completed\n" << std::flush;
    }
}

void OptimizationPipeline::run_until_fixpoint(MirProgram& program, int max_iterations) {
    using namespace cm::mir::optimizations;

    ConvergenceManager convergence_mgr;

    // 個々の最適化パスの実行回数を追跡
    std::unordered_map<std::string, int> pass_run_counts;

    // 各パスが前回の反復で変更を生じたかを追跡
    // 変更なし + その後に他のパスも変更なし → スキップ可能
    std::vector<bool> pass_changed_last(passes.size(), true);  // 初回は全パス実行

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

        // 個々のパスごとの実行回数制限
        const int max_pass_runs_total = 30;

        // この反復で何かしら変更があったか
        bool any_pass_changed_this_iteration = false;

        // 各パスの変更状態を記録（次の反復用）
        std::vector<bool> pass_changed_current(passes.size(), false);

        // 各パスを実行（回数制限付き）
        for (size_t p = 0; p < passes.size(); ++p) {
            auto& pass = passes[p];
            std::string pass_name = pass->name();

            if (pass_run_counts[pass_name] >= max_pass_runs_total) {
                if (debug_output) {
                    std::cout << "[OPT]   " << pass_name
                              << " スキップ（実行回数上限: " << max_pass_runs_total << "回）\n";
                }
                continue;
            }

            // スキップ判定: 前回変更なし + その後に他のパスも変更なし → スキップ
            if (i > 0 && !pass_changed_last[p] && !any_pass_changed_this_iteration) {
                if (debug_output) {
                    std::cout << "[OPT]   " << pass_name << " スキップ（前回変更なし）\n";
                }
                continue;
            }

            auto pass_start = std::chrono::steady_clock::now();
            bool pass_changed = pass->run_on_program(program);
            auto pass_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - pass_start)
                               .count();

            pass_changed_current[p] = pass_changed;

            if (pass_changed) {
                pass_run_counts[pass_name]++;
                any_pass_changed_this_iteration = true;
                if (debug_output) {
                    std::cout << "[OPT]   " << pass_name
                              << " 変更実行 (回数: " << pass_run_counts[pass_name] << "/"
                              << max_pass_runs_total << ", " << pass_ms << "ms)\n";
                }
            } else if (debug_output && pass_ms > 0) {
                std::cout << "[OPT]   " << pass_name << " 変更なし (" << pass_ms << "ms)\n";
            }
        }

        // 次の反復用に変更状態を更新
        pass_changed_last = pass_changed_current;

        int after_count = count_instructions(program);
        bool any_changed = (before_count != after_count);

        if (any_changed) {
            int curr_inst_count = count_instructions(program);
            int curr_block_count = count_blocks(program);

            metrics.instructions_changed = std::abs(curr_inst_count - prev_inst_count);
            metrics.blocks_changed = std::abs(curr_block_count - prev_block_count);

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
                    std::cout << "[OPT] ✓ 実用的収束: 軽微な変更のみ（反復 " << (i + 1) << "）\n";
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

    debug_msg("MIR_OPT",
              "[OPT] ⚠ 警告: 最大反復回数（" + std::to_string(max_iterations) + "）に達しました");
    if (debug_output) {
        std::cerr << convergence_mgr.get_statistics();
    }
}

}  // namespace cm::mir::opt
