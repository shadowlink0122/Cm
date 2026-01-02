#include "instruction_combiner.hpp"

#include <iostream>
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/PatternMatch.h>

using namespace llvm;
using namespace llvm::PatternMatch;

namespace cm::codegen::llvm_backend::optimizations {

bool InstructionCombiner::runOnFunction(Function& func) {
    bool changed = false;
    builder = std::make_unique<IRBuilder<>>(func.getContext());

    // 収束するまで繰り返し実行
    bool localChanged;
    do {
        localChanged = false;
        for (auto& bb : func) {
            localChanged |= combineInBasicBlock(bb);
        }
        changed |= localChanged;
    } while (localChanged);

#ifdef DEBUG
    printStatistics();
#endif

    return changed;
}

bool InstructionCombiner::combineInBasicBlock(BasicBlock& bb) {
    bool changed = false;
    SmallVector<Instruction*, 16> toDelete;

    for (auto& inst : bb) {
        // 削除予定の命令はスキップ
        if (std::find(toDelete.begin(), toDelete.end(), &inst) != toDelete.end()) {
            continue;
        }

        // 各種結合最適化を試行
        if (combineGEPs(&inst)) {
            changed = true;
        } else if (applyDistributiveLaw(&inst)) {
            changed = true;
        } else if (reassociateConstants(&inst)) {
            changed = true;
        } else if (combineCompareAndBranch(&inst)) {
            changed = true;
        } else if (combinePHIWithOperation(&inst)) {
            changed = true;
        } else if (optimizeMemoryAccess(&inst)) {
            changed = true;
        } else if (combineBitOperations(&inst)) {
            changed = true;
        } else if (recognizeMinMax(&inst)) {
            changed = true;
        } else if (combineExtensions(&inst)) {
            changed = true;
        } else if (eliminateFreezeInstructions(&inst)) {
            changed = true;
        }
    }

    // 削除予定の命令を削除
    for (auto* inst : toDelete) {
        if (!inst->use_empty()) {
            inst->replaceAllUsesWith(UndefValue::get(inst->getType()));
        }
        inst->eraseFromParent();
    }

    return changed;
}

bool InstructionCombiner::combineGEPs(Instruction* inst) {
    auto* gep = dyn_cast<GetElementPtrInst>(inst);
    if (!gep)
        return false;

    // 前の命令もGEPかチェック
    auto* prevGep = dyn_cast<GetElementPtrInst>(gep->getPointerOperand());
    if (!prevGep)
        return false;

    // 同じ基本ブロック内でのみ結合
    if (prevGep->getParent() != gep->getParent())
        return false;

    // インデックスを結合
    SmallVector<Value*, 8> combinedIndices;

    // 最初のGEPのインデックスをコピー
    for (auto& idx : prevGep->indices()) {
        combinedIndices.push_back(idx);
    }

    // 2番目のGEPのインデックスを追加（最初の0は除く）
    bool first = true;
    for (auto& idx : gep->indices()) {
        if (first && isa<ConstantInt>(idx) && cast<ConstantInt>(idx)->isZero()) {
            first = false;
            continue;
        }
        combinedIndices.push_back(idx);
    }

    // 新しい結合GEPを作成
    builder->SetInsertPoint(gep);
    auto* newGep = builder->CreateGEP(prevGep->getSourceElementType(), prevGep->getPointerOperand(),
                                      combinedIndices);

    gep->replaceAllUsesWith(newGep);
    stats.gepssCombined++;
    return true;
}

bool InstructionCombiner::applyDistributiveLaw(Instruction* inst) {
    // a*b + a*c -> a*(b+c) パターンを検出
    Value *A1, *B, *A2, *C;

    if (match(inst, m_Add(m_Mul(m_Value(A1), m_Value(B)), m_Mul(m_Value(A2), m_Value(C))))) {
        if (A1 == A2) {
            // 共通因子Aを抽出
            builder->SetInsertPoint(inst);
            auto* sum = builder->CreateAdd(B, C);
            auto* result = builder->CreateMul(A1, sum);
            inst->replaceAllUsesWith(result);
            stats.distributiveLawApplied++;
            return true;
        }
        if (A1 == C) {
            // a*b + c*a -> a*(b+c)
            builder->SetInsertPoint(inst);
            auto* sum = builder->CreateAdd(B, A2);
            auto* result = builder->CreateMul(A1, sum);
            inst->replaceAllUsesWith(result);
            stats.distributiveLawApplied++;
            return true;
        }
    }

    // a*b - a*c -> a*(b-c) パターン
    if (match(inst, m_Sub(m_Mul(m_Value(A1), m_Value(B)), m_Mul(m_Value(A2), m_Value(C))))) {
        if (A1 == A2) {
            builder->SetInsertPoint(inst);
            auto* diff = builder->CreateSub(B, C);
            auto* result = builder->CreateMul(A1, diff);
            inst->replaceAllUsesWith(result);
            stats.distributiveLawApplied++;
            return true;
        }
    }

    return false;
}

bool InstructionCombiner::reassociateConstants(Instruction* inst) {
    // (x + C1) + C2 -> x + (C1 + C2) パターン
    Value *X, *Y;
    ConstantInt *C1, *C2;

    // 加算の場合
    if (match(inst, m_Add(m_Add(m_Value(X), m_ConstantInt(C1)), m_ConstantInt(C2)))) {
        builder->SetInsertPoint(inst);
        auto* sumConst = ConstantInt::get(C1->getType(), C1->getValue() + C2->getValue());
        auto* result = builder->CreateAdd(X, sumConst);
        inst->replaceAllUsesWith(result);
        stats.constantsReassociated++;
        return true;
    }

    // 乗算の場合
    if (match(inst, m_Mul(m_Mul(m_Value(X), m_ConstantInt(C1)), m_ConstantInt(C2)))) {
        builder->SetInsertPoint(inst);
        auto* productConst = ConstantInt::get(C1->getType(), C1->getValue() * C2->getValue());
        auto* result = builder->CreateMul(X, productConst);
        inst->replaceAllUsesWith(result);
        stats.constantsReassociated++;
        return true;
    }

    // (x << C1) << C2 -> x << (C1 + C2)
    if (match(inst, m_Shl(m_Shl(m_Value(X), m_ConstantInt(C1)), m_ConstantInt(C2)))) {
        builder->SetInsertPoint(inst);
        auto* totalShift = ConstantInt::get(C1->getType(), C1->getValue() + C2->getValue());
        auto* result = builder->CreateShl(X, totalShift);
        inst->replaceAllUsesWith(result);
        stats.constantsReassociated++;
        return true;
    }

    return false;
}

bool InstructionCombiner::combineCompareAndBranch(Instruction* inst) {
    auto* br = dyn_cast<BranchInst>(inst);
    if (!br || !br->isConditional())
        return false;

    // 条件がICmp命令の直接の結果かチェック
    auto* cmp = dyn_cast<ICmpInst>(br->getCondition());
    if (!cmp)
        return false;

    // cmpが他で使われていない場合のみ最適化
    if (!cmp->hasOneUse())
        return false;

    // 同じ基本ブロック内かチェック
    if (cmp->getParent() != br->getParent())
        return false;

    // Switch命令への変換を検討（複数の比較がある場合）
    // この実装では簡単な最適化のみ

    // 特定のパターンの最適化
    // x == 0 ? true_bb : false_bb を最適化
    Value* X;
    if (match(cmp, m_ICmp(ICmpInst::ICMP_EQ, m_Value(X), m_Zero()))) {
        // 将来的な最適化のマーカーとして記録
        stats.compareAndBranchCombined++;
        return false;  // 実際の変更は行わない（今は分析のみ）
    }

    return false;
}

bool InstructionCombiner::combinePHIWithOperation(Instruction* inst) {
    // PHI node後の共通演算をPHI前に移動
    auto* phi = dyn_cast<PHINode>(inst);
    if (!phi || phi->getNumIncomingValues() < 2)
        return false;

    // PHIの全ての使用者をチェック
    Instruction* commonOp = nullptr;
    for (auto* user : phi->users()) {
        auto* userInst = dyn_cast<Instruction>(user);
        if (!userInst)
            return false;

        if (!commonOp) {
            commonOp = userInst;
        } else if (userInst->getOpcode() != commonOp->getOpcode()) {
            return false;  // 異なる演算
        }
    }

    if (!commonOp || phi->users().empty())
        return false;

    // 単純な二項演算のみ対応
    auto* binOp = dyn_cast<BinaryOperator>(commonOp);
    if (!binOp)
        return false;

    // PHI前で演算を実行
    PHINode* newPhi = PHINode::Create(binOp->getType(), phi->getNumIncomingValues(),
                                      phi->getName() + ".combined", phi);

    for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i) {
        builder->SetInsertPoint(phi->getIncomingBlock(i)->getTerminator());
        Value* val = phi->getIncomingValue(i);

        // 演算を実行
        Value* result = nullptr;
        if (binOp->getOperand(0) == phi) {
            result = builder->CreateBinOp(binOp->getOpcode(), val, binOp->getOperand(1));
        } else if (binOp->getOperand(1) == phi) {
            result = builder->CreateBinOp(binOp->getOpcode(), binOp->getOperand(0), val);
        }

        if (result) {
            newPhi->addIncoming(result, phi->getIncomingBlock(i));
        }
    }

    if (newPhi->getNumIncomingValues() == phi->getNumIncomingValues()) {
        binOp->replaceAllUsesWith(newPhi);
        stats.phiOperationsCombined++;
        return true;
    }

    newPhi->eraseFromParent();
    return false;
}

bool InstructionCombiner::optimizeMemoryAccess(Instruction* inst) {
    // 連続するメモリアクセスのベクトル化準備
    auto* load = dyn_cast<LoadInst>(inst);
    if (!load)
        return false;

    // 次の命令も同じアドレスの近くのloadかチェック
    auto* nextInst = inst->getNextNode();
    if (!nextInst)
        return false;

    auto* nextLoad = dyn_cast<LoadInst>(nextInst);
    if (!nextLoad)
        return false;

    // アドレスが連続しているかチェック（簡易版）
    auto* gep1 = dyn_cast<GetElementPtrInst>(load->getPointerOperand());
    auto* gep2 = dyn_cast<GetElementPtrInst>(nextLoad->getPointerOperand());

    if (gep1 && gep2 && gep1->getPointerOperand() == gep2->getPointerOperand()) {
        // 将来的なベクトル化のマーカー
        stats.memoryAccessOptimized++;
        return false;  // 実際の変更は行わない（今は分析のみ）
    }

    return false;
}

bool InstructionCombiner::combineBitOperations(Instruction* inst) {
    Value *X, *Y, *Z;
    ConstantInt *C1, *C2;

    // (x & C1) & C2 -> x & (C1 & C2)
    if (match(inst, m_And(m_And(m_Value(X), m_ConstantInt(C1)), m_ConstantInt(C2)))) {
        builder->SetInsertPoint(inst);
        auto* combinedMask = ConstantInt::get(C1->getType(), C1->getValue() & C2->getValue());
        auto* result = builder->CreateAnd(X, combinedMask);
        inst->replaceAllUsesWith(result);
        stats.bitOpsCombined++;
        return true;
    }

    // (x | C1) | C2 -> x | (C1 | C2)
    if (match(inst, m_Or(m_Or(m_Value(X), m_ConstantInt(C1)), m_ConstantInt(C2)))) {
        builder->SetInsertPoint(inst);
        auto* combinedMask = ConstantInt::get(C1->getType(), C1->getValue() | C2->getValue());
        auto* result = builder->CreateOr(X, combinedMask);
        inst->replaceAllUsesWith(result);
        stats.bitOpsCombined++;
        return true;
    }

    // (x ^ C1) ^ C2 -> x ^ (C1 ^ C2)
    if (match(inst, m_Xor(m_Xor(m_Value(X), m_ConstantInt(C1)), m_ConstantInt(C2)))) {
        builder->SetInsertPoint(inst);
        auto* combinedMask = ConstantInt::get(C1->getType(), C1->getValue() ^ C2->getValue());
        auto* result = builder->CreateXor(X, combinedMask);
        inst->replaceAllUsesWith(result);
        stats.bitOpsCombined++;
        return true;
    }

    // ~(~x) -> x (二重否定の除去)
    if (match(inst, m_Not(m_Not(m_Value(X))))) {
        inst->replaceAllUsesWith(X);
        stats.bitOpsCombined++;
        return true;
    }

    return false;
}

bool InstructionCombiner::recognizeMinMax(Instruction* inst) {
    // select (x < y), x, y -> min(x, y) パターンの認識
    auto* select = dyn_cast<SelectInst>(inst);
    if (!select)
        return false;

    auto* cmp = dyn_cast<ICmpInst>(select->getCondition());
    if (!cmp)
        return false;

    Value* x = cmp->getOperand(0);
    Value* y = cmp->getOperand(1);
    Value* trueVal = select->getTrueValue();
    Value* falseVal = select->getFalseValue();

    // min パターン: select (x < y), x, y
    if (cmp->getPredicate() == ICmpInst::ICMP_SLT && x == trueVal && y == falseVal) {
        // Min操作として認識（将来的な最適化用）
        stats.minMaxRecognized++;
        return false;  // 実際の変更は行わない
    }

    // max パターン: select (x > y), x, y
    if (cmp->getPredicate() == ICmpInst::ICMP_SGT && x == trueVal && y == falseVal) {
        // Max操作として認識（将来的な最適化用）
        stats.minMaxRecognized++;
        return false;  // 実際の変更は行わない
    }

    return false;
}

bool InstructionCombiner::combineExtensions(Instruction* inst) {
    // 連続する符号拡張・ゼロ拡張を結合

    // sext(sext(x)) -> sext(x) to final type
    if (auto* sext = dyn_cast<SExtInst>(inst)) {
        if (auto* prevSext = dyn_cast<SExtInst>(sext->getOperand(0))) {
            builder->SetInsertPoint(inst);
            auto* result = builder->CreateSExt(prevSext->getOperand(0), sext->getDestTy());
            inst->replaceAllUsesWith(result);
            stats.extensionsCombined++;
            return true;
        }
    }

    // zext(zext(x)) -> zext(x) to final type
    if (auto* zext = dyn_cast<ZExtInst>(inst)) {
        if (auto* prevZext = dyn_cast<ZExtInst>(zext->getOperand(0))) {
            builder->SetInsertPoint(inst);
            auto* result = builder->CreateZExt(prevZext->getOperand(0), zext->getDestTy());
            inst->replaceAllUsesWith(result);
            stats.extensionsCombined++;
            return true;
        }
    }

    // trunc(sext(x)) or trunc(zext(x)) -> x (if same type)
    if (auto* trunc = dyn_cast<TruncInst>(inst)) {
        Value* src = nullptr;
        if (auto* sext = dyn_cast<SExtInst>(trunc->getOperand(0))) {
            src = sext->getOperand(0);
        } else if (auto* zext = dyn_cast<ZExtInst>(trunc->getOperand(0))) {
            src = zext->getOperand(0);
        }

        if (src && src->getType() == trunc->getDestTy()) {
            inst->replaceAllUsesWith(src);
            stats.extensionsCombined++;
            return true;
        }
    }

    return false;
}

bool InstructionCombiner::eliminateFreezeInstructions(Instruction* inst) {
    auto* freeze = dyn_cast<FreezeInst>(inst);
    if (!freeze)
        return false;

    // Freeze of a constant -> constant
    if (isa<Constant>(freeze->getOperand(0))) {
        inst->replaceAllUsesWith(freeze->getOperand(0));
        stats.freezesEliminated++;
        return true;
    }

    // Freeze of freeze -> single freeze
    if (isa<FreezeInst>(freeze->getOperand(0))) {
        inst->replaceAllUsesWith(freeze->getOperand(0));
        stats.freezesEliminated++;
        return true;
    }

    return false;
}

bool InstructionCombiner::canFoldConstants(Instruction* inst) const {
    // 全てのオペランドが定数かチェック
    for (auto& op : inst->operands()) {
        if (!isa<Constant>(op)) {
            return false;
        }
    }
    return true;
}

bool InstructionCombiner::allUsersInSameBlock(Instruction* inst) const {
    BasicBlock* bb = inst->getParent();
    for (auto* user : inst->users()) {
        if (auto* userInst = dyn_cast<Instruction>(user)) {
            if (userInst->getParent() != bb) {
                return false;
            }
        }
    }
    return true;
}

bool InstructionCombiner::isValueInRange(Value* val, int64_t min, int64_t max) const {
    if (auto* constInt = dyn_cast<ConstantInt>(val)) {
        int64_t value = constInt->getSExtValue();
        return value >= min && value <= max;
    }
    return false;
}

void InstructionCombiner::printStatistics() const {
    std::cerr << "\n=== Instruction Combining Statistics ===\n";
    std::cerr << "  GEPs combined: " << stats.gepssCombined << "\n";
    std::cerr << "  Distributive law applied: " << stats.distributiveLawApplied << "\n";
    std::cerr << "  Constants reassociated: " << stats.constantsReassociated << "\n";
    std::cerr << "  Compare and branch combined: " << stats.compareAndBranchCombined << "\n";
    std::cerr << "  PHI operations combined: " << stats.phiOperationsCombined << "\n";
    std::cerr << "  Memory access optimized: " << stats.memoryAccessOptimized << "\n";
    std::cerr << "  Bit operations combined: " << stats.bitOpsCombined << "\n";
    std::cerr << "  Min/Max recognized: " << stats.minMaxRecognized << "\n";
    std::cerr << "  Extensions combined: " << stats.extensionsCombined << "\n";
    std::cerr << "  Freezes eliminated: " << stats.freezesEliminated << "\n";
    std::cerr << "========================================\n";
}

}  // namespace cm::codegen::llvm_backend::optimizations