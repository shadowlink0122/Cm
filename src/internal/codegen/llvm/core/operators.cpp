/// @file llvm_operators.cpp
/// @brief 二項演算・単項演算・論理演算処理

#include "mir_to_llvm.hpp"

#include <functional>
#include <string>

namespace cm::codegen::llvm_backend {

// 符号なし型かどうかを判定するヘルパー
static bool isUnsignedType(const hir::TypePtr& type) {
    if (!type)
        return false;
    switch (type->kind) {
        case ast::TypeKind::UTiny:
        case ast::TypeKind::UShort:
        case ast::TypeKind::UInt:
        case ast::TypeKind::ULong:
        case ast::TypeKind::UFloat:
        case ast::TypeKind::UDouble:
            return true;
        default:
            return false;
    }
}

// 整数ゼロ除算の実行時チェックを挿入する。
// 除数が定数0なら無条件で、非定数なら分岐でトラップする（従来はLLVMのsdiv/udivの未定義動作でゴミ値が返っていた）
static void emitDivByZeroCheck(llvm::IRBuilder<>* builder, llvm::LLVMContext& llvm_ctx,
                               llvm::Value* rhs,
                               const std::function<void(const std::string&)>& panic) {
    if (!rhs->getType()->isIntegerTy()) {
        return;
    }
    // 非ゼロ定数はチェック不要
    if (auto* c = llvm::dyn_cast<llvm::ConstantInt>(rhs)) {
        if (!c->isZero()) {
            return;
        }
    }
    auto func = builder->GetInsertBlock()->getParent();
    auto zero = llvm::ConstantInt::get(rhs->getType(), 0);
    auto is_zero = builder->CreateICmpEQ(rhs, zero, "divzero.check");
    auto failBB = llvm::BasicBlock::Create(llvm_ctx, "divzero.fail", func);
    auto contBB = llvm::BasicBlock::Create(llvm_ctx, "divzero.cont", func);
    builder->CreateCondBr(is_zero, failBB, contBB);
    builder->SetInsertPoint(failBB);
    panic("integer division by zero");
    builder->SetInsertPoint(contBB);
}

// 浮動小数点型の型昇格（float/doubleの統一）
static void coerceFloatTypes(llvm::IRBuilder<>* builder, llvm::Value*& lhs, llvm::Value*& rhs) {
    auto lhsType = lhs->getType();
    auto rhsType = rhs->getType();

    // 両方が浮動小数点で型が異なる場合
    if (lhsType->isFloatingPointTy() && rhsType->isFloatingPointTy() && lhsType != rhsType) {
        // floatをdoubleに拡張
        if (lhsType->isFloatTy() && rhsType->isDoubleTy()) {
            lhs = builder->CreateFPExt(lhs, rhsType, "fpext");
        } else if (lhsType->isDoubleTy() && rhsType->isFloatTy()) {
            rhs = builder->CreateFPExt(rhs, lhsType, "fpext");
        }
    }
}

// ポインタ演算の要素サイズをDataLayoutから計算する。
// 従来は構造体名パターン（Vector→16等）のハードコード推定で、wasm32等のポインタ幅が異なるターゲットや未知の構造体で実レイアウトとずれ、隣接要素の読み書き破壊を起こしていた
int64_t MIRToLLVM::getElementAllocSize(const hir::TypePtr& type) {
    if (!type)
        return 1;

    // ポインタ型の場合、要素型のサイズを返す
    if (type->kind == ast::TypeKind::Pointer && type->element_type) {
        return getElementAllocSize(type->element_type);
    }

    llvm::Type* llvm_type = convertType(type);
    if (llvm_type && llvm_type->isSized()) {
        return static_cast<int64_t>(module->getDataLayout().getTypeAllocSize(llvm_type));
    }
    // void等のサイズ不定型はバイト単位（void*演算互換）
    return 1;
}

// 二項演算変換
llvm::Value* MIRToLLVM::convertBinaryOp(mir::MirBinaryOp op, llvm::Value* lhs, llvm::Value* rhs,
                                        const hir::TypePtr& result_type,
                                        const hir::TypePtr& lhs_type,
                                        const hir::TypePtr& rhs_type) {
    // 符号なしセマンティクスの判定:
    // 比較の result_type は bool になるため、オペランド型も参照する
    const bool operands_unsigned =
        isUnsignedType(lhs_type) || isUnsignedType(rhs_type) || isUnsignedType(result_type);
    // 文字列比較の判定: LLVM上はstringも他のポインタも同じptr型のため、
    // オペランドのCm型がstring/cstringのときだけ内容比較（cm_strcmp）にする。
    // これを見ないと int* 同士の順序/等値比較までstrcmpになりポインタ演算が壊れる（C2の回帰防止）。
    auto is_string_type = [](const hir::TypePtr& t) {
        return t && (t->kind == ast::TypeKind::String || t->kind == ast::TypeKind::CString);
    };
    const bool string_compare = is_string_type(lhs_type) || is_string_type(rhs_type);
    switch (op) {
        // 算術演算
        case mir::MirBinaryOp::Add: {
            auto lhsType = lhs->getType();
            auto rhsType = rhs->getType();

            // ポインタ演算: pointer + int
            if (lhsType->isPointerTy() && rhsType->isIntegerTy()) {
                // 要素サイズを取得
                int64_t elem_size = 1;
                if (result_type && result_type->kind == ast::TypeKind::Pointer &&
                    result_type->element_type) {
                    elem_size = getElementAllocSize(result_type->element_type);
                }

                auto idx = rhs;
                if (rhsType->getIntegerBitWidth() != 64) {
                    idx = builder->CreateSExt(rhs, ctx.getI64Type(), "idx_ext");
                }

                // オフセットを要素サイズで乗算
                if (elem_size > 1) {
                    auto size_val = llvm::ConstantInt::get(ctx.getI64Type(), elem_size);
                    idx = builder->CreateMul(idx, size_val, "scaled_idx");
                }

                return builder->CreateGEP(ctx.getI8Type(), lhs, idx, "ptr_add");
            }

            // ポインタ演算: int + pointer
            if (lhsType->isIntegerTy() && rhsType->isPointerTy()) {
                int64_t elem_size = 1;
                if (result_type && result_type->kind == ast::TypeKind::Pointer &&
                    result_type->element_type) {
                    elem_size = getElementAllocSize(result_type->element_type);
                }

                auto idx = lhs;
                if (lhsType->getIntegerBitWidth() != 64) {
                    idx = builder->CreateSExt(lhs, ctx.getI64Type(), "idx_ext");
                }

                if (elem_size > 1) {
                    auto size_val = llvm::ConstantInt::get(ctx.getI64Type(), elem_size);
                    idx = builder->CreateMul(idx, size_val, "scaled_idx");
                }

                return builder->CreateGEP(ctx.getI8Type(), rhs, idx, "ptr_add");
            }

            // 文字列連結の処理（両方がポインタ）
            if (lhsType->isPointerTy() && rhsType->isPointerTy()) {
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
            if (lhs->getType()->isFloatingPointTy() || rhs->getType()->isFloatingPointTy()) {
                coerceFloatTypes(builder, lhs, rhs);
                return builder->CreateFAdd(lhs, rhs, "fadd");
            }

            // 配列型（文字列リテラル [n x i8]）の場合は文字列連結として処理
            if (lhsType->isArrayTy() || rhsType->isArrayTy()) {
                // 配列をポインタに変換
                llvm::Value* lhsPtr = lhs;
                llvm::Value* rhsPtr = rhs;

                if (lhsType->isArrayTy()) {
                    // 配列をallocaに格納してGEPでポインタ取得
                    auto lhsAlloca = builder->CreateAlloca(lhsType, nullptr, "str_tmp");
                    builder->CreateStore(lhs, lhsAlloca);
                    lhsPtr = builder->CreateGEP(lhsType, lhsAlloca,
                                                {llvm::ConstantInt::get(ctx.getI32Type(), 0),
                                                 llvm::ConstantInt::get(ctx.getI32Type(), 0)},
                                                "str_ptr");
                }

                if (rhsType->isArrayTy()) {
                    auto rhsAlloca = builder->CreateAlloca(rhsType, nullptr, "str_tmp");
                    builder->CreateStore(rhs, rhsAlloca);
                    rhsPtr = builder->CreateGEP(rhsType, rhsAlloca,
                                                {llvm::ConstantInt::get(ctx.getI32Type(), 0),
                                                 llvm::ConstantInt::get(ctx.getI32Type(), 0)},
                                                "str_ptr");
                }

                // 文字列連結関数を呼び出す
                auto concatFunc = module->getOrInsertFunction(
                    "cm_string_concat",
                    llvm::FunctionType::get(ctx.getPtrType(), {ctx.getPtrType(), ctx.getPtrType()},
                                            false));
                return builder->CreateCall(concatFunc, {lhsPtr, rhsPtr});
            }

            // 整数型のビット幅を揃える（long + int 等）
            if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
                auto lhsBits = lhs->getType()->getIntegerBitWidth();
                auto rhsBits = rhs->getType()->getIntegerBitWidth();
                // 拡張は拡張されるオペランド自身の符号で選ぶ（結果型基準だとushort+intでsext誤用）
                if (lhsBits < rhsBits) {
                    lhs = isUnsignedType(lhs_type)
                              ? builder->CreateZExt(lhs, rhs->getType(), "zext")
                              : builder->CreateSExt(lhs, rhs->getType(), "sext");
                } else if (rhsBits < lhsBits) {
                    rhs = isUnsignedType(rhs_type)
                              ? builder->CreateZExt(rhs, lhs->getType(), "zext")
                              : builder->CreateSExt(rhs, lhs->getType(), "sext");
                }
            }

            return builder->CreateAdd(lhs, rhs, "add");
        }
        case mir::MirBinaryOp::Sub: {
            auto lhsType = lhs->getType();
            auto rhsType = rhs->getType();

            // ポインタ演算: pointer - int
            if (lhsType->isPointerTy() && rhsType->isIntegerTy()) {
                int64_t elem_size = 1;
                if (result_type && result_type->kind == ast::TypeKind::Pointer &&
                    result_type->element_type) {
                    elem_size = getElementAllocSize(result_type->element_type);
                }

                auto idx = rhs;
                if (rhsType->getIntegerBitWidth() != 64) {
                    idx = builder->CreateSExt(rhs, ctx.getI64Type(), "idx_ext");
                }

                // オフセットを要素サイズで乗算してから負にする
                if (elem_size > 1) {
                    auto size_val = llvm::ConstantInt::get(ctx.getI64Type(), elem_size);
                    idx = builder->CreateMul(idx, size_val, "scaled_idx");
                }

                auto negIdx = builder->CreateNeg(idx, "neg_idx");
                return builder->CreateGEP(ctx.getI8Type(), lhs, negIdx, "ptr_sub");
            }

            // ポインタ差分: pointer - pointer (バイト単位の差)
            if (lhsType->isPointerTy() && rhsType->isPointerTy()) {
                auto lhsInt = builder->CreatePtrToInt(lhs, ctx.getI64Type(), "ptr_to_int");
                auto rhsInt = builder->CreatePtrToInt(rhs, ctx.getI64Type(), "ptr_to_int");
                return builder->CreateSub(lhsInt, rhsInt, "ptr_diff");
            }

            if (lhsType->isFloatingPointTy() || rhsType->isFloatingPointTy()) {
                coerceFloatTypes(builder, lhs, rhs);
                return builder->CreateFSub(lhs, rhs, "fsub");
            }
            // 整数型のビット幅を揃える（long - int 等）
            if (lhsType->isIntegerTy() && rhsType->isIntegerTy()) {
                auto lhsBits = lhsType->getIntegerBitWidth();
                auto rhsBits = rhsType->getIntegerBitWidth();
                // 拡張は拡張されるオペランド自身の符号で選ぶ（Addと同じ理由）
                if (lhsBits < rhsBits) {
                    lhs = isUnsignedType(lhs_type) ? builder->CreateZExt(lhs, rhsType, "zext")
                                                   : builder->CreateSExt(lhs, rhsType, "sext");
                } else if (rhsBits < lhsBits) {
                    rhs = isUnsignedType(rhs_type) ? builder->CreateZExt(rhs, lhsType, "zext")
                                                   : builder->CreateSExt(rhs, lhsType, "sext");
                }
            }
            return builder->CreateSub(lhs, rhs, "sub");
        }
        case mir::MirBinaryOp::Mul: {
            if (lhs->getType()->isFloatingPointTy() || rhs->getType()->isFloatingPointTy()) {
                coerceFloatTypes(builder, lhs, rhs);
                return builder->CreateFMul(lhs, rhs, "fmul");
            }
            // 整数型のビット幅を揃える（long * int 等）
            if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
                auto lhsBits = lhs->getType()->getIntegerBitWidth();
                auto rhsBits = rhs->getType()->getIntegerBitWidth();
                // 拡張は拡張されるオペランド自身の符号で選ぶ（結果型基準だとushort+intでsext誤用）
                if (lhsBits < rhsBits) {
                    lhs = isUnsignedType(lhs_type)
                              ? builder->CreateZExt(lhs, rhs->getType(), "zext")
                              : builder->CreateSExt(lhs, rhs->getType(), "sext");
                } else if (rhsBits < lhsBits) {
                    rhs = isUnsignedType(rhs_type)
                              ? builder->CreateZExt(rhs, lhs->getType(), "zext")
                              : builder->CreateSExt(rhs, lhs->getType(), "sext");
                }
            }
            return builder->CreateMul(lhs, rhs, "mul");
        }
        case mir::MirBinaryOp::Div: {
            if (lhs->getType()->isFloatingPointTy() || rhs->getType()->isFloatingPointTy()) {
                coerceFloatTypes(builder, lhs, rhs);
                return builder->CreateFDiv(lhs, rhs, "fdiv");
            }
            // 整数型のビット幅を揃える（long / int 等）
            if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
                auto lhsBits = lhs->getType()->getIntegerBitWidth();
                auto rhsBits = rhs->getType()->getIntegerBitWidth();
                // 拡張は拡張されるオペランド自身の符号で選ぶ（結果型基準だとushort+intでsext誤用）
                if (lhsBits < rhsBits) {
                    lhs = isUnsignedType(lhs_type)
                              ? builder->CreateZExt(lhs, rhs->getType(), "zext")
                              : builder->CreateSExt(lhs, rhs->getType(), "sext");
                } else if (rhsBits < lhsBits) {
                    rhs = isUnsignedType(rhs_type)
                              ? builder->CreateZExt(rhs, lhs->getType(), "zext")
                              : builder->CreateSExt(rhs, lhs->getType(), "sext");
                }
            }
            // ゼロ除算の実行時トラップ
            emitDivByZeroCheck(builder, ctx.getContext(), rhs,
                               [this](const std::string& msg) { generatePanic(msg); });
            // 符号なし型は符号なし除算
            if (operands_unsigned) {
                return builder->CreateUDiv(lhs, rhs, "udiv");
            }
            return builder->CreateSDiv(lhs, rhs, "div");
        }
        case mir::MirBinaryOp::Mod: {
            if (lhs->getType()->isFloatingPointTy() || rhs->getType()->isFloatingPointTy()) {
                coerceFloatTypes(builder, lhs, rhs);
                return builder->CreateFRem(lhs, rhs, "fmod");
            }
            // 整数型のビット幅を揃える（long % int 等）
            if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
                auto lhsBits = lhs->getType()->getIntegerBitWidth();
                auto rhsBits = rhs->getType()->getIntegerBitWidth();
                // 拡張は拡張されるオペランド自身の符号で選ぶ（結果型基準だとushort+intでsext誤用）
                if (lhsBits < rhsBits) {
                    lhs = isUnsignedType(lhs_type)
                              ? builder->CreateZExt(lhs, rhs->getType(), "zext")
                              : builder->CreateSExt(lhs, rhs->getType(), "sext");
                } else if (rhsBits < lhsBits) {
                    rhs = isUnsignedType(rhs_type)
                              ? builder->CreateZExt(rhs, lhs->getType(), "zext")
                              : builder->CreateSExt(rhs, lhs->getType(), "sext");
                }
            }
            // ゼロ除算の実行時トラップ
            emitDivByZeroCheck(builder, ctx.getContext(), rhs,
                               [this](const std::string& msg) { generatePanic(msg); });
            // 符号なし型は符号なし剰余
            if (operands_unsigned) {
                return builder->CreateURem(lhs, rhs, "umod");
            }
            return builder->CreateSRem(lhs, rhs, "mod");
        }

        // 比較演算
        case mir::MirBinaryOp::Eq: {
            if (lhs->getType()->isFloatingPointTy() || rhs->getType()->isFloatingPointTy()) {
                coerceFloatTypes(builder, lhs, rhs);
                return builder->CreateFCmpOEQ(lhs, rhs, "feq");
            }
            // 文字列比較 (cm_strcmp: 自前実装、no_std対応)
            // ポインタ同士の比較は文字列比較として扱う
            // Eq/Neのポインタ→strcmpは既存挙動（派生Eqの構造体/配列フィールド比較が依存）のため
            // string_compareでゲートしない。順序比較(Lt/Le/Gt/Ge)のみ文字列に限定する（C2）。
            if (lhs->getType()->isPointerTy() && rhs->getType()->isPointerTy()) {
                auto strcmpFunc = module->getOrInsertFunction(
                    "cm_strcmp",
                    llvm::FunctionType::get(ctx.getI32Type(), {ctx.getPtrType(), ctx.getPtrType()},
                                            false));
                auto cmpResult = builder->CreateCall(strcmpFunc, {lhs, rhs}, "cm_strcmp");
                return builder->CreateICmpEQ(cmpResult, llvm::ConstantInt::get(ctx.getI32Type(), 0),
                                             "streq");
            }
            // ポインタとnull（整数0）の比較
            if (lhs->getType()->isPointerTy() && rhs->getType()->isIntegerTy()) {
                // 整数（null）をポインタ型に変換
                rhs = builder->CreateIntToPtr(rhs, lhs->getType(), "null_to_ptr");
                return builder->CreateICmpEQ(lhs, rhs, "ptr_eq");
            }
            if (lhs->getType()->isIntegerTy() && rhs->getType()->isPointerTy()) {
                // 整数（null）をポインタ型に変換
                lhs = builder->CreateIntToPtr(lhs, rhs->getType(), "null_to_ptr");
                return builder->CreateICmpEQ(lhs, rhs, "ptr_eq");
            }
            // 整数型のビット幅を揃える。拡張は拡張されるオペランド自身の符号で選ぶ
            // （uintをsextするとint32超の値が負値化し、4277009103 == 0xFEEDFACFがfalseになる）
            if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
                auto lhsBits = lhs->getType()->getIntegerBitWidth();
                auto rhsBits = rhs->getType()->getIntegerBitWidth();
                if (lhsBits < rhsBits) {
                    lhs = isUnsignedType(lhs_type) ? builder->CreateZExt(lhs, rhs->getType())
                                                   : builder->CreateSExt(lhs, rhs->getType());
                } else if (rhsBits < lhsBits) {
                    rhs = isUnsignedType(rhs_type) ? builder->CreateZExt(rhs, lhs->getType())
                                                   : builder->CreateSExt(rhs, lhs->getType());
                }
            }
            return builder->CreateICmpEQ(lhs, rhs, "eq");
        }
        case mir::MirBinaryOp::Ne: {
            if (lhs->getType()->isFloatingPointTy() || rhs->getType()->isFloatingPointTy()) {
                coerceFloatTypes(builder, lhs, rhs);
                return builder->CreateFCmpONE(lhs, rhs, "fne");
            }
            // 文字列比較 (cm_strcmp: 自前実装、no_std対応)
            // ポインタ同士の比較は文字列比較として扱う
            // Eq/Neのポインタ→strcmpは既存挙動のためstring_compareでゲートしない（C2）。
            if (lhs->getType()->isPointerTy() && rhs->getType()->isPointerTy()) {
                auto strcmpFunc = module->getOrInsertFunction(
                    "cm_strcmp",
                    llvm::FunctionType::get(ctx.getI32Type(), {ctx.getPtrType(), ctx.getPtrType()},
                                            false));
                auto cmpResult = builder->CreateCall(strcmpFunc, {lhs, rhs}, "cm_strcmp");
                return builder->CreateICmpNE(cmpResult, llvm::ConstantInt::get(ctx.getI32Type(), 0),
                                             "strne");
            }
            // ポインタとnull（整数0）の比較
            if (lhs->getType()->isPointerTy() && rhs->getType()->isIntegerTy()) {
                rhs = builder->CreateIntToPtr(rhs, lhs->getType(), "null_to_ptr");
                return builder->CreateICmpNE(lhs, rhs, "ptr_ne");
            }
            if (lhs->getType()->isIntegerTy() && rhs->getType()->isPointerTy()) {
                lhs = builder->CreateIntToPtr(lhs, rhs->getType(), "null_to_ptr");
                return builder->CreateICmpNE(lhs, rhs, "ptr_ne");
            }
            // 整数型のビット幅を揃える。拡張は拡張されるオペランド自身の符号で選ぶ（Eqと同じ理由）
            if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
                auto lhsBits = lhs->getType()->getIntegerBitWidth();
                auto rhsBits = rhs->getType()->getIntegerBitWidth();
                if (lhsBits < rhsBits) {
                    lhs = isUnsignedType(lhs_type) ? builder->CreateZExt(lhs, rhs->getType())
                                                   : builder->CreateSExt(lhs, rhs->getType());
                } else if (rhsBits < lhsBits) {
                    rhs = isUnsignedType(rhs_type) ? builder->CreateZExt(rhs, lhs->getType())
                                                   : builder->CreateSExt(rhs, lhs->getType());
                }
            }
            return builder->CreateICmpNE(lhs, rhs, "ne");
        }
        case mir::MirBinaryOp::Lt: {
            if (lhs->getType()->isFloatingPointTy() || rhs->getType()->isFloatingPointTy()) {
                coerceFloatTypes(builder, lhs, rhs);
                return builder->CreateFCmpOLT(lhs, rhs, "flt");
            }
            // 文字列比較（ポインタ同士）は内容比較にする（C2）
            if (string_compare && lhs->getType()->isPointerTy() && rhs->getType()->isPointerTy()) {
                auto strcmpFunc = module->getOrInsertFunction(
                    "cm_strcmp",
                    llvm::FunctionType::get(ctx.getI32Type(), {ctx.getPtrType(), ctx.getPtrType()},
                                            false));
                auto cmpResult = builder->CreateCall(strcmpFunc, {lhs, rhs}, "cm_strcmp");
                return builder->CreateICmpSLT(cmpResult,
                                              llvm::ConstantInt::get(ctx.getI32Type(), 0), "strlt");
            }
            if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
                auto lhsBits = lhs->getType()->getIntegerBitWidth();
                auto rhsBits = rhs->getType()->getIntegerBitWidth();
                if (lhsBits < rhsBits) {
                    lhs = isUnsignedType(lhs_type) ? builder->CreateZExt(lhs, rhs->getType())
                                                   : builder->CreateSExt(lhs, rhs->getType());
                } else if (rhsBits < lhsBits) {
                    rhs = isUnsignedType(rhs_type) ? builder->CreateZExt(rhs, lhs->getType())
                                                   : builder->CreateSExt(rhs, lhs->getType());
                }
            }
            if (operands_unsigned) {
                return builder->CreateICmpULT(lhs, rhs, "ult");
            }
            return builder->CreateICmpSLT(lhs, rhs, "lt");
        }
        case mir::MirBinaryOp::Le: {
            if (lhs->getType()->isFloatingPointTy() || rhs->getType()->isFloatingPointTy()) {
                coerceFloatTypes(builder, lhs, rhs);
                return builder->CreateFCmpOLE(lhs, rhs, "fle");
            }
            // 文字列比較（ポインタ同士）は内容比較にする（C2）
            if (string_compare && lhs->getType()->isPointerTy() && rhs->getType()->isPointerTy()) {
                auto strcmpFunc = module->getOrInsertFunction(
                    "cm_strcmp",
                    llvm::FunctionType::get(ctx.getI32Type(), {ctx.getPtrType(), ctx.getPtrType()},
                                            false));
                auto cmpResult = builder->CreateCall(strcmpFunc, {lhs, rhs}, "cm_strcmp");
                return builder->CreateICmpSLE(cmpResult,
                                              llvm::ConstantInt::get(ctx.getI32Type(), 0), "strle");
            }
            if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
                auto lhsBits = lhs->getType()->getIntegerBitWidth();
                auto rhsBits = rhs->getType()->getIntegerBitWidth();
                if (lhsBits < rhsBits) {
                    lhs = isUnsignedType(lhs_type) ? builder->CreateZExt(lhs, rhs->getType())
                                                   : builder->CreateSExt(lhs, rhs->getType());
                } else if (rhsBits < lhsBits) {
                    rhs = isUnsignedType(rhs_type) ? builder->CreateZExt(rhs, lhs->getType())
                                                   : builder->CreateSExt(rhs, lhs->getType());
                }
            }
            if (operands_unsigned) {
                return builder->CreateICmpULE(lhs, rhs, "ule");
            }
            return builder->CreateICmpSLE(lhs, rhs, "le");
        }
        case mir::MirBinaryOp::Gt: {
            if (lhs->getType()->isFloatingPointTy() || rhs->getType()->isFloatingPointTy()) {
                coerceFloatTypes(builder, lhs, rhs);
                return builder->CreateFCmpOGT(lhs, rhs, "fgt");
            }
            // 文字列比較（ポインタ同士）は内容比較にする（C2）
            if (string_compare && lhs->getType()->isPointerTy() && rhs->getType()->isPointerTy()) {
                auto strcmpFunc = module->getOrInsertFunction(
                    "cm_strcmp",
                    llvm::FunctionType::get(ctx.getI32Type(), {ctx.getPtrType(), ctx.getPtrType()},
                                            false));
                auto cmpResult = builder->CreateCall(strcmpFunc, {lhs, rhs}, "cm_strcmp");
                return builder->CreateICmpSGT(cmpResult,
                                              llvm::ConstantInt::get(ctx.getI32Type(), 0), "strgt");
            }
            if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
                auto lhsBits = lhs->getType()->getIntegerBitWidth();
                auto rhsBits = rhs->getType()->getIntegerBitWidth();
                if (lhsBits < rhsBits) {
                    lhs = isUnsignedType(lhs_type) ? builder->CreateZExt(lhs, rhs->getType())
                                                   : builder->CreateSExt(lhs, rhs->getType());
                } else if (rhsBits < lhsBits) {
                    rhs = isUnsignedType(rhs_type) ? builder->CreateZExt(rhs, lhs->getType())
                                                   : builder->CreateSExt(rhs, lhs->getType());
                }
            }
            if (operands_unsigned) {
                return builder->CreateICmpUGT(lhs, rhs, "ugt");
            }
            return builder->CreateICmpSGT(lhs, rhs, "gt");
        }
        case mir::MirBinaryOp::Ge: {
            if (lhs->getType()->isFloatingPointTy() || rhs->getType()->isFloatingPointTy()) {
                coerceFloatTypes(builder, lhs, rhs);
                return builder->CreateFCmpOGE(lhs, rhs, "fge");
            }
            // 文字列比較（ポインタ同士）は内容比較にする（C2）
            if (string_compare && lhs->getType()->isPointerTy() && rhs->getType()->isPointerTy()) {
                auto strcmpFunc = module->getOrInsertFunction(
                    "cm_strcmp",
                    llvm::FunctionType::get(ctx.getI32Type(), {ctx.getPtrType(), ctx.getPtrType()},
                                            false));
                auto cmpResult = builder->CreateCall(strcmpFunc, {lhs, rhs}, "cm_strcmp");
                return builder->CreateICmpSGE(cmpResult,
                                              llvm::ConstantInt::get(ctx.getI32Type(), 0), "strge");
            }
            if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
                auto lhsBits = lhs->getType()->getIntegerBitWidth();
                auto rhsBits = rhs->getType()->getIntegerBitWidth();
                if (lhsBits < rhsBits) {
                    lhs = isUnsignedType(lhs_type) ? builder->CreateZExt(lhs, rhs->getType())
                                                   : builder->CreateSExt(lhs, rhs->getType());
                } else if (rhsBits < lhsBits) {
                    rhs = isUnsignedType(rhs_type) ? builder->CreateZExt(rhs, lhs->getType())
                                                   : builder->CreateSExt(rhs, lhs->getType());
                }
            }
            if (operands_unsigned) {
                return builder->CreateICmpUGE(lhs, rhs, "uge");
            }
            return builder->CreateICmpSGE(lhs, rhs, "ge");
        }

        // ビット演算（整数型のビット幅を揃える: ulong & int 等）
        case mir::MirBinaryOp::BitXor: {
            if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
                auto lhsBits = lhs->getType()->getIntegerBitWidth();
                auto rhsBits = rhs->getType()->getIntegerBitWidth();
                // 拡張は拡張されるオペランド自身の符号で選ぶ（結果型基準だとushort+intでsext誤用）
                if (lhsBits < rhsBits) {
                    lhs = isUnsignedType(lhs_type)
                              ? builder->CreateZExt(lhs, rhs->getType(), "zext")
                              : builder->CreateSExt(lhs, rhs->getType(), "sext");
                } else if (rhsBits < lhsBits) {
                    rhs = isUnsignedType(rhs_type)
                              ? builder->CreateZExt(rhs, lhs->getType(), "zext")
                              : builder->CreateSExt(rhs, lhs->getType(), "sext");
                }
            }
            return builder->CreateXor(lhs, rhs, "xor");
        }
        case mir::MirBinaryOp::BitAnd: {
            if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
                auto lhsBits = lhs->getType()->getIntegerBitWidth();
                auto rhsBits = rhs->getType()->getIntegerBitWidth();
                // 拡張は拡張されるオペランド自身の符号で選ぶ（結果型基準だとushort+intでsext誤用）
                if (lhsBits < rhsBits) {
                    lhs = isUnsignedType(lhs_type)
                              ? builder->CreateZExt(lhs, rhs->getType(), "zext")
                              : builder->CreateSExt(lhs, rhs->getType(), "sext");
                } else if (rhsBits < lhsBits) {
                    rhs = isUnsignedType(rhs_type)
                              ? builder->CreateZExt(rhs, lhs->getType(), "zext")
                              : builder->CreateSExt(rhs, lhs->getType(), "sext");
                }
            }
            return builder->CreateAnd(lhs, rhs, "bitand");
        }
        case mir::MirBinaryOp::BitOr: {
            if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
                auto lhsBits = lhs->getType()->getIntegerBitWidth();
                auto rhsBits = rhs->getType()->getIntegerBitWidth();
                // 拡張は拡張されるオペランド自身の符号で選ぶ（結果型基準だとushort+intでsext誤用）
                if (lhsBits < rhsBits) {
                    lhs = isUnsignedType(lhs_type)
                              ? builder->CreateZExt(lhs, rhs->getType(), "zext")
                              : builder->CreateSExt(lhs, rhs->getType(), "sext");
                } else if (rhsBits < lhsBits) {
                    rhs = isUnsignedType(rhs_type)
                              ? builder->CreateZExt(rhs, lhs->getType(), "zext")
                              : builder->CreateSExt(rhs, lhs->getType(), "sext");
                }
            }
            return builder->CreateOr(lhs, rhs, "bitor");
        }
        case mir::MirBinaryOp::Shl: {
            // ビット幅を揃える（LLVMはShlの両オペランドに同一型を要求）
            if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
                auto lhsBits = lhs->getType()->getIntegerBitWidth();
                auto rhsBits = rhs->getType()->getIntegerBitWidth();
                if (lhsBits < rhsBits) {
                    lhs = isUnsignedType(lhs_type) ? builder->CreateZExt(lhs, rhs->getType())
                                                   : builder->CreateSExt(lhs, rhs->getType());
                } else if (rhsBits < lhsBits) {
                    rhs = isUnsignedType(rhs_type) ? builder->CreateZExt(rhs, lhs->getType())
                                                   : builder->CreateSExt(rhs, lhs->getType());
                }
            }
            return builder->CreateShl(lhs, rhs, "shl");
        }
        case mir::MirBinaryOp::Shr: {
            // ビット幅を揃える（LLVMはAShrの両オペランドに同一型を要求）
            if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
                auto lhsBits = lhs->getType()->getIntegerBitWidth();
                auto rhsBits = rhs->getType()->getIntegerBitWidth();
                if (lhsBits < rhsBits) {
                    lhs = isUnsignedType(lhs_type) ? builder->CreateZExt(lhs, rhs->getType())
                                                   : builder->CreateSExt(lhs, rhs->getType());
                } else if (rhsBits < lhsBits) {
                    rhs = isUnsignedType(rhs_type) ? builder->CreateZExt(rhs, lhs->getType())
                                                   : builder->CreateSExt(rhs, lhs->getType());
                }
            }
            // 符号なし型は論理シフト、符号付き型は算術シフト
            if (operands_unsigned) {
                return builder->CreateLShr(lhs, rhs, "lshr");
            }
            return builder->CreateAShr(lhs, rhs, "shr");
        }

        // 論理演算
        case mir::MirBinaryOp::And: {
            // bool（i8）を i1 に変換してAND、結果をi8に戻す
            llvm::Value* lhsBool = lhs;
            llvm::Value* rhsBool = rhs;

            if (lhs->getType()->isIntegerTy(8)) {
                lhsBool = builder->CreateICmpNE(lhs, llvm::ConstantInt::get(ctx.getI8Type(), 0),
                                                "lhs_bool");
            }
            if (rhs->getType()->isIntegerTy(8)) {
                rhsBool = builder->CreateICmpNE(rhs, llvm::ConstantInt::get(ctx.getI8Type(), 0),
                                                "rhs_bool");
            }

            auto result = builder->CreateAnd(lhsBool, rhsBool, "logical_and");
            return builder->CreateZExt(result, ctx.getI8Type(), "and_ext");
        }
        case mir::MirBinaryOp::Or: {
            // bool（i8）を i1 に変換してOR、結果をi8に戻す
            llvm::Value* lhsBool = lhs;
            llvm::Value* rhsBool = rhs;

            if (lhs->getType()->isIntegerTy(8)) {
                lhsBool = builder->CreateICmpNE(lhs, llvm::ConstantInt::get(ctx.getI8Type(), 0),
                                                "lhs_bool");
            }
            if (rhs->getType()->isIntegerTy(8)) {
                rhsBool = builder->CreateICmpNE(rhs, llvm::ConstantInt::get(ctx.getI8Type(), 0),
                                                "rhs_bool");
            }

            auto result = builder->CreateOr(lhsBool, rhsBool, "logical_or");
            return builder->CreateZExt(result, ctx.getI8Type(), "or_ext");
        }

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
        case mir::MirUnaryOp::Neg: {
            auto operandType = operand->getType();
            // ポインタ型の場合（プリミティブ型implメソッドのself）
            // i8*からi32*にキャストしてloadしてからNeg
            if (operandType->isPointerTy()) {
                // MIRの型情報から実際の型を取得すべきだが、ここでは整数と仮定
                auto intPtrType = llvm::PointerType::get(ctx.getI32Type(), 0);
                auto castedPtr = builder->CreateBitCast(operand, intPtrType, "neg_ptr_cast");
                operand = builder->CreateLoad(ctx.getI32Type(), castedPtr, "neg_ptr_load");
                operandType = operand->getType();
            }
            // 浮動小数点の場合はFNeg、整数の場合はNeg
            if (operandType->isFloatingPointTy()) {
                return builder->CreateFNeg(operand, "fneg");
            }
            return builder->CreateNeg(operand, "neg");
        }
        case mir::MirUnaryOp::BitNot: {
            // ビット反転: ~operand → xor operand, -1
            return builder->CreateNot(operand, "bitnot");
        }
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
