#pragma once

#include "../../nodes.hpp"
#include "../../printer.hpp"

#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <string>

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
   private:
    std::vector<std::unique_ptr<OptimizationPass>> passes;
    bool debug_output = false;

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

    // 収束するまで繰り返し実行（無限ループ対策強化）
    void run_until_fixpoint(MirProgram& program, int max_iterations = 10) {
        // 反復回数を10に戻す（O3レベルの最適化で収束しやすくするため）
        int total_pass_runs = 0;
        const int max_total_runs = 200;  // 全体での最大実行回数（O3で多数のパス×10反復に対応）

        for (int i = 0; i < max_iterations; ++i) {
            bool changed = false;

            if (debug_output) {
                std::cout << "[OPT] Iteration " << (i + 1) << "/" << max_iterations << "\n";
            }

            for (auto& pass : passes) {
                if (total_pass_runs >= max_total_runs) {
                    std::cerr << "[OPT] 警告: 最適化パスの総実行回数が上限(" << max_total_runs
                              << ")に達しました\n";
                    return;
                }

                total_pass_runs++;
                bool pass_changed = pass->run_on_program(program);
                changed |= pass_changed;

                if (debug_output && pass_changed) {
                    std::cout << "[OPT]   " << pass->name() << " made changes\n";
                }
            }

            if (!changed) {
                if (debug_output) {
                    std::cout << "[OPT] Fixed point reached after " << i + 1 << " iterations\n";
                    std::cout << "[OPT] Total pass runs: " << total_pass_runs << "\n";
                }
                break;
            }

            if (i == max_iterations - 1) {
                if (debug_output) {
                    std::cerr << "[OPT] 警告: 最大反復回数に達しました（収束せず）\n";
                }
            }
        }
    }
};

}  // namespace cm::mir::opt