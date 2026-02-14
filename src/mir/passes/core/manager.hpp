#pragma once

#include "base.hpp"

#include <iostream>
#include <memory>
#include <vector>

namespace cm::mir::opt {

// 標準的な最適化パスを作成する関数
std::vector<std::unique_ptr<OptimizationPass>> create_standard_passes(int optimization_level);

// 最適化レベルに応じた収束戦略で最適化を実行
void run_optimization_passes(MirProgram& program, int optimization_level, bool debug = false);

}  // namespace cm::mir::opt