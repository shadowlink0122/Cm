#include "manager.hpp"

#include "../cleanup/cfg.hpp"
#include "../cleanup/dce.hpp"
#include "../cleanup/dse.hpp"
#include "../cleanup/program_dce.hpp"
#include "../convergence/manager.hpp"
#include "../interprocedural/inlining.hpp"
#include "../interprocedural/tail_call_elimination.hpp"
#include "../loop/licm.hpp"
#include "../redundancy/gvn.hpp"
#include "../scalar/folding.hpp"
#include "../scalar/propagation.hpp"
#include "../scalar/sccp.hpp"

namespace cm::mir::opt {

std::vector<std::unique_ptr<OptimizationPass>> create_standard_passes(int optimization_level) {
    std::vector<std::unique_ptr<OptimizationPass>> passes;

    // 最適化レベル0: デバッグ用（最適化なし）
    if (optimization_level == 0) {
        return passes;
    }

    // Phase 1: 基礎最適化
    passes.push_back(std::make_unique<SparseConditionalConstantPropagation>());
    passes.push_back(std::make_unique<ConstantFolding>());

    // Phase 2: データフロー最適化
    passes.push_back(std::make_unique<GVN>());
    passes.push_back(std::make_unique<CopyPropagation>());

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
        passes.push_back(std::make_unique<CopyPropagation>());
        passes.push_back(std::make_unique<DeadCodeElimination>());
    }

    return passes;
}

void run_optimization_passes(MirProgram& program, int optimization_level, bool debug) {
    // パイプラインを使用（収束管理付き）
    OptimizationPipeline pass_mgr;
    pass_mgr.enable_debug_output(debug);

    auto passes = create_standard_passes(optimization_level);
    for (auto& pass : passes) {
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
