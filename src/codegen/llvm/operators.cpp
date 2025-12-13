/// @file llvm_operators.cpp
/// @brief 二項演算・単項演算・論理演算処理

#include "mir_to_llvm.hpp"

namespace cm::codegen::llvm_backend {

// 二項演算変換
llvm::Value* MIRToLLVM::convertBinaryOp(mir::MirBinaryOp op, llvm::Value* lhs, llvm::Value* rhs) {
    switch (op) {
        // 算術演算
        case mir::MirBinaryOp::Add: {
            // 文字列連結の処理
            auto lhsType = lhs->getType();
            auto rhsType = rhs->getType();

            // 両方またはどちらかがポインタ（文字列）の場合
            if (lhsType->isPointerTy() || rhsType->isPointerTy()) {
                // 左側を文字列に変換
                llvm::Value* lhsStr = lhs;
                if (!lhsType->isPointerTy()) {
                    if (lhsType->isFloatingPointTy()) {
                        auto formatFunc = module->getOrInsertFunction(
                            "cm_format_double",
                            llvm::FunctionType::get(ctx.getPtrType(), {ctx.getF64Type()}, false));
                        auto lhsDouble = lhs;
                        if (lhsType->isFloatTy()) {
                            lhsDouble = builder->CreateFPExt(lhs, ctx.getF64Type());
                        }
                        lhsStr = builder->CreateCall(formatFunc, {lhsDouble});
                    } else if (lhsType->isIntegerTy()) {
                        auto intType = llvm::cast<llvm::IntegerType>(lhsType);
                        if (intType->getBitWidth() == 8) {
                            auto formatFunc = module->getOrInsertFunction(
                                "cm_format_char", llvm::FunctionType::get(
                                                      ctx.getPtrType(), {ctx.getI8Type()}, false));
                            lhsStr = builder->CreateCall(formatFunc, {lhs});
                        } else {
                            auto formatFunc = module->getOrInsertFunction(
                                "cm_format_int", llvm::FunctionType::get(
                                                     ctx.getPtrType(), {ctx.getI32Type()}, false));
                            auto lhsInt = lhs;
                            if (lhsType->getIntegerBitWidth() != 32) {
                                lhsInt = builder->CreateSExt(lhs, ctx.getI32Type());
                            }
                            lhsStr = builder->CreateCall(formatFunc, {lhsInt});
                        }
                    }
                }

                // 右側を文字列に変換
                llvm::Value* rhsStr = rhs;
                if (!rhsType->isPointerTy()) {
                    if (rhsType->isFloatingPointTy()) {
                        auto formatFunc = module->getOrInsertFunction(
                            "cm_format_double",
                            llvm::FunctionType::get(ctx.getPtrType(), {ctx.getF64Type()}, false));
                        auto rhsDouble = rhs;
                        if (rhsType->isFloatTy()) {
                            rhsDouble = builder->CreateFPExt(rhs, ctx.getF64Type());
                        }
                        rhsStr = builder->CreateCall(formatFunc, {rhsDouble});
                    } else if (rhsType->isIntegerTy()) {
                        auto intType = llvm::cast<llvm::IntegerType>(rhsType);
                        if (intType->getBitWidth() == 8) {
                            auto formatFunc = module->getOrInsertFunction(
                                "cm_format_char", llvm::FunctionType::get(
                                                      ctx.getPtrType(), {ctx.getI8Type()}, false));
                            rhsStr = builder->CreateCall(formatFunc, {rhs});
                        } else {
                            auto formatFunc = module->getOrInsertFunction(
                                "cm_format_int", llvm::FunctionType::get(
                                                     ctx.getPtrType(), {ctx.getI32Type()}, false));
                            auto rhsInt = rhs;
                            if (rhsType->getIntegerBitWidth() != 32) {
                                rhsInt = builder->CreateSExt(rhs, ctx.getI32Type());
                            }
                            rhsStr = builder->CreateCall(formatFunc, {rhsInt});
                        }
                    }
                }

                // 文字列連結関数を呼び出す
                auto concatFunc = module->getOrInsertFunction(
                    "cm_string_concat",
                    llvm::FunctionType::get(ctx.getPtrType(), {ctx.getPtrType(), ctx.getPtrType()},
                                            false));
                return builder->CreateCall(concatFunc, {lhsStr, rhsStr});
            }

            // 通常の数値加算
            if (lhs->getType()->isFloatingPointTy()) {
                return builder->CreateFAdd(lhs, rhs, "fadd");
            }
            return builder->CreateAdd(lhs, rhs, "add");
        }
        case mir::MirBinaryOp::Sub: {
            if (lhs->getType()->isFloatingPointTy()) {
                return builder->CreateFSub(lhs, rhs, "fsub");
            }
            return builder->CreateSub(lhs, rhs, "sub");
        }
        case mir::MirBinaryOp::Mul: {
            if (lhs->getType()->isFloatingPointTy()) {
                return builder->CreateFMul(lhs, rhs, "fmul");
            }
            return builder->CreateMul(lhs, rhs, "mul");
        }
        case mir::MirBinaryOp::Div: {
            if (lhs->getType()->isFloatingPointTy()) {
                return builder->CreateFDiv(lhs, rhs, "fdiv");
            }
            return builder->CreateSDiv(lhs, rhs, "div");
        }
        case mir::MirBinaryOp::Mod: {
            if (lhs->getType()->isFloatingPointTy()) {
                return builder->CreateFRem(lhs, rhs, "fmod");
            }
            return builder->CreateSRem(lhs, rhs, "mod");
        }

        // 比較演算
        case mir::MirBinaryOp::Eq: {
            if (lhs->getType()->isFloatingPointTy()) {
                return builder->CreateFCmpOEQ(lhs, rhs, "feq");
            }
            // 文字列比較
            if (lhs->getType()->isPointerTy() && rhs->getType()->isPointerTy()) {
                auto strcmpFunc = module->getOrInsertFunction(
                    "strcmp", llvm::FunctionType::get(ctx.getI32Type(),
                                                      {ctx.getPtrType(), ctx.getPtrType()}, false));
                auto cmpResult = builder->CreateCall(strcmpFunc, {lhs, rhs}, "strcmp");
                return builder->CreateICmpEQ(cmpResult, llvm::ConstantInt::get(ctx.getI32Type(), 0),
                                             "streq");
            }
            // 整数型のビット幅を揃える
            if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
                auto lhsBits = lhs->getType()->getIntegerBitWidth();
                auto rhsBits = rhs->getType()->getIntegerBitWidth();
                if (lhsBits < rhsBits) {
                    lhs = builder->CreateSExt(lhs, rhs->getType());
                } else if (rhsBits < lhsBits) {
                    rhs = builder->CreateSExt(rhs, lhs->getType());
                }
            }
            return builder->CreateICmpEQ(lhs, rhs, "eq");
        }
        case mir::MirBinaryOp::Ne: {
            if (lhs->getType()->isFloatingPointTy()) {
                return builder->CreateFCmpONE(lhs, rhs, "fne");
            }
            // 文字列比較
            if (lhs->getType()->isPointerTy() && rhs->getType()->isPointerTy()) {
                auto strcmpFunc = module->getOrInsertFunction(
                    "strcmp", llvm::FunctionType::get(ctx.getI32Type(),
                                                      {ctx.getPtrType(), ctx.getPtrType()}, false));
                auto cmpResult = builder->CreateCall(strcmpFunc, {lhs, rhs}, "strcmp");
                return builder->CreateICmpNE(cmpResult, llvm::ConstantInt::get(ctx.getI32Type(), 0),
                                             "strne");
            }
            // 整数型のビット幅を揃える
            if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
                auto lhsBits = lhs->getType()->getIntegerBitWidth();
                auto rhsBits = rhs->getType()->getIntegerBitWidth();
                if (lhsBits < rhsBits) {
                    lhs = builder->CreateSExt(lhs, rhs->getType());
                } else if (rhsBits < lhsBits) {
                    rhs = builder->CreateSExt(rhs, lhs->getType());
                }
            }
            return builder->CreateICmpNE(lhs, rhs, "ne");
        }
        case mir::MirBinaryOp::Lt: {
            if (lhs->getType()->isFloatingPointTy()) {
                return builder->CreateFCmpOLT(lhs, rhs, "flt");
            }
            if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
                auto lhsBits = lhs->getType()->getIntegerBitWidth();
                auto rhsBits = rhs->getType()->getIntegerBitWidth();
                if (lhsBits < rhsBits) {
                    lhs = builder->CreateSExt(lhs, rhs->getType());
                } else if (rhsBits < lhsBits) {
                    rhs = builder->CreateSExt(rhs, lhs->getType());
                }
            }
            return builder->CreateICmpSLT(lhs, rhs, "lt");
        }
        case mir::MirBinaryOp::Le: {
            if (lhs->getType()->isFloatingPointTy()) {
                return builder->CreateFCmpOLE(lhs, rhs, "fle");
            }
            if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
                auto lhsBits = lhs->getType()->getIntegerBitWidth();
                auto rhsBits = rhs->getType()->getIntegerBitWidth();
                if (lhsBits < rhsBits) {
                    lhs = builder->CreateSExt(lhs, rhs->getType());
                } else if (rhsBits < lhsBits) {
                    rhs = builder->CreateSExt(rhs, lhs->getType());
                }
            }
            return builder->CreateICmpSLE(lhs, rhs, "le");
        }
        case mir::MirBinaryOp::Gt: {
            if (lhs->getType()->isFloatingPointTy()) {
                return builder->CreateFCmpOGT(lhs, rhs, "fgt");
            }
            if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
                auto lhsBits = lhs->getType()->getIntegerBitWidth();
                auto rhsBits = rhs->getType()->getIntegerBitWidth();
                if (lhsBits < rhsBits) {
                    lhs = builder->CreateSExt(lhs, rhs->getType());
                } else if (rhsBits < lhsBits) {
                    rhs = builder->CreateSExt(rhs, lhs->getType());
                }
            }
            return builder->CreateICmpSGT(lhs, rhs, "gt");
        }
        case mir::MirBinaryOp::Ge: {
            if (lhs->getType()->isFloatingPointTy()) {
                return builder->CreateFCmpOGE(lhs, rhs, "fge");
            }
            if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
                auto lhsBits = lhs->getType()->getIntegerBitWidth();
                auto rhsBits = rhs->getType()->getIntegerBitWidth();
                if (lhsBits < rhsBits) {
                    lhs = builder->CreateSExt(lhs, rhs->getType());
                } else if (rhsBits < lhsBits) {
                    rhs = builder->CreateSExt(rhs, lhs->getType());
                }
            }
            return builder->CreateICmpSGE(lhs, rhs, "ge");
        }

        // ビット演算
        case mir::MirBinaryOp::BitXor:
            return builder->CreateXor(lhs, rhs, "xor");
        case mir::MirBinaryOp::BitAnd:
            return builder->CreateAnd(lhs, rhs, "bitand");
        case mir::MirBinaryOp::BitOr:
            return builder->CreateOr(lhs, rhs, "bitor");
        case mir::MirBinaryOp::Shl:
            return builder->CreateShl(lhs, rhs, "shl");
        case mir::MirBinaryOp::Shr:
            return builder->CreateAShr(lhs, rhs, "shr");

        default:
            return nullptr;
    }
}

// 単項演算変換
llvm::Value* MIRToLLVM::convertUnaryOp(mir::MirUnaryOp op, llvm::Value* operand) {
    switch (op) {
        case mir::MirUnaryOp::Not: {
            auto operandType = operand->getType();
            if (operandType->isIntegerTy(1)) {
                // i1のbool値の場合：単純なxor
                return builder->CreateXor(operand, llvm::ConstantInt::getTrue(ctx.getContext()),
                                          "logical_not");
            } else if (operandType->isIntegerTy(8)) {
                // i8のbool値の場合：0との比較
                auto zero = llvm::ConstantInt::get(ctx.getI8Type(), 0);
                auto cmp = builder->CreateICmpEQ(operand, zero, "not_cmp");
                return builder->CreateZExt(cmp, ctx.getI8Type(), "logical_not");
            } else if (operandType->isIntegerTy()) {
                // その他の整数型（i32等）がbool値として使われる場合
                // 0か非0かを判定して論理否定
                auto zero = llvm::ConstantInt::get(operandType, 0);
                auto cmp = builder->CreateICmpEQ(operand, zero, "not_cmp");
                return builder->CreateZExt(cmp, operandType, "logical_not");
            } else {
                // フォールバック：ビット反転
                return builder->CreateNot(operand, "not");
            }
        }
        case mir::MirUnaryOp::Neg:
            return builder->CreateNeg(operand, "neg");
        default:
            return nullptr;
    }
}

// 論理AND演算（短絡評価付き）
llvm::Value* MIRToLLVM::convertLogicalAnd(llvm::Value* lhs, llvm::Value* rhs) {
    auto currentBB = builder->GetInsertBlock();
    auto func = currentBB->getParent();

    auto rhsBB = llvm::BasicBlock::Create(ctx.getContext(), "and.rhs", func);
    auto mergeBB = llvm::BasicBlock::Create(ctx.getContext(), "and.merge", func);

    auto zero = llvm::ConstantInt::get(lhs->getType(), 0);
    auto lhsBool = builder->CreateICmpNE(lhs, zero, "lhs.bool");
    builder->CreateCondBr(lhsBool, rhsBB, mergeBB);

    builder->SetInsertPoint(rhsBB);
    builder->CreateBr(mergeBB);

    builder->SetInsertPoint(mergeBB);
    auto phi = builder->CreatePHI(ctx.getI8Type(), 2, "and.result");
    phi->addIncoming(llvm::ConstantInt::get(ctx.getI8Type(), 0), currentBB);
    phi->addIncoming(rhs, rhsBB);

    return phi;
}

// 論理OR演算（短絡評価付き）
llvm::Value* MIRToLLVM::convertLogicalOr(llvm::Value* lhs, llvm::Value* rhs) {
    auto currentBB = builder->GetInsertBlock();
    auto func = currentBB->getParent();

    auto rhsBB = llvm::BasicBlock::Create(ctx.getContext(), "or.rhs", func);
    auto mergeBB = llvm::BasicBlock::Create(ctx.getContext(), "or.merge", func);

    auto zero = llvm::ConstantInt::get(lhs->getType(), 0);
    auto lhsBool = builder->CreateICmpNE(lhs, zero, "lhs.bool");
    builder->CreateCondBr(lhsBool, mergeBB, rhsBB);

    builder->SetInsertPoint(rhsBB);
    builder->CreateBr(mergeBB);

    builder->SetInsertPoint(mergeBB);
    auto phi = builder->CreatePHI(ctx.getI8Type(), 2, "or.result");
    phi->addIncoming(llvm::ConstantInt::get(ctx.getI8Type(), 1), currentBB);
    phi->addIncoming(rhs, rhsBB);

    return phi;
}

}  // namespace cm::codegen::llvm_backend
