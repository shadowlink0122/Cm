#pragma once

#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>

namespace cm::codegen::llvm_backend::optimizations {

/// Peephole最適化 - 小さな命令パターンの最適化
class PeepholeOptimizer {
   public:
    struct Config {
        bool enableIdentityElimination;
        bool enableStrengthReduction;
        bool enableConstantFolding;
        unsigned maxIterations;

        Config()
            : enableIdentityElimination(true),
              enableStrengthReduction(true),
              enableConstantFolding(true),
              maxIterations(1) {}
    };

    explicit PeepholeOptimizer(const Config& config = Config()) : config(config) {}

    bool optimizeFunction(llvm::Function& func);

    struct Stats {
        unsigned identitiesEliminated = 0;
        unsigned strengthReductions = 0;
        unsigned constantFolds = 0;
    };

    const Stats& getStats() const { return stats; }

   private:
    Config config;
    Stats stats;

    // 最適化メソッド
    bool eliminateIdentity(llvm::Instruction* inst);
    bool performStrengthReduction(llvm::Instruction* inst);
    bool foldConstants(llvm::Instruction* inst);
    bool eliminateRedundantCast(llvm::Instruction* inst);
    bool simplifyComparison(llvm::Instruction* inst);
    bool optimizeMemoryAccess(llvm::Instruction* inst);

    // ヘルパー関数
    bool isZero(llvm::Value* v);
    bool isOne(llvm::Value* v);
    bool isMinusOne(llvm::Value* v);
    bool isAllOnes(llvm::Value* v);
    bool canEliminateIntermediateCast(llvm::CastInst* first, llvm::CastInst* second);
};

}  // namespace cm::codegen::llvm_backend::optimizations
