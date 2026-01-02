#include "loop_unroller.hpp"

#include <llvm/Analysis/AssumptionCache.h>
#include <llvm/Analysis/LoopPass.h>
#include <llvm/IR/Constants.h>
#include <llvm/Support/Debug.h>
#include <llvm/Transforms/Utils/Cloning.h>

namespace cm::codegen::llvm_backend::optimizations {

bool LoopUnroller::unrollFunction(llvm::Function& func) {
    bool modified = false;

    // ループ情報の取得
    llvm::DominatorTree DT(func);
    llvm::LoopInfo LI(DT);

    // すべてのループを収集（内側のループから処理）
    std::vector<llvm::Loop*> loops;
    for (auto* loop : LI) {
        std::function<void(llvm::Loop*)> collectLoops = [&](llvm::Loop* L) {
            for (auto* subLoop : L->getSubLoops()) {
                collectLoops(subLoop);
            }
            loops.push_back(L);
        };
        collectLoops(loop);
    }

    // 各ループの展開を試みる
    for (auto* loop : loops) {
        if (unrollLoopSimple(loop, DT)) {
            modified = true;
        }
    }

    if (modified) {
        debugPrintStatistics();
    }

    return modified;
}

bool LoopUnroller::unrollLoopSimple(llvm::Loop* loop, llvm::DominatorTree& DT) {
    stats.loopsAnalyzed++;

    // 簡易的なループ解析
    UnrollInfo info = analyzeLoopSimple(loop);
    if (!info.canUnroll) {
        return false;
    }

    // 展開戦略の決定
    UnrollStrategy strategy = determineStrategy(info);
    if (strategy == UnrollStrategy::None) {
        return false;
    }

    // 戦略に応じた展開の実行
    bool result = false;
    switch (strategy) {
        case UnrollStrategy::Complete:
            result = performCompleteUnroll(loop, info.tripCount, DT);
            if (result)
                stats.loopsCompletelyUnrolled++;
            break;

        case UnrollStrategy::Partial:
            result = performPartialUnroll(loop, info.unrollFactor, DT);
            if (result)
                stats.loopsPartiallyUnrolled++;
            break;

        case UnrollStrategy::Runtime: {
            auto* preheader = loop->getLoopPreheader();
            if (preheader && preheader->getTerminator()) {
                llvm::IRBuilder<> builder(preheader->getTerminator());
                result = performRuntimeUnroll(loop, info.unrollFactor, builder, DT);
                if (result)
                    stats.loopsRuntimeUnrolled++;
            }
        } break;

        case UnrollStrategy::Peel:
            result = performPeeling(loop, 1, DT);  // 1回だけピーリング
            if (result)
                stats.loopsPeeled++;
            break;

        case UnrollStrategy::PeelAndUnroll:
            if (performPeeling(loop, 1, DT)) {
                result = performPartialUnroll(loop, info.unrollFactor, DT);
                if (result) {
                    stats.loopsPeeled++;
                    stats.loopsPartiallyUnrolled++;
                }
            }
            break;

        default:
            break;
    }

    return result;
}

LoopUnroller::UnrollInfo LoopUnroller::analyzeLoopSimple(llvm::Loop* loop) {
    UnrollInfo info;

    // 基本チェック
    if (!isLoopSimplified(loop)) {
        return info;
    }

    // トリップカウントの簡易推定
    info.tripCount = estimateTripCountSimple(loop);

    // ループサイズの計算
    info.loopSize = calculateLoopSize(loop);

    // サイドエフェクトのチェック
    if (hasSideEffects(loop)) {
        info.hasCallInLoop = true;
    }

    // メモリ依存性のチェック
    info.hasLoopCarriedDependency = hasMemoryDependency(loop);

    // レジスタ圧力の推定
    info.registerPressure = estimateRegisterPressure(loop);

    // 展開可能性の判定
    info.canUnroll = (info.loopSize > 0 && info.loopSize <= config.maxLoopSize);

    // 完全展開可能性の判定
    if (info.tripCount > 0 && info.tripCount <= config.maxUnrollFactor) {
        unsigned expandedSize = info.loopSize * info.tripCount;
        info.isFullyUnrollable = (expandedSize <= config.maxLoopSize * 2);
    }

    // 展開係数の計算
    info.unrollFactor = calculateUnrollFactor(info);

    return info;
}

LoopUnroller::UnrollStrategy LoopUnroller::determineStrategy(const UnrollInfo& info) {
    // 展開不可の場合
    if (!info.canUnroll || info.unrollFactor <= 1) {
        return UnrollStrategy::None;
    }

    // 完全展開が可能で有効な場合
    if (config.enableCompleteUnroll && info.isFullyUnrollable && info.tripCount > 0) {
        return UnrollStrategy::Complete;
    }

    // トリップカウントが不明の場合は実行時展開
    if (config.enableRuntimeUnroll && info.tripCount == 0) {
        return UnrollStrategy::Runtime;
    }

    // 部分展開
    if (config.enablePartialUnroll) {
        // ピーリングも有効ならピーリング + 展開
        if (config.enablePeeling && info.tripCount % info.unrollFactor != 0) {
            return UnrollStrategy::PeelAndUnroll;
        }
        return UnrollStrategy::Partial;
    }

    // ピーリングのみ
    if (config.enablePeeling) {
        return UnrollStrategy::Peel;
    }

    return UnrollStrategy::None;
}

unsigned LoopUnroller::calculateUnrollFactor(const UnrollInfo& info) {
    // 優先係数が指定されている場合
    if (config.preferredUnrollFactor > 0) {
        return std::min(config.preferredUnrollFactor, config.maxUnrollFactor);
    }

    // 完全展開可能な場合
    if (info.isFullyUnrollable && info.tripCount > 0) {
        return info.tripCount;
    }

    // 展開係数の自動決定
    unsigned factor = 1;

    // トリップカウントが既知の場合
    if (info.tripCount > 0) {
        // トリップカウントの約数から選択
        for (unsigned f = 2; f <= std::min(config.maxUnrollFactor, info.tripCount); ++f) {
            if (info.tripCount % f == 0) {
                unsigned expandedSize = info.loopSize * f;
                if (expandedSize <= config.maxLoopSize) {
                    factor = f;
                }
            }
        }
    } else {
        // トリップカウントが不明な場合はデフォルト値
        factor = std::min(4u, config.maxUnrollFactor);

        // ループサイズに基づく調整
        while (factor > 1 && info.loopSize * factor > config.maxLoopSize) {
            factor /= 2;
        }
    }

    return factor;
}

bool LoopUnroller::performCompleteUnroll(llvm::Loop* loop, unsigned tripCount,
                                         llvm::DominatorTree& DT) {
    if (tripCount == 0 || tripCount > config.maxUnrollFactor) {
        return false;
    }

    auto* header = loop->getHeader();
    auto* latch = loop->getLoopLatch();
    auto* preheader = loop->getLoopPreheader();
    auto* exit = loop->getExitBlock();

    if (!header || !latch || !preheader || !exit) {
        return false;
    }

    // ループ本体を複製
    std::vector<llvm::BasicBlock*> newBlocks;
    llvm::ValueToValueMapTy VMap;

    for (unsigned i = 0; i < tripCount - 1; ++i) {
        // ループ本体の各ブロックを複製
        for (auto* BB : loop->blocks()) {
            auto* newBB = llvm::CloneBasicBlock(BB, VMap, ".unroll" + std::to_string(i));
            header->getParent()->getBasicBlockList().push_back(newBB);
            newBlocks.push_back(newBB);

            // PHIノードの更新
            for (auto& I : *newBB) {
                if (auto* phi = llvm::dyn_cast<llvm::PHINode>(&I)) {
                    for (unsigned j = 0; j < phi->getNumIncomingValues(); ++j) {
                        auto* incomingVal = phi->getIncomingValue(j);
                        if (VMap.count(incomingVal)) {
                            phi->setIncomingValue(j, VMap[incomingVal]);
                        }
                    }
                }
            }
        }
    }

    // 最後の反復を元のループに接続
    auto* lastLatch = latch;
    if (!newBlocks.empty()) {
        lastLatch = newBlocks.back();
    }

    // ブランチ命令を更新してループを除去
    if (auto* br = llvm::dyn_cast<llvm::BranchInst>(lastLatch->getTerminator())) {
        br->setSuccessor(0, exit);
    }

    // DominatorTreeを更新
    DT.recalculate(*header->getParent());

    stats.totalInstructionsEliminated += 2;  // ループ条件チェックとブランチ

    return true;
}

bool LoopUnroller::performPartialUnroll(llvm::Loop* loop, unsigned unrollFactor,
                                        llvm::DominatorTree& DT) {
    if (unrollFactor <= 1) {
        return false;
    }

    auto* header = loop->getHeader();
    auto* latch = loop->getLoopLatch();
    auto* preheader = loop->getLoopPreheader();

    if (!header || !latch || !preheader) {
        return false;
    }

    // ループ本体を unrollFactor-1 回複製
    std::vector<llvm::BasicBlock*> unrolledBlocks;
    llvm::BasicBlock* previousLatch = latch;

    for (unsigned i = 1; i < unrollFactor; ++i) {
        llvm::ValueToValueMapTy VMap;

        // ループ本体の各ブロックを複製
        for (auto* BB : loop->blocks()) {
            auto* newBB = llvm::CloneBasicBlock(BB, VMap, ".unroll" + std::to_string(i));
            header->getParent()->getBasicBlockList().insert(header->getIterator(), newBB);
            unrolledBlocks.push_back(newBB);

            // 前の反復の最後を新しい反復の最初に接続
            if (BB == header && previousLatch) {
                auto* br = previousLatch->getTerminator();
                if (auto* condBr = llvm::dyn_cast<llvm::BranchInst>(br)) {
                    if (condBr->isConditional()) {
                        // ループ継続の分岐先を新しいヘッダーに変更
                        for (unsigned j = 0; j < condBr->getNumSuccessors(); ++j) {
                            if (condBr->getSuccessor(j) == header) {
                                condBr->setSuccessor(j, newBB);
                                break;
                            }
                        }
                    }
                }
            }

            // マッピングを更新
            VMap[BB] = newBB;
        }

        // 新しいラッチを記録
        if (VMap.count(latch)) {
            previousLatch = llvm::cast<llvm::BasicBlock>(VMap[latch]);
        }
    }

    // インダクション変数の更新を調整
    // （展開係数分だけステップを増やす）
    for (auto& phi : header->phis()) {
        if (auto* incVal =
                llvm::dyn_cast<llvm::BinaryOperator>(phi.getIncomingValueForBlock(latch))) {
            if (incVal->getOpcode() == llvm::Instruction::Add) {
                if (auto* stepVal = llvm::dyn_cast<llvm::ConstantInt>(incVal->getOperand(1))) {
                    // ステップを unrollFactor 倍にする
                    auto* newStep = llvm::ConstantInt::get(stepVal->getType(),
                                                           stepVal->getSExtValue() * unrollFactor);
                    incVal->setOperand(1, newStep);
                }
            }
        }
    }

    // DominatorTreeを更新
    DT.recalculate(*header->getParent());

    return true;
}

bool LoopUnroller::performRuntimeUnroll(llvm::Loop* loop, unsigned unrollFactor,
                                        llvm::IRBuilder<>& builder, llvm::DominatorTree& DT) {
    // 実行時展開は複雑なため、簡易実装
    // 実際にはトリップカウントを実行時に判定し、
    // メインループとエピローグループを生成する必要がある

    auto* header = loop->getHeader();
    auto* preheader = loop->getLoopPreheader();

    if (!header || !preheader) {
        return false;
    }

    // プロローグループの生成（剰余分の処理）
    // メインループ（展開済み）の生成
    // エピローグの結合

    // 簡易実装のため部分展開にフォールバック
    return performPartialUnroll(loop, unrollFactor, DT);
}

bool LoopUnroller::performPeeling(llvm::Loop* loop, unsigned peelCount, llvm::DominatorTree& DT) {
    if (peelCount == 0) {
        return false;
    }

    auto* header = loop->getHeader();
    auto* preheader = loop->getLoopPreheader();

    if (!header || !preheader) {
        return false;
    }

    // 最初の peelCount 回の反復を分離
    llvm::ValueToValueMapTy VMap;

    for (unsigned i = 0; i < peelCount; ++i) {
        for (auto* BB : loop->blocks()) {
            auto* peeledBB = llvm::CloneBasicBlock(BB, VMap, ".peel" + std::to_string(i));
            header->getParent()->getBasicBlockList().insert(header->getIterator(), peeledBB);
        }
    }

    // DominatorTreeを更新
    DT.recalculate(*header->getParent());

    return true;
}

// ヘルパー関数の実装

bool LoopUnroller::isLoopSimplified(llvm::Loop* loop) {
    // ループが正規化された形式かチェック
    return loop && loop->getLoopPreheader() && loop->getLoopLatch() && loop->getExitBlock() &&
           loop->getHeader();
}

unsigned LoopUnroller::estimateTripCountSimple(llvm::Loop* loop) {
    // 簡易的なトリップカウント推定
    // ループ条件から定数の比較を探す
    auto* latch = loop->getLoopLatch();
    if (!latch)
        return 0;

    auto* term = latch->getTerminator();
    if (auto* br = llvm::dyn_cast<llvm::BranchInst>(term)) {
        if (br->isConditional()) {
            if (auto* cmp = llvm::dyn_cast<llvm::ICmpInst>(br->getCondition())) {
                // 比較命令の右側が定数の場合
                if (auto* constVal = llvm::dyn_cast<llvm::ConstantInt>(cmp->getOperand(1))) {
                    int64_t val = constVal->getSExtValue();
                    // 0からN未満のループと仮定
                    if (val > 0 && val <= 100) {
                        return static_cast<unsigned>(val);
                    }
                }
            }
        }
    }

    return 0;  // 推定できない場合
}

unsigned LoopUnroller::calculateLoopSize(llvm::Loop* loop) {
    unsigned size = 0;
    for (auto* BB : loop->blocks()) {
        size += BB->size();
    }
    return size;
}

bool LoopUnroller::hasSideEffects(llvm::Loop* loop) {
    for (auto* BB : loop->blocks()) {
        for (auto& I : *BB) {
            if (I.mayHaveSideEffects()) {
                return true;
            }
            if (llvm::isa<llvm::CallInst>(I) || llvm::isa<llvm::InvokeInst>(I)) {
                return true;
            }
        }
    }
    return false;
}

bool LoopUnroller::hasMemoryDependency(llvm::Loop* loop) {
    // 簡易的なメモリ依存性チェック
    bool hasStore = false;
    bool hasLoad = false;

    for (auto* BB : loop->blocks()) {
        for (auto& I : *BB) {
            if (llvm::isa<llvm::StoreInst>(I)) {
                hasStore = true;
            }
            if (llvm::isa<llvm::LoadInst>(I)) {
                hasLoad = true;
            }
        }
    }

    return hasStore && hasLoad;
}

unsigned LoopUnroller::estimateRegisterPressure(llvm::Loop* loop) {
    // 簡易的なレジスタ圧力推定
    unsigned pressure = 0;

    for (auto* BB : loop->blocks()) {
        for (auto& I : *BB) {
            // 各命令が使用するレジスタ数の概算
            pressure += I.getNumOperands();
        }
    }

    return pressure / loop->getNumBlocks();
}

void LoopUnroller::debugPrintStatistics() const {
    llvm::dbgs() << "[LoopUnroller] Statistics:\n"
                 << "  Loops analyzed: " << stats.loopsAnalyzed << "\n"
                 << "  Completely unrolled: " << stats.loopsCompletelyUnrolled << "\n"
                 << "  Partially unrolled: " << stats.loopsPartiallyUnrolled << "\n"
                 << "  Runtime unrolled: " << stats.loopsRuntimeUnrolled << "\n"
                 << "  Peeled: " << stats.loopsPeeled << "\n"
                 << "  Instructions eliminated: " << stats.totalInstructionsEliminated << "\n";
}

void LoopUnroller::printStatistics() const {
    llvm::errs() << "[LoopUnroller] Statistics:\n"
                 << "  Loops analyzed: " << stats.loopsAnalyzed << "\n"
                 << "  Completely unrolled: " << stats.loopsCompletelyUnrolled << "\n"
                 << "  Partially unrolled: " << stats.loopsPartiallyUnrolled << "\n";
}

}  // namespace cm::codegen::llvm_backend::optimizations
