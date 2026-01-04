/// @file inst_combiner.cpp
/// @brief 命令結合最適化の実装

// LLVM列挙型の網羅性警告を抑制（処理しない値が多数あるため）
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"

#include "inst_combiner.hpp"

#include <algorithm>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/PatternMatch.h>
#include <llvm/Support/Debug.h>
#include <llvm/Support/raw_ostream.h>

using namespace llvm::PatternMatch;

namespace cm::codegen::llvm_backend::optimizations {

// 命令結合の実行
bool InstCombiner::combineInstructions(llvm::Function& func) {
    bool changed = false;
    unsigned iteration = 0;

    // 反復的に最適化を適用
    while (iteration < config.maxIterations) {
        bool iterationChanged = false;

        // 削除予定の命令を追跡
        std::vector<llvm::Instruction*> toDelete;

        for (auto& bb : func) {
            for (auto it = bb.begin(); it != bb.end();) {
                auto* inst = &*it++;

                // 既に処理済みまたは削除予定の命令はスキップ
                if (!inst->getParent())
                    continue;

                if (combineInstruction(inst)) {
                    iterationChanged = true;
                    stats.instructionsCombined++;

                    // 無効化された命令を削除リストに追加
                    if (llvm::isa<llvm::UndefValue>(inst) || inst->use_empty()) {
                        toDelete.push_back(inst);
                    }
                }
            }
        }

        // 削除予定の命令を実際に削除
        for (auto* inst : toDelete) {
            if (inst->getParent()) {
                inst->eraseFromParent();
            }
        }

        if (!iterationChanged) {
            break;  // 変更がなければ早期終了
        }

        changed = true;
        iteration++;
    }

    if (changed) {
        printStatistics();
    }

    return changed;
}

// 個別の命令を最適化
bool InstCombiner::combineInstruction(llvm::Instruction* inst) {
    if (!inst)
        return false;

    // 既に削除された命令をチェック
    if (!inst->getParent())
        return false;

    bool changed = false;

    // 各最適化を試行（順序重要：命令を削除する可能性があるものは最後に）
    if (config.enableAlgebraicSimplification && inst->getParent()) {
        changed |= simplifyAlgebraic(inst);
    }

    if (config.enableConstantFolding && inst->getParent()) {
        changed |= foldConstants(inst);
    }

    if (config.enableBitPatterns && inst->getParent()) {
        changed |= optimizeBitPatterns(inst);
    }

    if (config.enableComparisonSimplification && inst->getParent()) {
        changed |= simplifyComparisons(inst);
    }

    if (config.enableSelectOptimization && inst->getParent()) {
        changed |= optimizeSelect(inst);
    }

    if (config.enableCastOptimization && inst->getParent()) {
        changed |= optimizeCasts(inst);
    }

    if (config.enablePhiOptimization && inst->getParent()) {
        changed |= optimizePhi(inst);
    }

    // メモリ最適化は命令を削除する可能性があるため最後に実行
    if (config.enableMemoryOptimization && inst->getParent()) {
        changed |= optimizeMemory(inst);
    }

    return changed;
}

// 代数的簡約化
bool InstCombiner::simplifyAlgebraic(llvm::Instruction* inst) {
    if (auto* binOp = llvm::dyn_cast<llvm::BinaryOperator>(inst)) {
        llvm::Value* lhs = binOp->getOperand(0);
        llvm::Value* rhs = binOp->getOperand(1);

        switch (binOp->getOpcode()) {
            case llvm::Instruction::Add:
                // X + 0 => X
                if (llvm::isa<llvm::ConstantInt>(rhs) &&
                    llvm::cast<llvm::ConstantInt>(rhs)->isZero()) {
                    binOp->replaceAllUsesWith(lhs);
                    stats.instructionsSimplified++;
                    return true;
                }
                // X + X => X * 2
                if (lhs == rhs) {
                    llvm::IRBuilder<> builder(binOp);
                    auto* two = llvm::ConstantInt::get(binOp->getType(), 2);
                    auto* mul = builder.CreateMul(lhs, two);
                    binOp->replaceAllUsesWith(mul);
                    stats.instructionsSimplified++;
                    return true;
                }
                break;

            case llvm::Instruction::Sub:
                // X - 0 => X
                if (llvm::isa<llvm::ConstantInt>(rhs) &&
                    llvm::cast<llvm::ConstantInt>(rhs)->isZero()) {
                    binOp->replaceAllUsesWith(lhs);
                    stats.instructionsSimplified++;
                    return true;
                }
                // X - X => 0
                if (lhs == rhs) {
                    auto* zero = llvm::ConstantInt::get(binOp->getType(), 0);
                    binOp->replaceAllUsesWith(zero);
                    stats.instructionsSimplified++;
                    return true;
                }
                break;

            case llvm::Instruction::Mul:
                // X * 0 => 0
                if (llvm::isa<llvm::ConstantInt>(rhs) &&
                    llvm::cast<llvm::ConstantInt>(rhs)->isZero()) {
                    binOp->replaceAllUsesWith(rhs);
                    stats.instructionsSimplified++;
                    return true;
                }
                // X * 1 => X
                if (llvm::isa<llvm::ConstantInt>(rhs) &&
                    llvm::cast<llvm::ConstantInt>(rhs)->isOne()) {
                    binOp->replaceAllUsesWith(lhs);
                    stats.instructionsSimplified++;
                    return true;
                }
                // X * 2^n => X << n (ビットシフトに変換)
                if (auto* constInt = llvm::dyn_cast<llvm::ConstantInt>(rhs)) {
                    if (constInt->getValue().isPowerOf2()) {
                        unsigned shiftAmount = constInt->getValue().logBase2();
                        llvm::IRBuilder<> builder(binOp);
                        auto* shift = builder.CreateShl(lhs, shiftAmount);
                        binOp->replaceAllUsesWith(shift);
                        stats.strengthReductions++;
                        return true;
                    }
                }
                break;

            case llvm::Instruction::SDiv:
            case llvm::Instruction::UDiv:
                // X / 1 => X
                if (llvm::isa<llvm::ConstantInt>(rhs) &&
                    llvm::cast<llvm::ConstantInt>(rhs)->isOne()) {
                    binOp->replaceAllUsesWith(lhs);
                    stats.instructionsSimplified++;
                    return true;
                }
                // X / 2^n => X >> n (符号なし除算のみ)
                if (binOp->getOpcode() == llvm::Instruction::UDiv) {
                    if (auto* constInt = llvm::dyn_cast<llvm::ConstantInt>(rhs)) {
                        if (constInt->getValue().isPowerOf2()) {
                            unsigned shiftAmount = constInt->getValue().logBase2();
                            llvm::IRBuilder<> builder(binOp);
                            auto* shift = builder.CreateLShr(lhs, shiftAmount);
                            binOp->replaceAllUsesWith(shift);
                            stats.strengthReductions++;
                            return true;
                        }
                    }
                }
                break;

            case llvm::Instruction::And:
                // X & 0 => 0
                if (llvm::isa<llvm::ConstantInt>(rhs) &&
                    llvm::cast<llvm::ConstantInt>(rhs)->isZero()) {
                    binOp->replaceAllUsesWith(rhs);
                    stats.instructionsSimplified++;
                    return true;
                }
                // X & -1 => X
                if (llvm::isa<llvm::ConstantInt>(rhs) &&
                    llvm::cast<llvm::ConstantInt>(rhs)->isMinusOne()) {
                    binOp->replaceAllUsesWith(lhs);
                    stats.instructionsSimplified++;
                    return true;
                }
                // X & X => X
                if (lhs == rhs) {
                    binOp->replaceAllUsesWith(lhs);
                    stats.instructionsSimplified++;
                    return true;
                }
                break;

            case llvm::Instruction::Or:
                // X | 0 => X
                if (llvm::isa<llvm::ConstantInt>(rhs) &&
                    llvm::cast<llvm::ConstantInt>(rhs)->isZero()) {
                    binOp->replaceAllUsesWith(lhs);
                    stats.instructionsSimplified++;
                    return true;
                }
                // X | -1 => -1
                if (llvm::isa<llvm::ConstantInt>(rhs) &&
                    llvm::cast<llvm::ConstantInt>(rhs)->isMinusOne()) {
                    binOp->replaceAllUsesWith(rhs);
                    stats.instructionsSimplified++;
                    return true;
                }
                // X | X => X
                if (lhs == rhs) {
                    binOp->replaceAllUsesWith(lhs);
                    stats.instructionsSimplified++;
                    return true;
                }
                break;

            case llvm::Instruction::Xor:
                // X ^ 0 => X
                if (llvm::isa<llvm::ConstantInt>(rhs) &&
                    llvm::cast<llvm::ConstantInt>(rhs)->isZero()) {
                    binOp->replaceAllUsesWith(lhs);
                    stats.instructionsSimplified++;
                    return true;
                }
                // X ^ X => 0
                if (lhs == rhs) {
                    auto* zero = llvm::ConstantInt::get(binOp->getType(), 0);
                    binOp->replaceAllUsesWith(zero);
                    stats.instructionsSimplified++;
                    return true;
                }
                break;
        }
    }

    return false;
}

// 定数畳み込み
bool InstCombiner::foldConstants(llvm::Instruction* inst) {
    // 両オペランドが定数の場合、計算を実行
    if (auto* binOp = llvm::dyn_cast<llvm::BinaryOperator>(inst)) {
        auto* lhs = llvm::dyn_cast<llvm::ConstantInt>(binOp->getOperand(0));
        auto* rhs = llvm::dyn_cast<llvm::ConstantInt>(binOp->getOperand(1));

        if (lhs && rhs) {
            llvm::APInt result;
            bool valid = true;

            switch (binOp->getOpcode()) {
                case llvm::Instruction::Add:
                    result = lhs->getValue() + rhs->getValue();
                    break;
                case llvm::Instruction::Sub:
                    result = lhs->getValue() - rhs->getValue();
                    break;
                case llvm::Instruction::Mul:
                    result = lhs->getValue() * rhs->getValue();
                    break;
                case llvm::Instruction::SDiv:
                    if (!rhs->isZero()) {
                        result = lhs->getValue().sdiv(rhs->getValue());
                    } else {
                        valid = false;
                    }
                    break;
                case llvm::Instruction::UDiv:
                    if (!rhs->isZero()) {
                        result = lhs->getValue().udiv(rhs->getValue());
                    } else {
                        valid = false;
                    }
                    break;
                case llvm::Instruction::SRem:
                    if (!rhs->isZero()) {
                        result = lhs->getValue().srem(rhs->getValue());
                    } else {
                        valid = false;
                    }
                    break;
                case llvm::Instruction::URem:
                    if (!rhs->isZero()) {
                        result = lhs->getValue().urem(rhs->getValue());
                    } else {
                        valid = false;
                    }
                    break;
                case llvm::Instruction::And:
                    result = lhs->getValue() & rhs->getValue();
                    break;
                case llvm::Instruction::Or:
                    result = lhs->getValue() | rhs->getValue();
                    break;
                case llvm::Instruction::Xor:
                    result = lhs->getValue() ^ rhs->getValue();
                    break;
                case llvm::Instruction::Shl:
                    result = lhs->getValue().shl(rhs->getValue());
                    break;
                case llvm::Instruction::LShr:
                    result = lhs->getValue().lshr(rhs->getValue());
                    break;
                case llvm::Instruction::AShr:
                    result = lhs->getValue().ashr(rhs->getValue());
                    break;
                default:
                    valid = false;
            }

            if (valid) {
                auto* constResult = llvm::ConstantInt::get(binOp->getType(), result);
                binOp->replaceAllUsesWith(constResult);
                stats.constantsFolded++;
                return true;
            }
        }
    }

    // 定数比較の畳み込み
    if (auto* cmpInst = llvm::dyn_cast<llvm::ICmpInst>(inst)) {
        auto* lhs = llvm::dyn_cast<llvm::ConstantInt>(cmpInst->getOperand(0));
        auto* rhs = llvm::dyn_cast<llvm::ConstantInt>(cmpInst->getOperand(1));

        if (lhs && rhs) {
            bool result = false;
            switch (cmpInst->getPredicate()) {
                case llvm::ICmpInst::ICMP_EQ:
                    result = lhs->getValue() == rhs->getValue();
                    break;
                case llvm::ICmpInst::ICMP_NE:
                    result = lhs->getValue() != rhs->getValue();
                    break;
                case llvm::ICmpInst::ICMP_UGT:
                    result = lhs->getValue().ugt(rhs->getValue());
                    break;
                case llvm::ICmpInst::ICMP_UGE:
                    result = lhs->getValue().uge(rhs->getValue());
                    break;
                case llvm::ICmpInst::ICMP_ULT:
                    result = lhs->getValue().ult(rhs->getValue());
                    break;
                case llvm::ICmpInst::ICMP_ULE:
                    result = lhs->getValue().ule(rhs->getValue());
                    break;
                case llvm::ICmpInst::ICMP_SGT:
                    result = lhs->getValue().sgt(rhs->getValue());
                    break;
                case llvm::ICmpInst::ICMP_SGE:
                    result = lhs->getValue().sge(rhs->getValue());
                    break;
                case llvm::ICmpInst::ICMP_SLT:
                    result = lhs->getValue().slt(rhs->getValue());
                    break;
                case llvm::ICmpInst::ICMP_SLE:
                    result = lhs->getValue().sle(rhs->getValue());
                    break;
            }

            auto* constResult = llvm::ConstantInt::get(cmpInst->getType(), result ? 1 : 0);
            cmpInst->replaceAllUsesWith(constResult);
            stats.constantsFolded++;
            return true;
        }
    }

    return false;
}

// 比較の簡約化
bool InstCombiner::simplifyComparisons(llvm::Instruction* inst) {
    if (auto* cmpInst = llvm::dyn_cast<llvm::ICmpInst>(inst)) {
        auto* lhs = cmpInst->getOperand(0);
        auto* rhs = cmpInst->getOperand(1);

        // X == X => true
        // X != X => false
        if (lhs == rhs) {
            bool isTrue = (cmpInst->getPredicate() == llvm::ICmpInst::ICMP_EQ ||
                           cmpInst->getPredicate() == llvm::ICmpInst::ICMP_UGE ||
                           cmpInst->getPredicate() == llvm::ICmpInst::ICMP_ULE ||
                           cmpInst->getPredicate() == llvm::ICmpInst::ICMP_SGE ||
                           cmpInst->getPredicate() == llvm::ICmpInst::ICMP_SLE);

            auto* result = llvm::ConstantInt::get(cmpInst->getType(), isTrue ? 1 : 0);
            cmpInst->replaceAllUsesWith(result);
            stats.comparisonsSimplified++;
            return true;
        }

        // 定数との比較を正規化 (定数を右側に)
        if (llvm::isa<llvm::Constant>(lhs) && !llvm::isa<llvm::Constant>(rhs)) {
            cmpInst->swapOperands();
            stats.comparisonsSimplified++;
            return true;
        }
    }

    return false;
}

// Select命令の最適化
bool InstCombiner::optimizeSelect(llvm::Instruction* inst) {
    if (auto* selectInst = llvm::dyn_cast<llvm::SelectInst>(inst)) {
        auto* cond = selectInst->getCondition();
        auto* trueVal = selectInst->getTrueValue();
        auto* falseVal = selectInst->getFalseValue();

        // 定数条件の場合
        if (auto* constCond = llvm::dyn_cast<llvm::ConstantInt>(cond)) {
            auto* result = constCond->isOne() ? trueVal : falseVal;
            selectInst->replaceAllUsesWith(result);
            stats.selectsOptimized++;
            return true;
        }

        // select C, true, false => C
        if (llvm::isa<llvm::ConstantInt>(trueVal) && llvm::isa<llvm::ConstantInt>(falseVal)) {
            auto* trueConst = llvm::cast<llvm::ConstantInt>(trueVal);
            auto* falseConst = llvm::cast<llvm::ConstantInt>(falseVal);

            if (trueConst->isOne() && falseConst->isZero()) {
                selectInst->replaceAllUsesWith(cond);
                stats.selectsOptimized++;
                return true;
            }

            // select C, false, true => !C
            if (trueConst->isZero() && falseConst->isOne()) {
                llvm::IRBuilder<> builder(selectInst);
                auto* notCond = builder.CreateXor(cond, llvm::ConstantInt::get(cond->getType(), 1));
                selectInst->replaceAllUsesWith(notCond);
                stats.selectsOptimized++;
                return true;
            }
        }

        // select C, X, X => X
        if (trueVal == falseVal) {
            selectInst->replaceAllUsesWith(trueVal);
            stats.selectsOptimized++;
            return true;
        }
    }

    return false;
}

// キャスト最適化
bool InstCombiner::optimizeCasts(llvm::Instruction* inst) {
    if (auto* castInst = llvm::dyn_cast<llvm::CastInst>(inst)) {
        auto* src = castInst->getOperand(0);

        // 冗長なキャストの除去
        if (castInst->getSrcTy() == castInst->getDestTy()) {
            castInst->replaceAllUsesWith(src);
            stats.castsOptimized++;
            return true;
        }

        // 連続するキャストの結合
        if (auto* prevCast = llvm::dyn_cast<llvm::CastInst>(src)) {
            // cast(cast(X, T1), T2) => cast(X, T2)
            if (prevCast->getSrcTy() == castInst->getDestTy()) {
                // 元の型に戻る場合
                castInst->replaceAllUsesWith(prevCast->getOperand(0));
                stats.castsOptimized++;
                return true;
            }
        }
    }

    return false;
}

// ビットパターンの最適化
bool InstCombiner::optimizeBitPatterns(llvm::Instruction* inst) {
    // ビット演算の最適化パターン

    if (auto* binOp = llvm::dyn_cast<llvm::BinaryOperator>(inst)) {
        llvm::Value* lhs = binOp->getOperand(0);
        llvm::Value* rhs = binOp->getOperand(1);

        switch (binOp->getOpcode()) {
            case llvm::Instruction::And: {
                // X & X => X
                if (lhs == rhs) {
                    inst->replaceAllUsesWith(lhs);
                    stats.instructionsSimplified++;
                    return true;
                }

                // X & 0 => 0
                if (auto* constRhs = llvm::dyn_cast<llvm::ConstantInt>(rhs)) {
                    if (constRhs->isZero()) {
                        inst->replaceAllUsesWith(constRhs);
                        stats.instructionsSimplified++;
                        return true;
                    }
                    // X & -1 => X (全ビット1)
                    if (constRhs->isMinusOne()) {
                        inst->replaceAllUsesWith(lhs);
                        stats.instructionsSimplified++;
                        return true;
                    }
                }
                break;
            }

            case llvm::Instruction::Or: {
                // X | X => X
                if (lhs == rhs) {
                    inst->replaceAllUsesWith(lhs);
                    stats.instructionsSimplified++;
                    return true;
                }

                // X | 0 => X
                if (auto* constRhs = llvm::dyn_cast<llvm::ConstantInt>(rhs)) {
                    if (constRhs->isZero()) {
                        inst->replaceAllUsesWith(lhs);
                        stats.instructionsSimplified++;
                        return true;
                    }
                    // X | -1 => -1 (全ビット1)
                    if (constRhs->isMinusOne()) {
                        inst->replaceAllUsesWith(constRhs);
                        stats.instructionsSimplified++;
                        return true;
                    }
                }
                break;
            }

            case llvm::Instruction::Xor: {
                // X ^ X => 0
                if (lhs == rhs) {
                    auto* zero = llvm::ConstantInt::get(binOp->getType(), 0);
                    inst->replaceAllUsesWith(zero);
                    stats.instructionsSimplified++;
                    return true;
                }

                // X ^ 0 => X
                if (auto* constRhs = llvm::dyn_cast<llvm::ConstantInt>(rhs)) {
                    if (constRhs->isZero()) {
                        inst->replaceAllUsesWith(lhs);
                        stats.instructionsSimplified++;
                        return true;
                    }
                    // X ^ -1 => ~X (ビット反転)
                    if (constRhs->isMinusOne()) {
                        llvm::IRBuilder<> builder(inst);
                        auto* notOp = builder.CreateNot(lhs, "not");
                        inst->replaceAllUsesWith(notOp);
                        stats.instructionsSimplified++;
                        return true;
                    }
                }

                // (X ^ C1) ^ C2 => X ^ (C1 ^ C2)
                if (auto* lhsXor = llvm::dyn_cast<llvm::BinaryOperator>(lhs)) {
                    if (lhsXor->getOpcode() == llvm::Instruction::Xor) {
                        if (auto* c1 = llvm::dyn_cast<llvm::ConstantInt>(lhsXor->getOperand(1))) {
                            if (auto* c2 = llvm::dyn_cast<llvm::ConstantInt>(rhs)) {
                                llvm::IRBuilder<> builder(inst);
                                auto* combinedConst = builder.CreateXor(c1, c2);
                                auto* result =
                                    builder.CreateXor(lhsXor->getOperand(0), combinedConst);
                                inst->replaceAllUsesWith(result);
                                stats.instructionsSimplified++;
                                return true;
                            }
                        }
                    }
                }
                break;
            }

            case llvm::Instruction::Shl:
            case llvm::Instruction::LShr:
            case llvm::Instruction::AShr: {
                // シフト量が0の場合
                if (auto* constRhs = llvm::dyn_cast<llvm::ConstantInt>(rhs)) {
                    if (constRhs->isZero()) {
                        inst->replaceAllUsesWith(lhs);
                        stats.instructionsSimplified++;
                        return true;
                    }

                    // シフト量がビット幅以上の場合は未定義動作なのでスキップ
                    unsigned bitWidth = binOp->getType()->getIntegerBitWidth();
                    if (constRhs->getValue().uge(bitWidth)) {
                        return false;
                    }
                }
                break;
            }
        }
    }

    return false;
}

// メモリ操作の最適化
bool InstCombiner::optimizeMemory(llvm::Instruction* inst) {
    // Load/Store命令の最適化

    // 1. Load-after-Store フォワーディング
    if (auto* load = llvm::dyn_cast<llvm::LoadInst>(inst)) {
        llvm::Value* loadPtr = load->getPointerOperand();

        // 直前の命令を調べる
        auto* prevInst = inst->getPrevNode();
        if (!prevInst)
            return false;

        // 同じアドレスへのStoreがあれば、その値を直接使う
        if (auto* store = llvm::dyn_cast<llvm::StoreInst>(prevInst)) {
            if (store->getPointerOperand() == loadPtr) {
                // エイリアシングやメモリの順序制約をチェック
                if (!load->isVolatile() && !store->isVolatile()) {
                    load->replaceAllUsesWith(store->getValueOperand());
                    stats.instructionsSimplified++;
                    return true;
                }
            }
        }

        // 2. 冗長なLoadの除去
        // 同じアドレスからの連続したLoadは1つにまとめる
        for (auto* user : loadPtr->users()) {
            if (user != inst && llvm::isa<llvm::LoadInst>(user)) {
                auto* otherLoad = llvm::cast<llvm::LoadInst>(user);

                // 同じブロック内で、このLoadより前にある場合
                if (otherLoad->getParent() == load->getParent()) {
                    // dominance check (簡易版: 同じブロック内で前にある)
                    bool dominates = false;
                    for (auto& I : *load->getParent()) {
                        if (&I == otherLoad) {
                            dominates = true;
                            break;
                        }
                        if (&I == load) {
                            break;
                        }
                    }

                    if (dominates && !otherLoad->isVolatile() && !load->isVolatile()) {
                        // 間に書き込みがないことを確認
                        bool hasIntervening = false;
                        bool foundFirst = false;

                        for (auto& I : *load->getParent()) {
                            if (&I == otherLoad) {
                                foundFirst = true;
                                continue;
                            }
                            if (&I == load) {
                                break;
                            }
                            if (foundFirst && I.mayWriteToMemory()) {
                                hasIntervening = true;
                                break;
                            }
                        }

                        if (!hasIntervening) {
                            load->replaceAllUsesWith(otherLoad);
                            stats.instructionsSimplified++;
                            return true;
                        }
                    }
                }
            }
        }
    }

    // 3. デッドストアの除去
    if (auto* store = llvm::dyn_cast<llvm::StoreInst>(inst)) {
        llvm::Value* storePtr = store->getPointerOperand();

        // 次の命令を調べる
        auto* nextInst = inst->getNextNode();
        if (nextInst) {
            // 同じアドレスへの別のStoreがある場合、最初のStoreは不要
            if (auto* nextStore = llvm::dyn_cast<llvm::StoreInst>(nextInst)) {
                if (nextStore->getPointerOperand() == storePtr) {
                    if (!store->isVolatile() && !nextStore->isVolatile()) {
                        // 最初のストアを削除マーク（実際の削除は呼び出し元で行う）
                        // 削除せずに、無効化だけ行う
                        auto* undef = llvm::UndefValue::get(store->getType());
                        store->replaceAllUsesWith(undef);
                        stats.instructionsSimplified++;
                        return true;
                    }
                }
            }
        }
    }

    // 4. Memcpy/Memsetの最適化
    if (auto* call = llvm::dyn_cast<llvm::CallInst>(inst)) {
        llvm::Function* callee = call->getCalledFunction();
        if (!callee)
            return false;

        llvm::StringRef name = callee->getName();

        // 小さなmemcpyをLoad/Storeに変換
        if (name == "memcpy" || name == "llvm.memcpy.p0i8.p0i8.i64") {
            if (call->getNumOperands() >= 3) {
                if (auto* sizeConst = llvm::dyn_cast<llvm::ConstantInt>(call->getOperand(2))) {
                    uint64_t size = sizeConst->getZExtValue();

                    // 小さなコピー（8バイト以下）は直接Load/Storeに変換
                    if (size > 0 && size <= 8 && isPowerOf2_64(size)) {
                        llvm::IRBuilder<> builder(inst);

                        // 適切な型を選択
                        llvm::Type* ty = nullptr;
                        switch (size) {
                            case 1:
                                ty = builder.getInt8Ty();
                                break;
                            case 2:
                                ty = builder.getInt16Ty();
                                break;
                            case 4:
                                ty = builder.getInt32Ty();
                                break;
                            case 8:
                                ty = builder.getInt64Ty();
                                break;
                        }

                        if (ty) {
                            // LLVM 14+: opaque pointersを使用、BitCastは不要
                            auto* srcPtr = call->getOperand(1);
                            auto* dstPtr = call->getOperand(0);
                            auto* val = builder.CreateLoad(ty, srcPtr);
                            builder.CreateStore(val, dstPtr);
                            // 削除せずに無効化
                            call->replaceAllUsesWith(llvm::UndefValue::get(call->getType()));
                            stats.instructionsSimplified++;
                            return true;
                        }
                    }
                }
            }
        }

        // 小さなmemsetを直接Storeに変換
        if (name == "memset" || name == "llvm.memset.p0i8.i64") {
            if (call->getNumOperands() >= 3) {
                if (auto* sizeConst = llvm::dyn_cast<llvm::ConstantInt>(call->getOperand(2))) {
                    uint64_t size = sizeConst->getZExtValue();

                    // 小さなセット（8バイト以下）
                    if (size > 0 && size <= 8 && isPowerOf2_64(size)) {
                        if (auto* valConst =
                                llvm::dyn_cast<llvm::ConstantInt>(call->getOperand(1))) {
                            uint8_t byteVal = valConst->getZExtValue() & 0xFF;
                            llvm::IRBuilder<> builder(inst);

                            // 値を拡張して適切なサイズにする
                            uint64_t fullVal = 0;
                            for (uint64_t i = 0; i < size; ++i) {
                                fullVal |= (uint64_t)byteVal << (i * 8);
                            }

                            llvm::Type* ty = nullptr;
                            switch (size) {
                                case 1:
                                    ty = builder.getInt8Ty();
                                    break;
                                case 2:
                                    ty = builder.getInt16Ty();
                                    break;
                                case 4:
                                    ty = builder.getInt32Ty();
                                    break;
                                case 8:
                                    ty = builder.getInt64Ty();
                                    break;
                            }

                            if (ty) {
                                // LLVM 14+: opaque pointersを使用、BitCastは不要
                                auto* dstPtr = call->getOperand(0);
                                auto* val = llvm::ConstantInt::get(ty, fullVal);
                                builder.CreateStore(val, dstPtr);
                                // 削除せずに無効化
                                call->replaceAllUsesWith(llvm::UndefValue::get(call->getType()));
                                stats.instructionsSimplified++;
                                return true;
                            }
                        }
                    }
                }
            }
        }
    }

    return false;
}

// ヘルパー関数: 2のべき乗判定
bool InstCombiner::isPowerOf2_64(uint64_t value) {
    return value && !(value & (value - 1));
}

// PHIノードの最適化
bool InstCombiner::optimizePhi(llvm::Instruction* inst) {
    if (auto* phi = llvm::dyn_cast<llvm::PHINode>(inst)) {
        // すべての入力値が同じ場合
        llvm::Value* commonValue = phi->getIncomingValue(0);
        bool allSame = true;

        for (unsigned i = 1; i < phi->getNumIncomingValues(); i++) {
            if (phi->getIncomingValue(i) != commonValue) {
                allSame = false;
                break;
            }
        }

        if (allSame) {
            phi->replaceAllUsesWith(commonValue);
            stats.phisOptimized++;
            return true;
        }

        // 単一の入力値の場合
        if (phi->getNumIncomingValues() == 1) {
            phi->replaceAllUsesWith(phi->getIncomingValue(0));
            stats.phisOptimized++;
            return true;
        }
    }

    return false;
}

// 統計情報を出力
void InstCombiner::printStatistics() const {
    llvm::errs() << "\n[InstCombiner] Optimization Statistics:\n"
                 << "  Instructions combined: " << stats.instructionsCombined << "\n"
                 << "  Instructions simplified: " << stats.instructionsSimplified << "\n"
                 << "  Constants folded: " << stats.constantsFolded << "\n"
                 << "  Strength reductions: " << stats.strengthReductions << "\n"
                 << "  Comparisons simplified: " << stats.comparisonsSimplified << "\n"
                 << "  Selects optimized: " << stats.selectsOptimized << "\n"
                 << "  Casts optimized: " << stats.castsOptimized << "\n"
                 << "  PHIs optimized: " << stats.phisOptimized << "\n";
}

}  // namespace cm::codegen::llvm_backend::optimizations
#pragma GCC diagnostic pop
