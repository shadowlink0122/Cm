#pragma once

#include <iostream>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/LoopPass.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/Pass.h>
#include <map>
#include <set>
#include <string>

namespace cm::codegen::llvm_backend {

// 無限ループ検出定数
namespace loop_detector_limits {
constexpr size_t MAX_COMPLEXITY_SCORE = 100000;
constexpr size_t MAX_INSTRUCTION_COUNT = 10000;
constexpr size_t HUGE_FUNCTION_LIMIT = 5;
}  // namespace loop_detector_limits

// 無限ループ検出クラス
class InfiniteLoopDetector {
   private:
    // ループの複雑度を評価
    struct LoopComplexity {
        size_t depth;
        size_t block_count;
        size_t instruction_count;
        bool has_exit_condition;
        bool has_side_effects;

        size_t complexity_score() const {
            size_t score = block_count * instruction_count;
            if (depth > 1)
                score *= depth;
            if (!has_exit_condition)
                score *= 10;
            if (!has_side_effects)
                score *= 5;
            return score;
        }

        bool is_likely_infinite() const {
            if (!has_exit_condition && !has_side_effects) {
                return true;
            }
            if (complexity_score() > loop_detector_limits::MAX_COMPLEXITY_SCORE) {
                return true;
            }
            return false;
        }
    };

   public:
    /// モジュール全体の無限ループリスクを評価
    static bool detectInfiniteLoopRisk(llvm::Module& module);

   private:
    /// 関数の複雑度を分析
    static size_t analyzeFunction(llvm::Function& F);

    /// 明らかな無限ループパターンを検出
    static bool hasObviousInfiniteLoop(llvm::Function& F);

    /// 強連結成分の一部かチェック（簡易版）
    static bool isPartOfInfiniteSCC(llvm::BasicBlock* BB);

    /// サイクル検出（DFS）
    static bool hasCycle(llvm::BasicBlock* BB, std::set<llvm::BasicBlock*>& visited,
                         std::set<llvm::BasicBlock*>& in_path);

    /// サイクルから脱出可能かチェック
    static bool hasExitFromCycle(llvm::BasicBlock* start, llvm::BasicBlock* end);

    /// サイクル内のブロックを収集
    static void collectCycleBlocks(llvm::BasicBlock* start, llvm::BasicBlock* end,
                                   std::set<llvm::BasicBlock*>& blocks);

    /// 最大ループ深度を推定
    static size_t estimateMaxLoopDepth(llvm::Function& F);
};

// LLVMモジュールの事前検証
class PreCodeGenValidator {
   public:
    /// モジュール検証
    static bool validate(llvm::Module& module);
};

}  // namespace cm::codegen::llvm_backend