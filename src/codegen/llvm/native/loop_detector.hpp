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

// 無限ループ検出クラス
class InfiniteLoopDetector {
   private:
    // ループの複雑度を評価
    struct LoopComplexity {
        size_t depth;              // ネストの深さ
        size_t block_count;        // ループ内のブロック数
        size_t instruction_count;  // ループ内の命令数
        bool has_exit_condition;   // 終了条件があるか
        bool has_side_effects;     // 副作用があるか

        size_t complexity_score() const {
            // 複雑度スコアを計算
            size_t score = block_count * instruction_count;
            if (depth > 1)
                score *= depth;  // ネストループは複雑度を増大
            if (!has_exit_condition)
                score *= 10;  // 終了条件なしは危険
            if (!has_side_effects)
                score *= 5;  // 副作用なしも危険（最適化で無限ループ化しやすい）
            return score;
        }

        bool is_likely_infinite() const {
            // 明らかな無限ループパターンを検出
            if (!has_exit_condition && !has_side_effects) {
                return true;  // while(true) {} のようなパターン
            }

            // 複雑度が異常に高い
            if (complexity_score() > 100000) {
                return true;
            }

            return false;
        }
    };

   public:
    // モジュール全体の無限ループリスクを評価
    static bool detectInfiniteLoopRisk(llvm::Module& module) {
        size_t total_complexity = 0;

        for (auto& F : module) {
            if (F.isDeclaration())
                continue;

            auto complexity = analyzeFunction(F);
            total_complexity += complexity;

            // 明らかな無限ループパターンを検出
            if (hasObviousInfiniteLoop(F)) {
                return true;
            }
        }

        // 全体の複雑度が異常に高い
        if (total_complexity > 100000) {
            return true;
        }

        return false;
    }

   private:
    // 関数の複雑度を分析
    static size_t analyzeFunction(llvm::Function& F) {
        size_t complexity = 0;
        std::map<llvm::BasicBlock*, size_t> block_complexity;

        // 各基本ブロックの複雑度を計算
        for (auto& BB : F) {
            size_t bb_complexity = BB.size();  // 命令数

            // 分岐命令の複雑度を追加
            if (auto* br = llvm::dyn_cast<llvm::BranchInst>(BB.getTerminator())) {
                if (br->isConditional()) {
                    bb_complexity *= 2;  // 条件分岐は複雑度を増加
                }
            }

            // PHIノードの複雑度を追加（ループの指標）
            for (auto& I : BB) {
                if (llvm::isa<llvm::PHINode>(I)) {
                    bb_complexity += 5;
                }
            }

            block_complexity[&BB] = bb_complexity;
            complexity += bb_complexity;
        }

        // ループの深さによる複雑度の調整
        size_t loop_depth = estimateMaxLoopDepth(F);
        if (loop_depth > 0) {
            complexity *= (1 + loop_depth);
        }

        return complexity;
    }

    // 明らかな無限ループパターンを検出
    // 注: LLVM最適化後のIRでは、forループなどが誤検出される可能性があるため、
    //     最も明らかなケース（無条件の自己ループ）のみを検出する
    static bool hasObviousInfiniteLoop(llvm::Function& F) {
        for (auto& BB : F) {
            // 自己ループをチェック
            for (auto* succ : llvm::successors(&BB)) {
                if (succ == &BB) {
                    // 自己ループが見つかった
                    // 脱出条件があるかチェック
                    if (auto* br = llvm::dyn_cast<llvm::BranchInst>(BB.getTerminator())) {
                        if (!br->isConditional()) {
                            // 無条件の自己ループ = 無限ループ
                            return true;
                        }
                    }
                }
            }

            // 強連結成分（SCC）による無限ループチェックは誤検出が多いため無効化
            // LLVM最適化後のIRでは正常なループも複雑な形に変換されることがある
            // if (isPartOfInfiniteSCC(&BB)) {
            //     return true;
            // }
        }

        return false;
    }

    // 強連結成分の一部かチェック（簡易版）
    static bool isPartOfInfiniteSCC(llvm::BasicBlock* BB) {
        std::set<llvm::BasicBlock*> visited;
        std::set<llvm::BasicBlock*> in_path;

        return hasCycle(BB, visited, in_path);
    }

    // サイクル検出（DFS）
    static bool hasCycle(llvm::BasicBlock* BB, std::set<llvm::BasicBlock*>& visited,
                         std::set<llvm::BasicBlock*>& in_path) {
        visited.insert(BB);
        in_path.insert(BB);

        for (auto* succ : llvm::successors(BB)) {
            if (in_path.count(succ)) {
                // バックエッジが見つかった（サイクル検出）
                // 脱出可能かチェック
                if (!hasExitFromCycle(BB, succ)) {
                    return true;  // 脱出不可能な無限ループ
                }
            }

            if (!visited.count(succ)) {
                if (hasCycle(succ, visited, in_path)) {
                    return true;
                }
            }
        }

        in_path.erase(BB);
        return false;
    }

    // サイクルから脱出可能かチェック
    static bool hasExitFromCycle(llvm::BasicBlock* start, llvm::BasicBlock* end) {
        // サイクル内のブロックを収集
        std::set<llvm::BasicBlock*> cycle_blocks;
        collectCycleBlocks(start, end, cycle_blocks);

        // サイクル外への脱出パスがあるかチェック
        for (auto* BB : cycle_blocks) {
            for (auto* succ : llvm::successors(BB)) {
                if (cycle_blocks.count(succ) == 0) {
                    // サイクル外への脱出パスが見つかった
                    return true;
                }
            }
        }

        return false;
    }

    // サイクル内のブロックを収集
    static void collectCycleBlocks(llvm::BasicBlock* start, llvm::BasicBlock* end,
                                   std::set<llvm::BasicBlock*>& blocks) {
        if (start == end) {
            blocks.insert(start);
            return;
        }

        blocks.insert(start);
        for (auto* succ : llvm::successors(start)) {
            if (blocks.count(succ) == 0) {
                collectCycleBlocks(succ, end, blocks);
            }
        }
    }

    // 最大ループ深度を推定
    static size_t estimateMaxLoopDepth(llvm::Function& F) {
        size_t max_depth = 0;

        // PHIノードの数からループの深さを推定（簡易版）
        for (auto& BB : F) {
            size_t phi_count = 0;
            for (auto& I : BB) {
                if (llvm::isa<llvm::PHINode>(I)) {
                    phi_count++;
                }
            }

            // PHIノードが多い = ループの可能性
            if (phi_count > 0) {
                max_depth = std::max(max_depth, (size_t)(phi_count / 2 + 1));
            }
        }

        return max_depth;
    }
};

// LLVMモジュールの事前検証
class PreCodeGenValidator {
   public:
    static bool validate(llvm::Module& module) {
        // 1. 基本的な検証
        if (module.empty()) {
            std::cerr << "Error: Empty module\n";
            return false;
        }

        // 2. 無限ループリスクの検出
        if (InfiniteLoopDetector::detectInfiniteLoopRisk(module)) {
            std::cerr << "Error: Infinite loop risk detected\n";
            std::cerr << "Hint: Try -O1 or -O0 option\n";
            return false;
        }

        // 3. 関数の複雑度チェック
        size_t huge_functions = 0;
        for (auto& F : module) {
            if (F.isDeclaration())
                continue;

            size_t inst_count = 0;
            for (auto& BB : F) {
                inst_count += BB.size();
            }

            if (inst_count > 10000) {
                huge_functions++;
            }
        }

        if (huge_functions > 5) {
            std::cerr << "Error: Too many huge functions (" << huge_functions << ")\n";
            return false;
        }

        return true;
    }
};

}  // namespace cm::codegen::llvm_backend