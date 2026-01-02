#pragma once

#include <llvm/Analysis/ValueTracking.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <map>
#include <set>

namespace cm::codegen::llvm_backend::optimizations {

/// Instruction Combiner - 複数の命令を効率的な形に結合
class InstructionCombiner {
   public:
    /// 関数に対してInstruction Combining最適化を実行
    bool runOnFunction(llvm::Function& func);

   private:
    /// 基本ブロック内での命令結合
    bool combineInBasicBlock(llvm::BasicBlock& bb);

    /// パターンベースの命令結合

    /// 連続するGEP命令を結合
    bool combineGEPs(llvm::Instruction* inst);

    /// 分配法則を適用 (a*b + a*c -> a*(b+c))
    bool applyDistributiveLaw(llvm::Instruction* inst);

    /// 結合法則を適用して定数をまとめる ((x+1)+2 -> x+3)
    bool reassociateConstants(llvm::Instruction* inst);

    /// 比較とブランチを結合 (cmp + br -> br with condition)
    bool combineCompareAndBranch(llvm::Instruction* inst);

    /// Phi nodeと演算を結合 (phi後の共通演算をphi前に移動)
    bool combinePHIWithOperation(llvm::Instruction* inst);

    /// メモリアクセスパターンを最適化
    bool optimizeMemoryAccess(llvm::Instruction* inst);

    /// ビット演算の結合 ((x & m1) & m2 -> x & (m1 & m2))
    bool combineBitOperations(llvm::Instruction* inst);

    /// Min/Max演算の認識と最適化
    bool recognizeMinMax(llvm::Instruction* inst);

    /// 符号拡張・ゼロ拡張の結合
    bool combineExtensions(llvm::Instruction* inst);

    /// Freeze命令の伝播と削除
    bool eliminateFreezeInstructions(llvm::Instruction* inst);

    /// ヘルパー関数

    /// 命令が定数畳み込み可能かチェック
    bool canFoldConstants(llvm::Instruction* inst) const;

    /// 命令の使用者が全て同じ基本ブロック内かチェック
    bool allUsersInSameBlock(llvm::Instruction* inst) const;

    /// 値が既知の範囲内にあるかチェック
    bool isValueInRange(llvm::Value* val, int64_t min, int64_t max) const;

    /// 統計情報
    struct Stats {
        unsigned gepssCombined = 0;
        unsigned distributiveLawApplied = 0;
        unsigned constantsReassociated = 0;
        unsigned compareAndBranchCombined = 0;
        unsigned phiOperationsCombined = 0;
        unsigned memoryAccessOptimized = 0;
        unsigned bitOpsCombined = 0;
        unsigned minMaxRecognized = 0;
        unsigned extensionsCombined = 0;
        unsigned freezesEliminated = 0;
    } stats;

    /// 統計情報を出力
    void printStatistics() const;

    /// 作業用のIRBuilder
    std::unique_ptr<llvm::IRBuilder<>> builder;
};

}  // namespace cm::codegen::llvm_backend::optimizations