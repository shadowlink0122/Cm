#pragma once

#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Transforms/Utils/LoopSimplify.h>
#include <llvm/Transforms/Utils/ValueMapper.h>
#include <optional>
#include <unordered_map>
#include <vector>

namespace cm::codegen::llvm_backend::optimizations {

/// ループ展開最適化 - ループオーバーヘッドの削減と命令レベル並列性の向上
class LoopUnroller {
   public:
    /// ループ展開設定
    struct Config {
        unsigned maxUnrollFactor;  // 最大展開係数
        unsigned maxLoopSize;      // 展開対象の最大ループサイズ（命令数）
        unsigned minTripCount;  // 最小トリップカウント（これ以下は展開しない）
        bool enablePartialUnroll;   // 部分展開を有効化
        bool enableCompleteUnroll;  // 完全展開を有効化
        bool enablePeeling;         // ループピーリング（最初/最後の反復を分離）
        bool enableRuntimeUnroll;  // 実行時展開（トリップカウントが不明な場合）
        unsigned preferredUnrollFactor;  // 0の場合は自動決定

        // デフォルトコンストラクタ
        Config()
            : maxUnrollFactor(4),
              maxLoopSize(100),
              minTripCount(4),
              enablePartialUnroll(true),
              enableCompleteUnroll(true),
              enablePeeling(true),
              enableRuntimeUnroll(true),
              preferredUnrollFactor(0) {}
    };

    explicit LoopUnroller(const Config& config = Config()) : config(config) {}

    /// 関数全体のループ展開
    bool unrollFunction(llvm::Function& func);

    /// 統計情報
    struct Stats {
        unsigned loopsAnalyzed = 0;
        unsigned loopsCompletelyUnrolled = 0;
        unsigned loopsPartiallyUnrolled = 0;
        unsigned loopsRuntimeUnrolled = 0;
        unsigned loopsPeeled = 0;
        unsigned loopsPipelined = 0;
        unsigned totalInstructionsEliminated = 0;
        unsigned totalSpeedup = 0;  // 推定高速化率（%）
    };

    /// 統計情報の取得
    const Stats& getStats() const { return stats; }

   private:
    Config config;
    Stats stats;

    /// ループ展開情報
    struct UnrollInfo {
        bool canUnroll = false;
        bool isFullyUnrollable = false;
        unsigned unrollFactor = 1;
        unsigned tripCount = 0;  // 0は不明
        unsigned loopSize = 0;   // ループ内の命令数
        bool hasLoopCarriedDependency = false;
        bool hasCallInLoop = false;
        unsigned registerPressure = 0;
        unsigned estimatedSpeedup = 0;  // 推定高速化率（%）
    };

    /// ループ展開戦略
    enum class UnrollStrategy {
        None,          // 展開しない
        Complete,      // 完全展開（ループを完全に除去）
        Partial,       // 部分展開（指定係数で展開）
        Runtime,       // 実行時展開（動的にトリップカウントを判定）
        Peel,          // ピーリング（最初/最後の反復を分離）
        PeelAndUnroll  // ピーリング + 部分展開
    };

    /// ループ解析と展開

    /// 簡易版ループの展開
    bool unrollLoopSimple(llvm::Loop* loop, llvm::DominatorTree& DT);

    /// 簡易版ループ解析
    UnrollInfo analyzeLoopSimple(llvm::Loop* loop);

    /// トリップカウントの簡易推定
    unsigned estimateTripCountSimple(llvm::Loop* loop);

    /// 展開戦略の決定
    UnrollStrategy determineStrategy(const UnrollInfo& info);

    /// 展開係数の計算
    unsigned calculateUnrollFactor(const UnrollInfo& info);

    /// レジスタ圧力の推定
    unsigned estimateRegisterPressure(llvm::Loop* loop);

    /// ループサイズの計算
    unsigned calculateLoopSize(llvm::Loop* loop);

    /// 展開実行

    /// 完全展開
    bool performCompleteUnroll(llvm::Loop* loop, unsigned tripCount, llvm::DominatorTree& DT);

    /// 部分展開
    bool performPartialUnroll(llvm::Loop* loop, unsigned unrollFactor, llvm::DominatorTree& DT);

    /// 実行時展開（トリップカウントが動的な場合）
    bool performRuntimeUnroll(llvm::Loop* loop, unsigned unrollFactor, llvm::IRBuilder<>& builder,
                              llvm::DominatorTree& DT);

    /// ループピーリング（最初のN回の反復を分離）
    bool performPeeling(llvm::Loop* loop, unsigned peelCount, llvm::DominatorTree& DT);

    // 将来の実装のためのプレースホルダー

    /// ソフトウェアパイプライニング

    /// モジュロスケジューリングによるソフトウェアパイプライニング
    bool performSoftwarePipelining(llvm::Loop* loop, llvm::DominatorTree& DT);

    /// ステージの計算
    std::vector<std::vector<llvm::Instruction*>> computeStages(llvm::Loop* loop);

    /// プロローグ・カーネル・エピローグの生成
    bool generatePipelinedLoop(llvm::Loop* loop,
                               const std::vector<std::vector<llvm::Instruction*>>& stages,
                               llvm::DominatorTree& DT);

    /// ヘルパー関数

    /// ループが正規化されているかチェック
    bool isLoopSimplified(llvm::Loop* loop);

    /// ループの正規化
    bool simplifyLoop(llvm::Loop* loop, llvm::DominatorTree& DT);

    /// ループ不変条件の検出
    bool hasLoopInvariantCondition(llvm::Loop* loop);

    /// サイドエフェクトのチェック
    bool hasSideEffects(llvm::Loop* loop);

    /// メモリ依存性のチェック
    bool hasMemoryDependency(llvm::Loop* loop);

    /// コスト/利益分析

    /// 展開のコスト計算
    unsigned calculateUnrollCost(const UnrollInfo& info);

    /// 展開の利益計算
    unsigned calculateUnrollBenefit(const UnrollInfo& info);

    /// キャッシュへの影響を推定
    int estimateCacheImpact(llvm::Loop* loop, unsigned unrollFactor);

    /// 命令レベル並列性の向上を推定
    unsigned estimateILPImprovement(llvm::Loop* loop, unsigned unrollFactor);

    /// 統計情報を出力
    void printStatistics() const;
    void debugPrintStatistics() const;

    /// デバッグ情報
    void debugPrintLoop(llvm::Loop* loop, const std::string& message);
    void debugPrintUnrollInfo(const UnrollInfo& info);
};

}  // namespace cm::codegen::llvm_backend::optimizations