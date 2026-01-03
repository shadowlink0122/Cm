#pragma once

#include "../convergence/manager.hpp"
#include "base.hpp"
#include "timeout_guard.hpp"

#include <iostream>
#include <unordered_map>

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

        // タイムアウトガードを設定（全体で30秒まで）
        TimeoutGuard timeout_guard(std::chrono::seconds(30));

        // パスごとのタイムアウト管理（各パス最大5秒）
        PassTimeoutManager pass_timer(std::chrono::seconds(5));

        // 複雑度制限（ブロック1000、ステートメント10000、ローカル500）
        ComplexityLimiter complexity_limiter(1000, 10000, 500);

        // 複雑すぎる関数をスキップ
        for (const auto& func : program.functions) {
            if (func && complexity_limiter.is_too_complex(*func)) {
                if (debug_output) {
                    std::cout << "[OPT] 関数 '" << func->name
                              << "' は複雑すぎるため最適化をスキップします\n";
                }
            }
        }

        // 個々の最適化パスの実行回数を追跡
        std::unordered_map<std::string, int> pass_run_counts;

        for (int i = 0; i < max_iterations; ++i) {
            // タイムアウトチェック
            if (timeout_guard.is_timeout()) {
                std::cerr << "[OPT] ⚠ 警告: 最適化がタイムアウトしました（30秒）\n";
                return;
            }

            ConvergenceManager::ChangeMetrics metrics;

            if (debug_output) {
                std::cout << "[OPT] 反復 " << (i + 1) << "/" << max_iterations << "\n";
                std::cout << "[OPT]   経過時間: " << timeout_guard.elapsed().count() << "ms\n";
            }

            // 実行前の状態を記録
            int prev_inst_count = count_instructions(program);
            int prev_block_count = count_blocks(program);

            // 一時的に変更検出用の仕組みを用意
            int before_count = count_instructions(program);

            // 個々のパスごとの実行回数制限（同じパスが過度に実行されるのを防ぐ）
            const int max_pass_runs_total = 30;  // 全反復を通じて同じパスの最大実行回数

            // 各パスを実行（回数制限付き）
            for (auto& pass : passes) {
                // 全体のタイムアウトチェック
                if (timeout_guard.is_timeout()) {
                    std::cerr << "[OPT] ⚠ パス実行中にタイムアウトしました\n";
                    return;
                }

                std::string pass_name = pass->name();

                // このパスの実行回数をチェック
                if (pass_run_counts[pass_name] >= max_pass_runs_total) {
                    if (debug_output) {
                        std::cout << "[OPT]   " << pass_name
                                  << " スキップ（実行回数上限: " << max_pass_runs_total << "回）\n";
                    }
                    continue;
                }

                // 個別パスのタイマー開始
                pass_timer.start_pass(pass_name);

                // パスをタイムアウト付きで実行
                bool pass_changed = timeout_guard.execute_with_timeout(
                    [&]() { return pass->run_on_program(program); }, pass_name);

                // パスのタイムアウトチェック
                if (pass_timer.check_pass_timeout()) {
                    std::cerr << "[OPT] ⚠ パス '" << pass_name << "' がタイムアウトしました\n";
                    return;
                }

                pass_timer.end_pass(debug_output);

                if (pass_changed) {
                    pass_run_counts[pass_name]++;
                    if (debug_output) {
                        std::cout << "[OPT]   " << pass_name
                                  << " 変更実行 (回数: " << pass_run_counts[pass_name] << "/"
                                  << max_pass_runs_total << ")\n";
                    }
                }
            }

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
