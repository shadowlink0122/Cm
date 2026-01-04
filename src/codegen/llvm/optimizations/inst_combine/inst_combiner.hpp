#pragma once

#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/raw_ostream.h>
#include <unordered_map>
#include <vector>

namespace cm::codegen::llvm_backend::optimizations {

/// 命令結合最適化 - 複数の命令を単一のより効率的な命令に結合
class InstCombiner {
   public:
    /// 設定
    struct Config {
        bool enableAlgebraicSimplification;
        bool enableConstantFolding;
        bool enableBitPatterns;
        bool enableComparisonSimplification;
        bool enableSelectOptimization;
        bool enableCastOptimization;
        bool enableMemoryOptimization;
        bool enablePhiOptimization;
        unsigned maxIterations;

        // デフォルトコンストラクタ
        Config()
            : enableAlgebraicSimplification(true),
              enableConstantFolding(true),
              enableBitPatterns(true),
              enableComparisonSimplification(true),
              enableSelectOptimization(true),
              enableCastOptimization(true),
              enableMemoryOptimization(true),
              enablePhiOptimization(true),
              maxIterations(2) {}
    };

    explicit InstCombiner(const Config& config = Config()) : config(config) {}

    /// 関数内の命令を結合
    bool combineInstructions(llvm::Function& func);

    /// 統計情報
    struct Stats {
        unsigned instructionsCombined = 0;
        unsigned instructionsSimplified = 0;
        unsigned constantsFolded = 0;
        unsigned strengthReductions = 0;
        unsigned comparisonsSimplified = 0;
        unsigned selectsOptimized = 0;
        unsigned castsOptimized = 0;
        unsigned phisOptimized = 0;
    };

    const Stats& getStats() const { return stats; }

   private:
    Config config;
    Stats stats;

    /// 個別の命令を最適化
    bool combineInstruction(llvm::Instruction* inst);

    /// 最適化パターン

    /// 代数的簡約化
    /// X + 0 => X
    /// X * 1 => X
    /// X - X => 0
    bool simplifyAlgebraic(llvm::Instruction* inst);

    /// 定数畳み込み
    /// const1 + const2 => const3
    bool foldConstants(llvm::Instruction* inst);

    /// ビットパターンの最適化
    /// (X & C1) | (X & C2) => X & (C1 | C2)
    bool optimizeBitPatterns(llvm::Instruction* inst);

    /// 比較の簡約化
    /// X == X => true
    /// X < X => false
    bool simplifyComparisons(llvm::Instruction* inst);

    /// Select命令の最適化
    /// select true, A, B => A
    /// select C, X, X => X
    bool optimizeSelect(llvm::Instruction* inst);

    /// キャスト最適化
    /// cast(cast(X, T1), T2) => cast(X, T2)
    bool optimizeCasts(llvm::Instruction* inst);

    /// メモリ操作の最適化
    /// load(store(X, ptr), ptr) => X
    bool optimizeMemory(llvm::Instruction* inst);

    /// PHIノードの最適化
    /// phi [X, BB1], [X, BB2] => X
    bool optimizePhi(llvm::Instruction* inst);

    /// ヘルパー関数
    bool isPowerOf2_64(uint64_t value);

    /// 統計情報を出力
    void printStatistics() const;
};

}  // namespace cm::codegen::llvm_backend::optimizations