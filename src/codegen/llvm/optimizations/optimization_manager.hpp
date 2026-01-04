#pragma once

#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/Pass.h>
#include <memory>
#include <vector>

namespace cm::codegen::llvm_backend::optimizations {

// 前方宣言
class PeepholeOptimizer;
class InstCombiner;
class Vectorizer;
class LoopUnroller;

/// LLVM最適化マネージャー - すべての最適化パスを管理・実行
class OptimizationManager {
   public:
    /// 最適化レベル
    enum class OptLevel {
        O0,  // 最適化なし
        O1,  // 基本最適化（Peephole）
        O2,  // 標準最適化（+InstCombine +Vectorize）
        O3,  // アグレッシブ最適化（+LoopUnroll +すべての最適化）
        Os,  // サイズ最適化
        Oz   // 最小サイズ最適化
    };

    /// 最適化設定
    struct Config {
        OptLevel level;

        // 個別最適化の有効/無効
        bool enablePeephole;
        bool enableInstCombine;
        bool enableVectorization;
        bool enableLoopUnrolling;

        // ベクトル化設定
        unsigned vectorWidth;  // SSE: 4, AVX: 8, AVX-512: 16
        bool enableSLP;

        // ループ展開設定
        unsigned maxUnrollFactor;
        bool enablePartialUnroll;
        bool enableCompleteUnroll;

        // その他の設定
        bool printStatistics;
        bool debugMode;

        // デフォルトコンストラクタ
        Config()
            : level(OptLevel::O2),
              enablePeephole(true),
              enableInstCombine(true),
              enableVectorization(true),
              enableLoopUnrolling(true),
              vectorWidth(4),
              enableSLP(true),
              maxUnrollFactor(4),
              enablePartialUnroll(true),
              enableCompleteUnroll(true),
              printStatistics(true),
              debugMode(false) {}
    };

    explicit OptimizationManager(const Config& config = Config());
    ~OptimizationManager();

    /// モジュール全体の最適化
    bool optimizeModule(llvm::Module& module);

    /// 関数単位の最適化
    bool optimizeFunction(llvm::Function& func);

    /// 最適化レベルの設定
    void setOptimizationLevel(OptLevel level);

    /// 統計情報の取得
    struct Statistics {
        // Peephole統計
        unsigned identitiesEliminated = 0;
        unsigned strengthReductions = 0;
        unsigned constantFolds = 0;

        // InstCombine統計
        unsigned instructionsCombined = 0;
        unsigned instructionsSimplified = 0;

        // Vectorization統計
        unsigned loopsVectorized = 0;
        unsigned slpGroupsVectorized = 0;

        // Loop Unrolling統計
        unsigned loopsUnrolled = 0;
        unsigned loopsCompletelyUnrolled = 0;

        // 全体統計
        unsigned totalInstructionsOptimized = 0;
        unsigned estimatedSpeedup = 0;  // 推定高速化率（%）
        size_t codeSizeReduction = 0;   // コードサイズ削減（バイト）
    };

    Statistics getStatistics() const { return stats; }
    void printStatistics() const;

   private:
    Config config;
    Statistics stats;

    // 最適化パス
    std::unique_ptr<PeepholeOptimizer> peephole;
    std::unique_ptr<InstCombiner> instCombine;
    std::unique_ptr<Vectorizer> vectorizer;
    std::unique_ptr<LoopUnroller> loopUnroller;

    /// 最適化パスの実行順序を管理
    enum class PassOrder {
        // Phase 1: 基本的な簡約化
        Peephole_First,
        InstCombine_First,

        // Phase 2: 高レベル変換
        LoopUnrolling,
        Vectorization,

        // Phase 3: クリーンアップ
        InstCombine_Second,
        Peephole_Second
    };

    /// 最適化パスの実行
    bool runPass(PassOrder pass, llvm::Function& func);

    /// 最適化パイプラインの構築
    std::vector<PassOrder> buildOptimizationPipeline();

    /// ターゲット固有の調整
    void adjustForTarget(const llvm::Module& module);

    /// メトリクスの収集
    void collectMetrics(const llvm::Function& func, bool before);

    /// 最適化の効果を測定
    void measureOptimizationEffect(const llvm::Function& func);
};

/// ファクトリ関数 - 最適化レベルから設定を作成
OptimizationManager::Config createConfigFromLevel(OptimizationManager::OptLevel level);

/// ターゲット固有の設定を作成
OptimizationManager::Config createConfigForTarget(const std::string& target);

}  // namespace cm::codegen::llvm_backend::optimizations