#pragma once

#include "../../nodes.hpp"
#include "../../printer.hpp"
#include "../convergence/manager.hpp"
#include "common/debug.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>

namespace cm::mir::opt {

// ============================================================
// 最適化パスの基底クラス
// ============================================================
class OptimizationPass {
   public:
    virtual ~OptimizationPass() = default;

    // パス名を取得
    virtual std::string name() const = 0;

    // 最適化を実行（変更があった場合trueを返す）
    virtual bool run(MirFunction& func) = 0;

    // プログラム全体に対する最適化（デフォルトは各関数に対して実行）
    virtual bool run_on_program(MirProgram& program) {
        bool changed = false;
        for (auto& func : program.functions) {
            if (func) {
                changed |= run(*func);
            }
        }
        return changed;
    }

   protected:
    // ユーティリティ関数

    // 使用されているローカル変数を収集
    void collect_used_locals(const MirFunction& func, std::set<LocalId>& used);

    // 定数値かどうかチェック
    bool is_constant(const MirOperand& op) const { return op.kind == MirOperand::Constant; }

    // 二つの定数を評価
    std::optional<MirConstant> eval_binary_op(MirBinaryOp op, const MirConstant& lhs,
                                              const MirConstant& rhs);

    // 単項演算を評価
    std::optional<MirConstant> eval_unary_op(MirUnaryOp op, const MirConstant& operand);
};

// ============================================================
// 最適化パイプライン
// ============================================================
class OptimizationPipeline {
   protected:
    std::vector<std::unique_ptr<OptimizationPass>> passes;
    bool debug_output = false;

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
    void enable_debug_output(bool enable = true) { debug_output = enable; }

    // パスを追加
    void add_pass(std::unique_ptr<OptimizationPass> pass) { passes.push_back(std::move(pass)); }

    // 標準的な最適化パスを追加
    void add_standard_passes(int optimization_level = 1);

    // 最適化を実行
    void run(MirProgram& program) {
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

    // 収束するまで繰り返し実行（収束管理付き）
    void run_until_fixpoint(MirProgram& program, int max_iterations = 10) {
        using namespace cm::mir::optimizations;

        ConvergenceManager convergence_mgr;

        // 個々の最適化パスの実行回数を追跡
        std::unordered_map<std::string, int> pass_run_counts;

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

            // 個々のパスごとの実行回数制限（同じパスが過度に実行されるのを防ぐ）
            const int max_pass_runs_total = 30;

            // 各パスを実行（回数制限付き）
            for (auto& pass : passes) {
                std::string pass_name = pass->name();

                // このパスの実行回数をチェック
                if (pass_run_counts[pass_name] >= max_pass_runs_total) {
                    if (debug_output) {
                        std::cout << "[OPT]   " << pass_name
                                  << " スキップ（実行回数上限: " << max_pass_runs_total << "回）\n";
                    }
                    continue;
                }

                // パスを実行
                bool pass_changed = pass->run_on_program(program);

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

        debug_msg("MIR_OPT", "[OPT] ⚠ 警告: 最大反復回数（" + std::to_string(max_iterations) +
                                 "）に達しました");
        if (debug_output) {
            std::cerr << convergence_mgr.get_statistics();
        }
    }
};

}  // namespace cm::mir::opt