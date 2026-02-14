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
    int count_instructions(const MirProgram& program) const;

    // 基本ブロック数をカウント
    int count_blocks(const MirProgram& program) const;

   public:
    // デバッグ出力を有効化
    void enable_debug_output(bool enable = true) { debug_output = enable; }

    // パスを追加
    void add_pass(std::unique_ptr<OptimizationPass> pass) { passes.push_back(std::move(pass)); }

    // 標準的な最適化パスを追加
    void add_standard_passes(int optimization_level = 1);

    // 最適化を実行
    void run(MirProgram& program);

    // 収束するまで繰り返し実行（収束管理付き）
    void run_until_fixpoint(MirProgram& program, int max_iterations = 10);
};

}  // namespace cm::mir::opt