#pragma once

#include "../cleanup/cfg.hpp"
#include "../cleanup/dce.hpp"
#include "../cleanup/dse.hpp"
#include "../cleanup/program_dce.hpp"
#include "../convergence/manager.hpp"  // 収束管理
#include "../interprocedural/inlining.hpp"
#include "../loop/licm.hpp"
#include "../redundancy/gvn.hpp"
#include "../scalar/folding.hpp"
#include "../scalar/propagation.hpp"
#include "../scalar/sccp.hpp"
#include "base.hpp"
#include "pipeline_v2.hpp"  // 改良版パイプライン

#include <iostream>

namespace cm::mir::opt {

// 標準的な最適化パスを作成する関数
inline std::vector<std::unique_ptr<OptimizationPass>> create_standard_passes(
    int optimization_level) {
    std::vector<std::unique_ptr<OptimizationPass>> passes;

    // 最適化レベル0: デバッグ用（最適化なし）
    if (optimization_level == 0) {
        return passes;
    }

    // デフォルト: すべてのMIR最適化を適用
    // これらは全プラットフォーム共通で高速に動作する

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

    // Phase 5: ループ最適化
    passes.push_back(std::make_unique<LoopInvariantCodeMotion>());

    // 最終パス: 不要コード削除
    passes.push_back(std::make_unique<DeadCodeElimination>());

    // 最適化レベル2以上: 複数回実行してさらなる最適化機会を探す
    if (optimization_level >= 2) {
        // 2回目のパスで新たな最適化機会が見つかることがある
        passes.push_back(std::make_unique<ConstantFolding>());
        passes.push_back(std::make_unique<CopyPropagation>());
        passes.push_back(std::make_unique<DeadCodeElimination>());
    }

    return passes;
}

// 最適化レベルに応じた収束戦略で最適化を実行
inline void run_optimization_passes(MirProgram& program, int optimization_level,
                                    bool debug = false) {
    // ポインタ操作のMIR最適化問題を修正済み
    // - Copy Propagation: キャスト（型変換）を含む代入はコピー伝播の対象外に
    // - Constant Folding: ポインタ型への変換は定数畳み込みしない
    //
    // 以下の修正により、ポインタ代入チェーンの無限ループ問題を解決：
    // 1. propagation.hpp: キャストを含む代入ではコピー情報を無効化
    // 2. folding.hpp: ポインタ型への変換は定数畳み込みから除外

    // パスマネージャーv2を使用（収束管理付き）
    OptimizationPipelineV2 pass_mgr;
    pass_mgr.enable_debug_output(debug);

    // 最適化パスを追加
    auto passes = create_standard_passes(optimization_level);
    for (auto& pass : passes) {
        pass_mgr.add_pass(std::move(pass));
    }

    // 最適化レベルに応じた反復回数を設定
    int max_iterations = 5;  // デフォルト
    switch (optimization_level) {
        case 1:
            max_iterations = 2;  // -O1: 軽量（最大2回）
            if (debug) {
                std::cout << "[OPT] -O1: バランス型最適化（最大" << max_iterations << "回反復）\n";
            }
            break;
        case 2:
            max_iterations = 10;  // -O2: 実用的（最大10回）
            if (debug) {
                std::cout << "[OPT] -O2: 実用最適化（最大" << max_iterations << "回反復）\n";
            }
            break;
        case 3:
            max_iterations = 10;  // -O3: 最大（最大10回）- 個々のパスに制限を設ける
            if (debug) {
                std::cout << "[OPT] -O3: 最大最適化（最大" << max_iterations << "回反復）\n";
            }
            break;
        default:
            if (optimization_level > 3) {
                max_iterations = 30;  // -O4以上: 実験的
                if (debug) {
                    std::cout << "[OPT] -O" << optimization_level << ": 実験的最適化（最大"
                              << max_iterations << "回反復）\n";
                }
            }
            break;
    }

    // 最適化を実行（v2を使用 - 収束判定とタイムアウト機能付き）
    pass_mgr.run_until_fixpoint_v2(program, max_iterations);

    if (debug) {
        std::cout << "[OPT] 最適化完了\n";
    }
}

}  // namespace cm::mir::opt