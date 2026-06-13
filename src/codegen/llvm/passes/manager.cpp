/// @file optimization_manager.cpp
/// @brief LLVM最適化マネージャーの実装

#include "inst_combine/inst_combiner.hpp"
#include "loop_unrolling/loop_unroller.hpp"
#include "optimization_manager.hpp"
#include "peephole/peephole_optimizer.hpp"
#include "vectorization/vectorizer.hpp"

#include <chrono>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/Dominators.h>
#include <llvm/Support/raw_ostream.h>
#include <map>

namespace cm::codegen::llvm_backend::optimizations {

// コンストラクタ
OptimizationManager::OptimizationManager(const Config& config) : config(config) {
    // 最適化パスの初期化
    if (config.enablePeephole) {
        PeepholeOptimizer::Config peepholeConfig;
        peepholeConfig.enableIdentityElimination = true;
        peepholeConfig.enableStrengthReduction = true;
        peepholeConfig.enableConstantFolding = true;
        peepholeConfig.maxIterations = (config.level >= OptLevel::O2) ? 3 : 1;
        peephole = std::make_unique<PeepholeOptimizer>(peepholeConfig);
    }

    if (config.enableInstCombine && config.level >= OptLevel::O2) {
        InstCombiner::Config combineConfig;
        combineConfig.enableAlgebraicSimplification = true;
        combineConfig.enableSelectOptimization = true;
        combineConfig.enableCastOptimization = true;
        combineConfig.maxIterations = (config.level >= OptLevel::O3) ? 3 : 2;
        instCombine = std::make_unique<InstCombiner>(combineConfig);
    }

    if (config.enableVectorization && config.level >= OptLevel::O2) {
        Vectorizer::Config vecConfig;
        vecConfig.vectorWidth = config.vectorWidth;
        vecConfig.enableSLP = config.enableSLP;
        vecConfig.enableLoopVectorization = true;
        vecConfig.enableIfConversion = (config.level >= OptLevel::O3);
        vectorizer = std::make_unique<Vectorizer>(vecConfig);
    }

    if (config.enableLoopUnrolling && config.level >= OptLevel::O3) {
        LoopUnroller::Config unrollConfig;
        unrollConfig.maxUnrollFactor = config.maxUnrollFactor;
        unrollConfig.enablePartialUnroll = config.enablePartialUnroll;
        unrollConfig.enableCompleteUnroll = config.enableCompleteUnroll;
        unrollConfig.enableRuntimeUnroll = (config.level >= OptLevel::O3);
        loopUnroller = std::make_unique<LoopUnroller>(unrollConfig);
    }
}

// デストラクタ
OptimizationManager::~OptimizationManager() = default;

// モジュール全体の最適化
bool OptimizationManager::optimizeModule(llvm::Module& module) {
    auto startTime = std::chrono::high_resolution_clock::now();
    bool modified = false;

    // ターゲット固有の調整
    adjustForTarget(module);

    // 各関数を最適化
    for (auto& func : module) {
        if (func.isDeclaration()) {
            continue;  // 宣言のみの関数はスキップ
        }

        if (optimizeFunction(func)) {
            modified = true;
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    if (config.printStatistics) {
        llvm::errs() << "\n[OptimizationManager] Module optimization completed in "
                     << duration.count() << "ms\n";
        printStatistics();
    }

    return modified;
}

// 関数単位の最適化
bool OptimizationManager::optimizeFunction(llvm::Function& func) {
    if (config.level == OptLevel::O0) {
        return false;  // 最適化なし
    }

    bool modified = false;
    const size_t maxIterations = (config.level >= OptLevel::O3) ? 3 : 2;

    // メトリクス収集（最適化前）
    collectMetrics(func, true);

    // 最適化パイプラインの実行
    auto pipeline = buildOptimizationPipeline();

    for (size_t iteration = 0; iteration < maxIterations; iteration++) {
        bool iterationModified = false;

        for (auto pass : pipeline) {
            if (runPass(pass, func)) {
                iterationModified = true;
            }
        }

        if (!iterationModified) {
            break;  // 変更がなければ早期終了
        }

        modified = true;
    }

    // メトリクス収集（最適化後）
    collectMetrics(func, false);

    // 最適化効果の測定
    if (modified) {
        measureOptimizationEffect(func);
    }

    return modified;
}

// 最適化パスの実行
bool OptimizationManager::runPass(PassOrder pass, llvm::Function& func) {
    bool modified = false;

    switch (pass) {
        case PassOrder::Peephole_First:
        case PassOrder::Peephole_Second:
            if (peephole) {
                modified = peephole->optimizeFunction(func);
                if (modified) {
                    stats.identitiesEliminated += peephole->getStats().identitiesEliminated;
                    stats.strengthReductions += peephole->getStats().strengthReductions;
                    stats.constantFolds += peephole->getStats().constantFolds;
                }
            }
            break;

        case PassOrder::InstCombine_First:
        case PassOrder::InstCombine_Second:
            if (instCombine) {
                modified = instCombine->combineInstructions(func);
                if (modified) {
                    stats.instructionsCombined += instCombine->getStats().instructionsCombined;
                    stats.instructionsSimplified += instCombine->getStats().instructionsSimplified;
                }
            }
            break;

        case PassOrder::Vectorization:
            if (vectorizer) {
                modified = vectorizer->vectorizeFunction(func);
                if (modified) {
                    stats.loopsVectorized += vectorizer->getStats().loopsVectorized;
                    stats.slpGroupsVectorized += vectorizer->getStats().slpGroupsVectorized;
                }
            }
            break;

        case PassOrder::LoopUnrolling:
            if (loopUnroller) {
                modified = loopUnroller->unrollFunction(func);
                if (modified) {
                    stats.loopsUnrolled += loopUnroller->getStats().loopsPartiallyUnrolled;
                    stats.loopsCompletelyUnrolled +=
                        loopUnroller->getStats().loopsCompletelyUnrolled;
                }
            }
            break;
    }

    if (modified) {
        stats.totalInstructionsOptimized++;
    }

    return modified;
}

// 最適化パイプラインの構築
std::vector<OptimizationManager::PassOrder> OptimizationManager::buildOptimizationPipeline() {
    std::vector<PassOrder> pipeline;

    // Phase 1: 基本的な簡約化（ループ展開前の準備）
    if (config.enablePeephole) {
        pipeline.push_back(PassOrder::Peephole_First);
    }
    if (config.enableInstCombine && config.level >= OptLevel::O2) {
        pipeline.push_back(PassOrder::InstCombine_First);
    }

    // Phase 2: 高レベル変換
    if (config.enableLoopUnrolling && config.level >= OptLevel::O3) {
        pipeline.push_back(PassOrder::LoopUnrolling);
    }
    if (config.enableVectorization && config.level >= OptLevel::O2) {
        pipeline.push_back(PassOrder::Vectorization);
    }

    // Phase 3: クリーンアップ（変換後の最適化）
    if (config.enableInstCombine && config.level >= OptLevel::O2) {
        pipeline.push_back(PassOrder::InstCombine_Second);
    }
    if (config.enablePeephole) {
        pipeline.push_back(PassOrder::Peephole_Second);
    }

    return pipeline;
}

// 最適化レベルの設定
void OptimizationManager::setOptimizationLevel(OptLevel level) {
    config.level = level;

    // レベルに応じて個別最適化を調整
    switch (level) {
        case OptLevel::O0:
            config.enablePeephole = false;
            config.enableInstCombine = false;
            config.enableVectorization = false;
            config.enableLoopUnrolling = false;
            break;

        case OptLevel::O1:
            config.enablePeephole = true;
            config.enableInstCombine = false;
            config.enableVectorization = false;
            config.enableLoopUnrolling = false;
            break;

        case OptLevel::O2:
            config.enablePeephole = true;
            config.enableInstCombine = true;
            config.enableVectorization = true;
            config.enableLoopUnrolling = false;
            config.vectorWidth = 4;
            break;

        case OptLevel::O3:
            config.enablePeephole = true;
            config.enableInstCombine = true;
            config.enableVectorization = true;
            config.enableLoopUnrolling = true;
            config.vectorWidth = 8;
            config.maxUnrollFactor = 8;
            break;

        case OptLevel::Os:
        case OptLevel::Oz:
            config.enablePeephole = true;
            config.enableInstCombine = true;
            config.enableVectorization = false;  // サイズ増加を避ける
            config.enableLoopUnrolling = false;  // サイズ増加を避ける
            break;
    }

    // 最適化パスを再初期化
    // NOTE: 代わりにコンフィギュレーションだけ更新
    // 完全な再初期化が必要な場合は、新しいOptimizationManagerを作成する必要がある
}

// ターゲット固有の調整
void OptimizationManager::adjustForTarget(const llvm::Module& module) {
    // ターゲットトリプルを取得
    std::string targetTriple = module.getTargetTriple();

    // WebAssembly向けの調整
    if (targetTriple.find("wasm32") != std::string::npos ||
        targetTriple.find("wasm64") != std::string::npos) {
        // WASMはSIMDサポートが限定的
        config.enableVectorization = false;
        config.enableLoopUnrolling = true;  // 代わりにループ展開を強化
        config.maxUnrollFactor = 2;         // 控えめに展開
    }

    // ARM向けの調整
    else if (targetTriple.find("arm") != std::string::npos ||
             targetTriple.find("aarch64") != std::string::npos) {
        // ARMはNEON SIMDをサポート
        config.vectorWidth = 4;
        config.enableSLP = true;
    }

    // x86/x64向けの調整
    else if (targetTriple.find("x86_64") != std::string::npos ||
             targetTriple.find("i386") != std::string::npos) {
        // AVXサポートを仮定
        config.vectorWidth = 8;
        config.enableSLP = true;
    }
}

// メトリクスの収集
void OptimizationManager::collectMetrics(const llvm::Function& func, bool before) {
    static std::map<const llvm::Function*, size_t> beforeSizes;

    size_t instructionCount = 0;
    for (const auto& bb : func) {
        instructionCount += bb.size();
    }

    if (before) {
        beforeSizes[&func] = instructionCount;
    } else {
        auto it = beforeSizes.find(&func);
        if (it != beforeSizes.end()) {
            size_t beforeSize = it->second;
            if (instructionCount < beforeSize) {
                stats.codeSizeReduction += (beforeSize - instructionCount);
            }
        }
    }
}

// 最適化効果の測定
void OptimizationManager::measureOptimizationEffect(const llvm::Function& func) {
    // 簡単な推定：削減された命令数から高速化率を計算
    // 実際の効果はアーキテクチャとワークロードに依存

    unsigned totalOptimizations = stats.identitiesEliminated + stats.strengthReductions +
                                  stats.constantFolds + stats.instructionsCombined +
                                  stats.instructionsSimplified;

    // ベクトル化とループ展開は大きな効果
    unsigned majorOptimizations = stats.loopsVectorized * 200 +  // ベクトル化は2倍高速化と仮定
                                  stats.loopsUnrolled * 30 +  // 部分展開は30%高速化
                                  stats.loopsCompletelyUnrolled * 50;  // 完全展開は50%高速化

    // 全体的な推定高速化率
    stats.estimatedSpeedup = std::min(300u,  // 最大3倍高速化
                                      totalOptimizations * 2 + majorOptimizations);
}

// 統計情報の出力
void OptimizationManager::printStatistics() const {
    llvm::errs() << "\n[OptimizationManager] Statistics:\n";
    llvm::errs() << "=====================================\n";

    if (config.enablePeephole &&
        stats.identitiesEliminated + stats.strengthReductions + stats.constantFolds > 0) {
        llvm::errs() << "Peephole Optimizer:\n";
        llvm::errs() << "  Identities eliminated: " << stats.identitiesEliminated << "\n";
        llvm::errs() << "  Strength reductions: " << stats.strengthReductions << "\n";
        llvm::errs() << "  Constants folded: " << stats.constantFolds << "\n";
    }

    if (config.enableInstCombine && stats.instructionsCombined + stats.instructionsSimplified > 0) {
        llvm::errs() << "Instruction Combiner:\n";
        llvm::errs() << "  Instructions combined: " << stats.instructionsCombined << "\n";
        llvm::errs() << "  Instructions simplified: " << stats.instructionsSimplified << "\n";
    }

    if (config.enableVectorization && stats.loopsVectorized + stats.slpGroupsVectorized > 0) {
        llvm::errs() << "Vectorizer:\n";
        llvm::errs() << "  Loops vectorized: " << stats.loopsVectorized << "\n";
        llvm::errs() << "  SLP groups vectorized: " << stats.slpGroupsVectorized << "\n";
    }

    if (config.enableLoopUnrolling && stats.loopsUnrolled + stats.loopsCompletelyUnrolled > 0) {
        llvm::errs() << "Loop Unroller:\n";
        llvm::errs() << "  Loops partially unrolled: " << stats.loopsUnrolled << "\n";
        llvm::errs() << "  Loops completely unrolled: " << stats.loopsCompletelyUnrolled << "\n";
    }

    llvm::errs() << "Overall:\n";
    llvm::errs() << "  Total instructions optimized: " << stats.totalInstructionsOptimized << "\n";
    llvm::errs() << "  Code size reduction: " << stats.codeSizeReduction << " instructions\n";
    llvm::errs() << "  Estimated speedup: " << stats.estimatedSpeedup << "%\n";
    llvm::errs() << "=====================================\n";
}

// ファクトリ関数 - 最適化レベルから設定を作成
OptimizationManager::Config createConfigFromLevel(OptimizationManager::OptLevel level) {
    OptimizationManager::Config config;
    config.level = level;

    switch (level) {
        case OptimizationManager::OptLevel::O0:
            config.enablePeephole = false;
            config.enableInstCombine = false;
            config.enableVectorization = false;
            config.enableLoopUnrolling = false;
            break;

        case OptimizationManager::OptLevel::O1:
            config.enablePeephole = true;
            config.enableInstCombine = false;
            config.enableVectorization = false;
            config.enableLoopUnrolling = false;
            break;

        case OptimizationManager::OptLevel::O2:
            config.enablePeephole = true;
            config.enableInstCombine = true;
            config.enableVectorization = true;
            config.enableLoopUnrolling = false;
            config.vectorWidth = 4;
            break;

        case OptimizationManager::OptLevel::O3:
            config.enablePeephole = true;
            config.enableInstCombine = true;
            config.enableVectorization = true;
            config.enableLoopUnrolling = true;
            config.vectorWidth = 8;
            config.maxUnrollFactor = 8;
            config.enablePartialUnroll = true;
            config.enableCompleteUnroll = true;
            break;

        case OptimizationManager::OptLevel::Os:
            config.enablePeephole = true;
            config.enableInstCombine = true;
            config.enableVectorization = false;
            config.enableLoopUnrolling = false;
            break;

        case OptimizationManager::OptLevel::Oz:
            config.enablePeephole = true;
            config.enableInstCombine = false;
            config.enableVectorization = false;
            config.enableLoopUnrolling = false;
            break;
    }

    return config;
}

// ターゲット固有の設定を作成
OptimizationManager::Config createConfigForTarget(const std::string& target) {
    OptimizationManager::Config config;

    if (target == "wasm32" || target == "wasm64") {
        // WebAssembly向け
        config.level = OptimizationManager::OptLevel::Os;
        config.enableVectorization = false;  // WASM SIMDは限定的
        config.enableLoopUnrolling = true;
        config.maxUnrollFactor = 2;
    } else if (target == "aarch64" || target == "arm64") {
        // ARM64向け
        config.level = OptimizationManager::OptLevel::O2;
        config.vectorWidth = 4;  // NEON
        config.enableSLP = true;
    } else if (target == "x86_64") {
        // x64向け
        config.level = OptimizationManager::OptLevel::O2;
        config.vectorWidth = 8;  // AVX
        config.enableSLP = true;
    } else {
        // デフォルト
        config.level = OptimizationManager::OptLevel::O2;
    }

    return config;
}

}  // namespace cm::codegen::llvm_backend::optimizations