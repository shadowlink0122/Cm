/// @file peephole_optimizer.cpp
/// @brief Peephole最適化の実装 - 小規模な命令パターンの最適化

#include "peephole_optimizer.hpp"

#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/Debug.h>

namespace cm::codegen::llvm_backend::optimizations {

bool PeepholeOptimizer::optimizeFunction(llvm::Function& func) {
    bool changed = false;
    unsigned iteration = 0;

    // 反復的に最適化を適用（固定点に到達するまで）
    while (iteration < config.maxIterations) {
        bool iterationChanged = false;

        for (auto& bb : func) {
            for (auto it = bb.begin(); it != bb.end();) {
                auto* inst = &*it++;

                // 削除された命令はスキップ
                if (!inst->getParent()) {
                    continue;
                }

                bool instChanged = false;

                // 各最適化パターンを試行
                if (config.enableIdentityElimination) {
                    instChanged |= eliminateIdentity(inst);
                }

                if (!instChanged && config.enableStrengthReduction) {
                    instChanged |= performStrengthReduction(inst);
                }

                if (!instChanged && config.enableConstantFolding) {
                    instChanged |= foldConstants(inst);
                }

                // 追加の最適化パターン
                if (!instChanged) {
                    instChanged |= eliminateRedundantCast(inst);
                    instChanged |= simplifyComparison(inst);
                    instChanged |= optimizeMemoryAccess(inst);
                }

                if (instChanged) {
                    iterationChanged = true;
                }
            }
        }

        if (!iterationChanged) {
            break;  // 変更がなければ終了
        }

        changed = true;
        iteration++;
    }

    return changed;
}

// 恒等元の削除
bool PeepholeOptimizer::eliminateIdentity(llvm::Instruction* inst) {
    if (auto* binOp = llvm::dyn_cast<llvm::BinaryOperator>(inst)) {
        llvm::Value* lhs = binOp->getOperand(0);
        llvm::Value* rhs = binOp->getOperand(1);

        switch (binOp->getOpcode()) {
            case llvm::Instruction::Add:
            case llvm::Instruction::FAdd:
                // X + 0 => X
                if (isZero(rhs)) {
                    inst->replaceAllUsesWith(lhs);
                    inst->eraseFromParent();
                    stats.identitiesEliminated++;
                    return true;
                }
                if (isZero(lhs)) {
                    inst->replaceAllUsesWith(rhs);
                    inst->eraseFromParent();
                    stats.identitiesEliminated++;
                    return true;
                }
                break;

            case llvm::Instruction::Sub:
            case llvm::Instruction::FSub:
                // X - 0 => X
                if (isZero(rhs)) {
                    inst->replaceAllUsesWith(lhs);
                    inst->eraseFromParent();
                    stats.identitiesEliminated++;
                    return true;
                }
                // X - X => 0
                if (lhs == rhs) {
                    auto* zero = llvm::Constant::getNullValue(binOp->getType());
                    inst->replaceAllUsesWith(zero);
                    inst->eraseFromParent();
                    stats.identitiesEliminated++;
                    return true;
                }
                break;

            case llvm::Instruction::Mul:
            case llvm::Instruction::FMul:
                // X * 1 => X
                if (isOne(rhs)) {
                    inst->replaceAllUsesWith(lhs);
                    inst->eraseFromParent();
                    stats.identitiesEliminated++;
                    return true;
                }
                if (isOne(lhs)) {
                    inst->replaceAllUsesWith(rhs);
                    inst->eraseFromParent();
                    stats.identitiesEliminated++;
                    return true;
                }
                // X * 0 => 0
                if (isZero(rhs) || isZero(lhs)) {
                    auto* zero = llvm::Constant::getNullValue(binOp->getType());
                    inst->replaceAllUsesWith(zero);
                    inst->eraseFromParent();
                    stats.constantFolds++;
                    return true;
                }
                break;

            case llvm::Instruction::UDiv:
            case llvm::Instruction::SDiv:
            case llvm::Instruction::FDiv:
                // X / 1 => X
                if (isOne(rhs)) {
                    inst->replaceAllUsesWith(lhs);
                    inst->eraseFromParent();
                    stats.identitiesEliminated++;
                    return true;
                }
                // X / X => 1 (X != 0)
                if (lhs == rhs && !isZero(lhs)) {
                    auto* one = llvm::ConstantInt::get(binOp->getType(), 1);
                    inst->replaceAllUsesWith(one);
                    inst->eraseFromParent();
                    stats.identitiesEliminated++;
                    return true;
                }
                break;

            case llvm::Instruction::And:
                // X & X => X
                if (lhs == rhs) {
                    inst->replaceAllUsesWith(lhs);
                    inst->eraseFromParent();
                    stats.identitiesEliminated++;
                    return true;
                }
                // X & 0 => 0
                if (isZero(rhs) || isZero(lhs)) {
                    auto* zero = llvm::Constant::getNullValue(binOp->getType());
                    inst->replaceAllUsesWith(zero);
                    inst->eraseFromParent();
                    stats.constantFolds++;
                    return true;
                }
                // X & -1 => X
                if (isAllOnes(rhs)) {
                    inst->replaceAllUsesWith(lhs);
                    inst->eraseFromParent();
                    stats.identitiesEliminated++;
                    return true;
                }
                if (isAllOnes(lhs)) {
                    inst->replaceAllUsesWith(rhs);
                    inst->eraseFromParent();
                    stats.identitiesEliminated++;
                    return true;
                }
                break;

            case llvm::Instruction::Or:
                // X | X => X
                if (lhs == rhs) {
                    inst->replaceAllUsesWith(lhs);
                    inst->eraseFromParent();
                    stats.identitiesEliminated++;
                    return true;
                }
                // X | 0 => X
                if (isZero(rhs)) {
                    inst->replaceAllUsesWith(lhs);
                    inst->eraseFromParent();
                    stats.identitiesEliminated++;
                    return true;
                }
                if (isZero(lhs)) {
                    inst->replaceAllUsesWith(rhs);
                    inst->eraseFromParent();
                    stats.identitiesEliminated++;
                    return true;
                }
                // X | -1 => -1
                if (isAllOnes(rhs) || isAllOnes(lhs)) {
                    auto* allOnes = llvm::Constant::getAllOnesValue(binOp->getType());
                    inst->replaceAllUsesWith(allOnes);
                    inst->eraseFromParent();
                    stats.constantFolds++;
                    return true;
                }
                break;

            case llvm::Instruction::Xor:
                // X ^ X => 0
                if (lhs == rhs) {
                    auto* zero = llvm::Constant::getNullValue(binOp->getType());
                    inst->replaceAllUsesWith(zero);
                    inst->eraseFromParent();
                    stats.identitiesEliminated++;
                    return true;
                }
                // X ^ 0 => X
                if (isZero(rhs)) {
                    inst->replaceAllUsesWith(lhs);
                    inst->eraseFromParent();
                    stats.identitiesEliminated++;
                    return true;
                }
                if (isZero(lhs)) {
                    inst->replaceAllUsesWith(rhs);
                    inst->eraseFromParent();
                    stats.identitiesEliminated++;
                    return true;
                }
                break;

            case llvm::Instruction::Shl:
            case llvm::Instruction::LShr:
            case llvm::Instruction::AShr:
                // X << 0, X >> 0 => X
                if (isZero(rhs)) {
                    inst->replaceAllUsesWith(lhs);
                    inst->eraseFromParent();
                    stats.identitiesEliminated++;
                    return true;
                }
                break;
        }
    }

    return false;
}

// 強度削減
bool PeepholeOptimizer::performStrengthReduction(llvm::Instruction* inst) {
    if (auto* binOp = llvm::dyn_cast<llvm::BinaryOperator>(inst)) {
        llvm::Value* lhs = binOp->getOperand(0);
        llvm::Value* rhs = binOp->getOperand(1);

        switch (binOp->getOpcode()) {
            case llvm::Instruction::Mul:
                // X * 2^n => X << n
                if (auto* constInt = llvm::dyn_cast<llvm::ConstantInt>(rhs)) {
                    if (constInt->getValue().isPowerOf2()) {
                        unsigned shiftAmount = constInt->getValue().logBase2();
                        llvm::IRBuilder<> builder(inst);
                        auto* shift = builder.CreateShl(lhs, shiftAmount);
                        inst->replaceAllUsesWith(shift);
                        inst->eraseFromParent();
                        stats.strengthReductions++;
                        return true;
                    }
                }
                // X * -1 => -X
                if (isMinusOne(rhs)) {
                    llvm::IRBuilder<> builder(inst);
                    auto* neg = builder.CreateNeg(lhs);
                    inst->replaceAllUsesWith(neg);
                    inst->eraseFromParent();
                    stats.strengthReductions++;
                    return true;
                }
                break;

            case llvm::Instruction::UDiv:
                // X / 2^n => X >> n (符号なし)
                if (auto* constInt = llvm::dyn_cast<llvm::ConstantInt>(rhs)) {
                    if (constInt->getValue().isPowerOf2()) {
                        unsigned shiftAmount = constInt->getValue().logBase2();
                        llvm::IRBuilder<> builder(inst);
                        auto* shift = builder.CreateLShr(lhs, shiftAmount);
                        inst->replaceAllUsesWith(shift);
                        inst->eraseFromParent();
                        stats.strengthReductions++;
                        return true;
                    }
                }
                break;

            case llvm::Instruction::SDiv:
                // X / 2^n => X >> n (符号付き、特別な処理が必要)
                if (auto* constInt = llvm::dyn_cast<llvm::ConstantInt>(rhs)) {
                    if (constInt->getValue().isPowerOf2() && !constInt->getValue().isNegative()) {
                        // 符号付き除算は負数の場合に注意が必要
                        // ここでは単純なケースのみ処理
                        unsigned shiftAmount = constInt->getValue().logBase2();
                        llvm::IRBuilder<> builder(inst);
                        auto* shift = builder.CreateAShr(lhs, shiftAmount);
                        inst->replaceAllUsesWith(shift);
                        inst->eraseFromParent();
                        stats.strengthReductions++;
                        return true;
                    }
                }
                break;

            case llvm::Instruction::URem:
                // X % 2^n => X & (2^n - 1)
                if (auto* constInt = llvm::dyn_cast<llvm::ConstantInt>(rhs)) {
                    if (constInt->getValue().isPowerOf2()) {
                        llvm::IRBuilder<> builder(inst);
                        auto* mask =
                            builder.CreateSub(rhs, llvm::ConstantInt::get(constInt->getType(), 1));
                        auto* andInst = builder.CreateAnd(lhs, mask);
                        inst->replaceAllUsesWith(andInst);
                        inst->eraseFromParent();
                        stats.strengthReductions++;
                        return true;
                    }
                }
                break;
        }
    }

    return false;
}

// 定数畳み込み
bool PeepholeOptimizer::foldConstants(llvm::Instruction* inst) {
    // 二項演算の定数畳み込み
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
                case llvm::Instruction::UDiv:
                    if (!rhs->isZero()) {
                        result = lhs->getValue().udiv(rhs->getValue());
                    } else {
                        valid = false;
                    }
                    break;
                case llvm::Instruction::SDiv:
                    if (!rhs->isZero()) {
                        result = lhs->getValue().sdiv(rhs->getValue());
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
                case llvm::Instruction::SRem:
                    if (!rhs->isZero()) {
                        result = lhs->getValue().srem(rhs->getValue());
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
                inst->replaceAllUsesWith(constResult);
                inst->eraseFromParent();
                stats.constantFolds++;
                return true;
            }
        }
    }

    // 比較命令の定数畳み込み
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
            inst->replaceAllUsesWith(constResult);
            inst->eraseFromParent();
            stats.constantFolds++;
            return true;
        }
    }

    return false;
}

// 冗長なキャストの削除
bool PeepholeOptimizer::eliminateRedundantCast(llvm::Instruction* inst) {
    if (auto* castInst = llvm::dyn_cast<llvm::CastInst>(inst)) {
        auto* src = castInst->getOperand(0);

        // 同じ型へのキャスト
        if (castInst->getSrcTy() == castInst->getDestTy()) {
            inst->replaceAllUsesWith(src);
            inst->eraseFromParent();
            stats.identitiesEliminated++;
            return true;
        }

        // 連続するキャストの最適化
        if (auto* prevCast = llvm::dyn_cast<llvm::CastInst>(src)) {
            // cast(cast(X, T1), T2)の形

            // 元の型に戻る場合: cast(cast(X, T1), T0) => X
            if (prevCast->getSrcTy() == castInst->getDestTy()) {
                inst->replaceAllUsesWith(prevCast->getOperand(0));
                inst->eraseFromParent();
                stats.identitiesEliminated++;
                return true;
            }

            // 中間キャストを除去: cast(cast(X, T1), T2) => cast(X, T2)
            // ただし、情報が失われないことを確認
            if (canEliminateIntermediateCast(prevCast, castInst)) {
                llvm::IRBuilder<> builder(inst);
                auto* newCast = builder.CreateCast(castInst->getOpcode(), prevCast->getOperand(0),
                                                   castInst->getDestTy());
                inst->replaceAllUsesWith(newCast);
                inst->eraseFromParent();
                stats.identitiesEliminated++;
                return true;
            }
        }
    }

    return false;
}

// 比較の簡約化
bool PeepholeOptimizer::simplifyComparison(llvm::Instruction* inst) {
    if (auto* cmpInst = llvm::dyn_cast<llvm::ICmpInst>(inst)) {
        auto* lhs = cmpInst->getOperand(0);
        auto* rhs = cmpInst->getOperand(1);

        // X == X, X >= X, X <= X => true
        // X != X, X > X, X < X => false
        if (lhs == rhs) {
            bool isTrue = false;
            switch (cmpInst->getPredicate()) {
                case llvm::ICmpInst::ICMP_EQ:
                case llvm::ICmpInst::ICMP_UGE:
                case llvm::ICmpInst::ICMP_ULE:
                case llvm::ICmpInst::ICMP_SGE:
                case llvm::ICmpInst::ICMP_SLE:
                    isTrue = true;
                    break;
                case llvm::ICmpInst::ICMP_NE:
                case llvm::ICmpInst::ICMP_UGT:
                case llvm::ICmpInst::ICMP_ULT:
                case llvm::ICmpInst::ICMP_SGT:
                case llvm::ICmpInst::ICMP_SLT:
                    isTrue = false;
                    break;
            }

            auto* result = llvm::ConstantInt::get(cmpInst->getType(), isTrue ? 1 : 0);
            inst->replaceAllUsesWith(result);
            inst->eraseFromParent();
            stats.constantFolds++;
            return true;
        }

        // 定数を右側に正規化
        if (llvm::isa<llvm::Constant>(lhs) && !llvm::isa<llvm::Constant>(rhs)) {
            cmpInst->swapOperands();
            return true;
        }
    }

    return false;
}

// メモリアクセスの最適化
bool PeepholeOptimizer::optimizeMemoryAccess(llvm::Instruction* inst) {
    // Load-Store フォワーディング
    if (auto* load = llvm::dyn_cast<llvm::LoadInst>(inst)) {
        auto* ptr = load->getPointerOperand();

        // 同じアドレスへの直前のStoreを探す
        auto* prevInst = inst->getPrevNode();
        while (prevInst) {
            if (auto* store = llvm::dyn_cast<llvm::StoreInst>(prevInst)) {
                if (store->getPointerOperand() == ptr) {
                    // load(store(V, P)) => V
                    inst->replaceAllUsesWith(store->getValueOperand());
                    inst->eraseFromParent();
                    stats.identitiesEliminated++;
                    return true;
                }
            }

            // メモリバリアや関数呼び出しがあれば中止
            if (prevInst->mayWriteToMemory() || prevInst->mayReadFromMemory()) {
                if (!llvm::isa<llvm::StoreInst>(prevInst) && !llvm::isa<llvm::LoadInst>(prevInst)) {
                    break;
                }
            }

            prevInst = prevInst->getPrevNode();
        }
    }

    // 冗長なStore-Loadペアの削除
    if (auto* store = llvm::dyn_cast<llvm::StoreInst>(inst)) {
        auto* nextInst = inst->getNextNode();
        if (auto* load = llvm::dyn_cast<llvm::LoadInst>(nextInst)) {
            if (load->getPointerOperand() == store->getPointerOperand()) {
                // store(V, P); load(P) => store(V, P); V
                load->replaceAllUsesWith(store->getValueOperand());
                load->eraseFromParent();
                stats.identitiesEliminated++;
                return true;
            }
        }
    }

    return false;
}

// ヘルパー関数
bool PeepholeOptimizer::isZero(llvm::Value* v) {
    if (auto* c = llvm::dyn_cast<llvm::ConstantInt>(v)) {
        return c->isZero();
    }
    if (auto* c = llvm::dyn_cast<llvm::ConstantFP>(v)) {
        return c->isZero();
    }
    return false;
}

bool PeepholeOptimizer::isOne(llvm::Value* v) {
    if (auto* c = llvm::dyn_cast<llvm::ConstantInt>(v)) {
        return c->isOne();
    }
    if (auto* c = llvm::dyn_cast<llvm::ConstantFP>(v)) {
        return c->isExactlyValue(1.0);
    }
    return false;
}

bool PeepholeOptimizer::isMinusOne(llvm::Value* v) {
    if (auto* c = llvm::dyn_cast<llvm::ConstantInt>(v)) {
        return c->isMinusOne();
    }
    if (auto* c = llvm::dyn_cast<llvm::ConstantFP>(v)) {
        return c->isExactlyValue(-1.0);
    }
    return false;
}

bool PeepholeOptimizer::isAllOnes(llvm::Value* v) {
    if (auto* c = llvm::dyn_cast<llvm::ConstantInt>(v)) {
        return c->isMinusOne();  // -1 は全ビット1
    }
    return false;
}

bool PeepholeOptimizer::canEliminateIntermediateCast(llvm::CastInst* first,
                                                     llvm::CastInst* second) {
    // 簡単な実装: 両方が同じ種類のキャストで、情報が失われない場合のみ
    if (first->getOpcode() == second->getOpcode()) {
        // ZExtやSExtの連続は安全
        if (llvm::isa<llvm::ZExtInst>(first) || llvm::isa<llvm::SExtInst>(first)) {
            return true;
        }
        // Truncの連続も安全（より小さい型へ）
        if (llvm::isa<llvm::TruncInst>(first)) {
            return true;
        }
    }

    // より詳細な分析が必要な場合はfalseを返す
    return false;
}

}  // namespace cm::codegen::llvm_backend::optimizations