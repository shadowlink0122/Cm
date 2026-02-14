// 無限ループ検出とモジュール検証の実装
#include "loop_detector.hpp"

namespace cm::codegen::llvm_backend {

// モジュール全体の無限ループリスクを評価
bool InfiniteLoopDetector::detectInfiniteLoopRisk(llvm::Module& module) {
    size_t total_complexity = 0;

    for (auto& F : module) {
        if (F.isDeclaration())
            continue;

        auto complexity = analyzeFunction(F);
        total_complexity += complexity;

        if (hasObviousInfiniteLoop(F)) {
            return true;
        }
    }

    if (total_complexity > loop_detector_limits::MAX_COMPLEXITY_SCORE) {
        return true;
    }

    return false;
}

// 関数の複雑度を分析
size_t InfiniteLoopDetector::analyzeFunction(llvm::Function& F) {
    size_t complexity = 0;
    std::map<llvm::BasicBlock*, size_t> block_complexity;

    for (auto& BB : F) {
        size_t bb_complexity = BB.size();

        if (auto* br = llvm::dyn_cast<llvm::BranchInst>(BB.getTerminator())) {
            if (br->isConditional()) {
                bb_complexity *= 2;
            }
        }

        for (auto& I : BB) {
            if (llvm::isa<llvm::PHINode>(I)) {
                bb_complexity += 5;
            }
        }

        block_complexity[&BB] = bb_complexity;
        complexity += bb_complexity;
    }

    size_t loop_depth = estimateMaxLoopDepth(F);
    if (loop_depth > 0) {
        complexity *= (1 + loop_depth);
    }

    return complexity;
}

// 明らかな無限ループパターンを検出
bool InfiniteLoopDetector::hasObviousInfiniteLoop(llvm::Function& F) {
    for (auto& BB : F) {
        for (auto* succ : llvm::successors(&BB)) {
            if (succ == &BB) {
                if (auto* br = llvm::dyn_cast<llvm::BranchInst>(BB.getTerminator())) {
                    if (!br->isConditional()) {
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

// 強連結成分の一部かチェック（簡易版）
bool InfiniteLoopDetector::isPartOfInfiniteSCC(llvm::BasicBlock* BB) {
    std::set<llvm::BasicBlock*> visited;
    std::set<llvm::BasicBlock*> in_path;

    return hasCycle(BB, visited, in_path);
}

// サイクル検出（DFS）
bool InfiniteLoopDetector::hasCycle(llvm::BasicBlock* BB, std::set<llvm::BasicBlock*>& visited,
                                    std::set<llvm::BasicBlock*>& in_path) {
    visited.insert(BB);
    in_path.insert(BB);

    for (auto* succ : llvm::successors(BB)) {
        if (in_path.count(succ)) {
            if (!hasExitFromCycle(BB, succ)) {
                return true;
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
bool InfiniteLoopDetector::hasExitFromCycle(llvm::BasicBlock* start, llvm::BasicBlock* end) {
    std::set<llvm::BasicBlock*> cycle_blocks;
    collectCycleBlocks(start, end, cycle_blocks);

    for (auto* BB : cycle_blocks) {
        for (auto* succ : llvm::successors(BB)) {
            if (cycle_blocks.count(succ) == 0) {
                return true;
            }
        }
    }

    return false;
}

// サイクル内のブロックを収集
void InfiniteLoopDetector::collectCycleBlocks(llvm::BasicBlock* start, llvm::BasicBlock* end,
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
size_t InfiniteLoopDetector::estimateMaxLoopDepth(llvm::Function& F) {
    size_t max_depth = 0;

    for (auto& BB : F) {
        size_t phi_count = 0;
        for (auto& I : BB) {
            if (llvm::isa<llvm::PHINode>(I)) {
                phi_count++;
            }
        }

        if (phi_count > 0) {
            max_depth = std::max(max_depth, (size_t)(phi_count / 2 + 1));
        }
    }

    return max_depth;
}

// モジュール事前検証
bool PreCodeGenValidator::validate(llvm::Module& module) {
    if (module.empty()) {
        std::cerr << "Error: Empty module\n";
        return false;
    }

    if (InfiniteLoopDetector::detectInfiniteLoopRisk(module)) {
        std::cerr << "Error: Infinite loop risk detected\n";
        std::cerr << "Hint: Try -O1 or -O0 option\n";
        return false;
    }

    size_t huge_functions = 0;
    for (auto& F : module) {
        if (F.isDeclaration())
            continue;

        size_t inst_count = 0;
        for (auto& BB : F) {
            inst_count += BB.size();
        }

        if (inst_count > loop_detector_limits::MAX_INSTRUCTION_COUNT) {
            huge_functions++;
        }
    }

    if (huge_functions > loop_detector_limits::HUGE_FUNCTION_LIMIT) {
        std::cerr << "Error: Too many huge functions (" << huge_functions << ")\n";
        return false;
    }

    return true;
}

}  // namespace cm::codegen::llvm_backend
