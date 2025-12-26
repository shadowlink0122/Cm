#pragma once

#include "constant_folding.hpp"
#include "copy_propagation.hpp"
#include "dead_code_elimination.hpp"
#include "dse.hpp"
#include "gvn.hpp"
#include "inlining.hpp"
#include "optimization_pass.hpp"
#include "program_dce.hpp"
#include "sccp.hpp"
#include "simplify_cfg.hpp"

#include <iostream>

namespace cm::mir::opt {

// 標準的な最適化パスを作成する関数
inline std::vector<std::unique_ptr<OptimizationPass>> create_standard_passes(
    int optimization_level) {
    std::vector<std::unique_ptr<OptimizationPass>> passes;

    if (optimization_level >= 1) {
        // -O1: 基本的な最適化
        passes.push_back(std::make_unique<SparseConditionalConstantPropagation>());
        passes.push_back(std::make_unique<ConstantFolding>());
        passes.push_back(std::make_unique<GVN>());
        passes.push_back(std::make_unique<CopyPropagation>());
        passes.push_back(std::make_unique<FunctionInlining>());
        passes.push_back(std::make_unique<SimplifyControlFlow>());
        passes.push_back(std::make_unique<DeadStoreElimination>());
        passes.push_back(std::make_unique<DeadCodeElimination>());
    }

    if (optimization_level >= 2) {
        // -O2: より積極的な最適化
        // 最適化パスを複数回実行
        passes.push_back(std::make_unique<ConstantFolding>());
        passes.push_back(std::make_unique<CopyPropagation>());
        // TODO: インライン化、ループ最適化など
    }

    if (optimization_level >= 3) {
        // -O3: 最大限の最適化
        // TODO: ベクトル化、アンロールなど
    }

    return passes;
}

}  // namespace cm::mir::opt