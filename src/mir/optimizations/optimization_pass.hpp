#pragma once

#include "../mir_nodes.hpp"

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
            std::cout << "[OPT] Starting optimization pipeline\n";
        }

        for (auto& pass : passes) {
            if (debug_output) {
                std::cout << "[OPT] Running pass: " << pass->name() << "\n";
            }

            bool changed = pass->run_on_program(program);

            if (debug_output) {
                std::cout << "[OPT] Pass " << pass->name()
                          << (changed ? " made changes" : " made no changes") << "\n";
            }
        }

        if (debug_output) {
            std::cout << "[OPT] Optimization pipeline completed\n";
        }
    }

    // 収束するまで繰り返し実行
    void run_until_fixpoint(MirProgram& program, int max_iterations = 10) {
        for (int i = 0; i < max_iterations; ++i) {
            bool changed = false;

            for (auto& pass : passes) {
                changed |= pass->run_on_program(program);
            }

            if (!changed) {
                if (debug_output) {
                    std::cout << "[OPT] Fixed point reached after " << i + 1 << " iterations\n";
                }
                break;
            }
        }
    }
};

}  // namespace cm::mir::opt