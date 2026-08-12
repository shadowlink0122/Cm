#pragma once

#include "base.hpp"

#include <iostream>
#include <memory>
#include <vector>

namespace cm::mir::opt {

// ユーザ指定のMIR最適化オプション（コマンドラインから制御する）。
// -O レベルとは独立に、個別の最適化を明示的に有効化するための仕組み。
// 新しい最適化フラグはここへフィールドを追加し、create_standard_passes でパスの追加・パラメータ化を行う
struct MirOptimizationOptions {
    bool unroll_loops = false;  // --funroll-loops: 定数トリップカウントループの静的展開
    int unroll_max_trips = 64;  // --funroll-loops=N: 展開する最大イテレーション数
    // js/tsターゲット: 集約（構造体・スライス）コピーの伝播を抑止する。
    // jsは構造体コピーを深いクローンで表現するため、コピー元での置換は別実体の変異になり不健全
    bool no_aggregate_copy_prop = false;
};

// 標準的な最適化パスを作成する関数
std::vector<std::unique_ptr<OptimizationPass>> create_standard_passes(
    int optimization_level, const MirOptimizationOptions& user_opts = {});

// 最適化レベルに応じた収束戦略で最適化を実行
void run_optimization_passes(MirProgram& program, int optimization_level, bool debug = false,
                             const MirOptimizationOptions& user_opts = {});

}  // namespace cm::mir::opt