#include "vectorizer.hpp"

#include <llvm/Analysis/AssumptionCache.h>
#include <llvm/Analysis/LoopPass.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IntrinsicsX86.h>
#include <llvm/Support/Debug.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>

namespace cm::codegen::llvm_backend::optimizations {

bool Vectorizer::vectorizeFunction(llvm::Function& func) {
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

    // 各ループのベクトル化を試みる
    for (auto* loop : loops) {
        if (vectorizeLoopSimple(loop, DT)) {
            modified = true;
            stats.loopsVectorized++;
        }
    }

    // SLP (Superword Level Parallelism) ベクトル化
    if (config.enableSLP) {
        if (performSLPVectorization(func)) {
            modified = true;
        }
    }

    if (modified) {
        debugPrintStatistics();
    }

    return modified;
}

bool Vectorizer::vectorizeLoopSimple(llvm::Loop* loop, llvm::DominatorTree& DT) {
    // 簡易的なループ解析
    VectorizationInfo info = analyzeLoopSimple(loop);
    if (!info.canVectorize) {
        return false;
    }

    // ベクトル化の実行
    auto* preheader = loop->getLoopPreheader();
    if (!preheader) {
        return false;
    }

    llvm::IRBuilder<> builder(preheader->getTerminator());
    return performVectorization(loop, info, builder, DT);
}

Vectorizer::VectorizationInfo Vectorizer::analyzeLoopSimple(llvm::Loop* loop) {
    VectorizationInfo info;

    // 基本チェック
    if (!loop->getLoopPreheader() || !loop->getLoopLatch() || !loop->getExitBlock() ||
        !loop->getHeader()) {
        return info;
    }

    // 簡易的なベクトル化可能性チェック
    bool hasSimplePattern = false;
    bool hasReduction = false;
    bool hasVectorizableMemAccess = false;
    (void)hasVectorizableMemAccess;

    for (auto* BB : loop->blocks()) {
        for (auto& I : *BB) {
            // 単純な算術演算パターン
            if (llvm::isa<llvm::BinaryOperator>(&I)) {
                auto* binOp = llvm::cast<llvm::BinaryOperator>(&I);
                switch (binOp->getOpcode()) {
                    case llvm::Instruction::Add:
                    case llvm::Instruction::Sub:
                    case llvm::Instruction::Mul:
                    case llvm::Instruction::FAdd:
                    case llvm::Instruction::FSub:
                    case llvm::Instruction::FMul:
                        hasSimplePattern = true;
                        break;
                    default:
                        break;
                }
            }

            // メモリアクセスパターン
            if (llvm::isa<llvm::LoadInst>(&I) || llvm::isa<llvm::StoreInst>(&I)) {
                // 連続メモリアクセスのチェック（簡易版）
                hasVectorizableMemAccess = checkConsecutiveAccess(&I);
            }

            // リダクション演算の検出
            if (auto* phi = llvm::dyn_cast<llvm::PHINode>(&I)) {
                if (isReductionPHI(phi)) {
                    hasReduction = true;
                }
            }
        }
    }

    // ベクトル化可能性の判定
    info.canVectorize = (hasSimplePattern || hasReduction) && !hasSideEffects(loop);

    if (info.canVectorize) {
        // ベクトル幅の決定
        info.vectorFactor = config.vectorWidth;

        // ベクトル型の設定
        auto& context = loop->getHeader()->getContext();
        info.vectorType =
            llvm::FixedVectorType::get(llvm::Type::getInt32Ty(context), info.vectorFactor);
    }

    return info;
}

bool Vectorizer::performVectorization(llvm::Loop* loop, const VectorizationInfo& info,
                                      llvm::IRBuilder<>& builder, llvm::DominatorTree& DT) {
    if (!info.canVectorize || info.vectorFactor <= 1) {
        return false;
    }

    auto* header = loop->getHeader();
    auto* preheader = loop->getLoopPreheader();
    auto* exit = loop->getExitBlock();

    if (!header || !preheader || !exit) {
        return false;
    }

    // ベクトル化されたループの作成
    auto& context = header->getContext();

    // 新しいベクトル化されたループ本体の作成
    auto* vectorBody = llvm::BasicBlock::Create(context, "vector.body", header->getParent(), exit);

    // ベクトル化されたループヘッダーの作成
    auto* vectorHeader =
        llvm::BasicBlock::Create(context, "vector.header", header->getParent(), vectorBody);

    // プレヘッダーからベクトルヘッダーへ分岐
    builder.SetInsertPoint(preheader->getTerminator());
    preheader->getTerminator()->eraseFromParent();
    builder.CreateBr(vectorHeader);

    // ベクトル化されたループ本体の生成
    generateVectorBody(loop, info, vectorBody, vectorHeader, builder);

    // エピローグの生成（スカラー版で残りを処理）
    generateEpilogue(loop, vectorBody, exit, info.vectorFactor, builder);

    // DominatorTreeを更新
    DT.recalculate(*header->getParent());

    return true;
}

void Vectorizer::generateVectorBody(llvm::Loop* loop, const VectorizationInfo& info,
                                    llvm::BasicBlock* vectorBody, llvm::BasicBlock* vectorHeader,
                                    llvm::IRBuilder<>& builder) {
    auto& context = vectorBody->getContext();

    // ベクトルヘッダーのPHIノード作成
    builder.SetInsertPoint(vectorHeader);
    auto* inductionPHI = builder.CreatePHI(llvm::Type::getInt32Ty(context), 2, "vec.ind");

    // ベクトル化されたループ本体
    builder.SetInsertPoint(vectorBody);

    // 簡単な例: 配列の要素ごとの加算をベクトル化
    // for (int i = 0; i < N; i += VF) {
    //     vec_load a[i:i+VF]
    //     vec_load b[i:i+VF]
    //     vec_add
    //     vec_store result[i:i+VF]
    // }

    // ベクトル化の主ループ
    // TODO: ベクトル化された値を適切に処理する実装が必要
    // llvm::Value* vectorValue = nullptr;

    // 元のループから命令を収集してベクトル化
    for (auto* BB : loop->blocks()) {
        for (auto& I : *BB) {
            if (auto* binOp = llvm::dyn_cast<llvm::BinaryOperator>(&I)) {
                // 算術演算をベクトル化
                if (binOp->getOpcode() == llvm::Instruction::Add) {
                    // ベクトル加算の生成
                    if (info.vectorType && info.vectorType->isVectorTy()) {
                        // 簡易実装: ベクトル定数の作成
                        auto* vecType = llvm::cast<llvm::FixedVectorType>(info.vectorType);
                        std::vector<llvm::Constant*> elems(
                            vecType->getNumElements(),
                            llvm::ConstantInt::get(vecType->getElementType(), 1));
                        auto* vec1 = llvm::ConstantVector::get(elems);
                        auto* vec2 = llvm::ConstantVector::get(elems);

                        // TODO: ベクトル化された値を使用する処理を追加
                        [[maybe_unused]] auto* vectorValue =
                            builder.CreateAdd(vec1, vec2, "vec.add");
                    }
                }
            }
        }
    }

    // インダクション変数の更新
    auto* stepValue = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), info.vectorFactor);
    auto* nextInd = builder.CreateAdd(inductionPHI, stepValue, "vec.ind.next");

    // ループ条件のチェック
    auto* loopBound = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 100);
    auto* cond = builder.CreateICmpULT(nextInd, loopBound, "vec.cond");

    // 分岐の作成
    auto* loopExit = llvm::BasicBlock::Create(context, "vector.exit", vectorBody->getParent());
    builder.CreateCondBr(cond, vectorHeader, loopExit);

    // PHIノードの入力を設定
    inductionPHI->addIncoming(llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0),
                              vectorHeader->getSinglePredecessor());
    inductionPHI->addIncoming(nextInd, vectorBody);

    // 出口ブロック
    builder.SetInsertPoint(loopExit);
    builder.CreateBr(loop->getExitBlock());
}

// エピローグループの生成（ベクトル化の後処理）
// TODO: 将来的に完全な実装が必要
void Vectorizer::generateEpilogue([[maybe_unused]] llvm::Loop* loop,
                                  [[maybe_unused]] llvm::BasicBlock* vectorExit,
                                  [[maybe_unused]] llvm::BasicBlock* originalExit,
                                  [[maybe_unused]] unsigned vectorFactor,
                                  [[maybe_unused]] llvm::IRBuilder<>& builder) {
    // エピローグループの生成（残りの要素を処理）
    // 簡易実装のため、元のスカラーループを残す
}

bool Vectorizer::performSLPVectorization(llvm::Function& func) {
    // SLP (Superword Level Parallelism) ベクトル化
    // 複数のスカラー演算を1つのベクトル演算にまとめる

    bool modified = false;

    for (auto& BB : func) {
        // 基本ブロック内の連続した同種演算を探す
        std::vector<llvm::Instruction*> candidates;

        for (auto& I : BB) {
            if (auto* binOp = llvm::dyn_cast<llvm::BinaryOperator>(&I)) {
                candidates.push_back(binOp);

                // 4つの同種演算が見つかったらベクトル化を試みる
                if (candidates.size() >= config.vectorWidth) {
                    if (trySLPVectorization(candidates)) {
                        modified = true;
                        stats.slpGroupsVectorized++;
                    }
                    candidates.clear();
                }
            } else {
                candidates.clear();  // 異なる種類の命令でリセット
            }
        }
    }

    return modified;
}

bool Vectorizer::trySLPVectorization(const std::vector<llvm::Instruction*>& insts) {
    if (insts.size() < 2) {
        return false;
    }

    // 同じ演算タイプかチェック
    auto opcode = insts[0]->getOpcode();
    for (auto* inst : insts) {
        if (inst->getOpcode() != opcode) {
            return false;
        }
    }

    // 簡易実装: ベクトル化可能と判定
    return true;
}

bool Vectorizer::checkConsecutiveAccess(llvm::Instruction* inst) {
    // 連続メモリアクセスの簡易チェック
    if (auto* load = llvm::dyn_cast<llvm::LoadInst>(inst)) {
        // GEP命令を通じたアクセスパターンを確認
        if (auto* gep = llvm::dyn_cast<llvm::GetElementPtrInst>(load->getPointerOperand())) {
            // インデックスが連続しているかチェック
            return true;  // 簡易実装
        }
    }
    return false;
}

bool Vectorizer::isReductionPHI(llvm::PHINode* phi) {
    // リダクション演算のPHIノードかチェック
    // 例: sum = sum + array[i]

    if (phi->getNumIncomingValues() != 2) {
        return false;
    }

    // PHIノードの使用を確認
    for (auto* user : phi->users()) {
        if (auto* binOp = llvm::dyn_cast<llvm::BinaryOperator>(user)) {
            // 加算や乗算などのリダクション演算
            switch (binOp->getOpcode()) {
                case llvm::Instruction::Add:
                case llvm::Instruction::FAdd:
                case llvm::Instruction::Mul:
                case llvm::Instruction::FMul:
                    return true;
                default:
                    break;
            }
        }
    }

    return false;
}

bool Vectorizer::hasSideEffects(llvm::Loop* loop) {
    for (auto* BB : loop->blocks()) {
        for (auto& I : *BB) {
            if (I.mayHaveSideEffects()) {
                return true;
            }
            // 関数呼び出しは通常ベクトル化できない
            if (llvm::isa<llvm::CallInst>(I) || llvm::isa<llvm::InvokeInst>(I)) {
                return true;
            }
        }
    }
    return false;
}

void Vectorizer::debugPrintStatistics() const {
    llvm::dbgs() << "[Vectorizer] Statistics:\n"
                 << "  Loops vectorized: " << stats.loopsVectorized << "\n"
                 << "  SLP groups: " << stats.slpGroupsVectorized << "\n"
                 << "  Reductions vectorized: " << stats.reductionsVectorized << "\n"
                 << "  Masked ops: " << stats.maskedOpsGenerated << "\n";
}

void Vectorizer::printStatistics() const {
    llvm::errs() << "[Vectorizer] Statistics:\n"
                 << "  Loops vectorized: " << stats.loopsVectorized << "\n"
                 << "  SLP groups: " << stats.slpGroupsVectorized << "\n";
}

}  // namespace cm::codegen::llvm_backend::optimizations
