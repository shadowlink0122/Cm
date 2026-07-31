#include "manager.hpp"

#include "internal/mir/passes/cleanup/cfg.hpp"
#include "internal/mir/passes/cleanup/dce.hpp"
#include "internal/mir/passes/cleanup/dse.hpp"
#include "internal/mir/passes/cleanup/program_dce.hpp"
#include "internal/mir/passes/cleanup/string_reassign_free.hpp"
#include "internal/mir/passes/convergence/manager.hpp"
#include "internal/mir/passes/interprocedural/inlining.hpp"
#include "internal/mir/passes/interprocedural/tail_call_elimination.hpp"
#include "internal/mir/passes/loop/const_unroll.hpp"
#include "internal/mir/passes/loop/licm.hpp"
#include "internal/mir/passes/redundancy/gvn.hpp"
#include "internal/mir/passes/scalar/folding.hpp"
#include "internal/mir/passes/scalar/propagation.hpp"
#include "internal/mir/passes/scalar/sccp.hpp"

#include <iostream>
#include <memory>
#include <utility>
#include <vector>

namespace cm::mir::opt {

std::vector<std::unique_ptr<OptimizationPass>> create_standard_passes(
    int optimization_level, const MirOptimizationOptions& user_opts) {
    std::vector<std::unique_ptr<OptimizationPass>> passes;

    // ユーザ指定の定数ループ展開（--funroll-loops）。
    // SCCP/ConstantFoldingより前に実行し、展開後の定数連鎖を後段で畳み込む。
    // -O0でも明示指定されていれば有効
    if (user_opts.unroll_loops) {
        passes.push_back(std::make_unique<ConstantLoopUnroll>(user_opts.unroll_max_trips));
    }

    // 最適化レベル0: デバッグ用（最適化なし。ユーザ指定パスのみ）
    if (optimization_level == 0) {
        return passes;
    }

    // Phase 0: 文字列再代入の旧バッファ解放（C12）。
    // loweringが生成した素のMIR形状（T = concat(...) → X = copy(T)）を前提に分類するため、
    // コピー伝播等がこの形状を書き換える前のパイプライン先頭で実行する
    passes.push_back(std::make_unique<StringReassignFree>());

    // Phase 1: 基礎最適化
    passes.push_back(std::make_unique<SparseConditionalConstantPropagation>());
    passes.push_back(std::make_unique<ConstantFolding>());

    // Phase 2: データフロー最適化
    passes.push_back(std::make_unique<GVN>());
    passes.push_back(std::make_unique<CopyPropagation>(user_opts.no_aggregate_copy_prop));

    // Phase 3: 冗長性排除
    passes.push_back(std::make_unique<DeadStoreElimination>());

    // Phase 4: 制御フロー最適化
    passes.push_back(std::make_unique<SimplifyControlFlow>());
    passes.push_back(std::make_unique<FunctionInlining>());
    // 末尾呼び出し最適化
    passes.push_back(std::make_unique<TailCallElimination>());

    // Phase 5: ループ最適化
    passes.push_back(std::make_unique<LoopInvariantCodeMotion>());

    // 最終パス: 不要コード削除
    passes.push_back(std::make_unique<DeadCodeElimination>());

    // 最適化レベル2以上: 複数回実行
    if (optimization_level >= 2) {
        passes.push_back(std::make_unique<ConstantFolding>());
        passes.push_back(std::make_unique<CopyPropagation>(user_opts.no_aggregate_copy_prop));
        passes.push_back(std::make_unique<DeadCodeElimination>());
    }

    return passes;
}

void run_optimization_passes(MirProgram& program, int optimization_level, bool debug,
                             const MirOptimizationOptions& user_opts) {
    // パイプラインを使用（収束管理付き）
    OptimizationPipeline pass_mgr;
    pass_mgr.enable_debug_output(debug);

    auto passes = create_standard_passes(optimization_level, user_opts);
    const bool trace_passes = std::getenv("CM_TRACE_PASSES") != nullptr;
    for (auto& pass : passes) {
        if (trace_passes) {
            fprintf(stderr, "[PASSDBG] queued: %s\n", pass->name().c_str());
        }
        pass_mgr.add_pass(std::move(pass));
    }

    // 最適化レベルに応じた反復回数を設定
    int max_iterations = 5;
    switch (optimization_level) {
        case 1:
            max_iterations = 3;
            if (debug) {
                std::cout << "[OPT] -O1: バランス型最適化（最大" << max_iterations << "回反復）\n";
            }
            break;
        case 2:
            max_iterations = 5;
            if (debug) {
                std::cout << "[OPT] -O2: 実用最適化（最大" << max_iterations << "回反復）\n";
            }
            break;
        case 3:
            max_iterations = 7;
            if (debug) {
                std::cout << "[OPT] -O3: 最大最適化（最大" << max_iterations << "回反復）\n";
            }
            break;
        default:
            if (optimization_level > 3) {
                max_iterations = 100;
                if (debug) {
                    std::cout << "[OPT] -O" << optimization_level << ": 実験的最適化（最大"
                              << max_iterations << "回反復）\n";
                }
            }
            break;
    }

    // 最適化を実行（収束判定付き）
    pass_mgr.run_until_fixpoint(program, max_iterations);

    if (debug) {
        std::cout << "[OPT] 最適化完了\n";
    }
}

}  // namespace cm::mir::opt
