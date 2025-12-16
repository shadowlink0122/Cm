/// @file terminator.cpp
/// @brief ターミネータ変換処理
/// Print/Format処理はprint_codegen.cppに分離

#include "mir_to_llvm.hpp"

namespace cm::codegen::llvm_backend {

void MIRToLLVM::convertTerminator(const mir::MirTerminator& term) {
    switch (term.kind) {
        case mir::MirTerminator::Goto: {
            auto& gotoData = std::get<mir::MirTerminator::GotoData>(term.data);
            auto target = blocks[gotoData.target];
            builder->CreateBr(target);
            break;
        }
        case mir::MirTerminator::SwitchInt: {
            auto& switchData = std::get<mir::MirTerminator::SwitchIntData>(term.data);
            auto discr = convertOperand(*switchData.discriminant);
            auto defaultBB = blocks[switchData.otherwise];
            auto switchInst = builder->CreateSwitch(discr, defaultBB, switchData.targets.size());

            for (const auto& [value, target] : switchData.targets) {
                auto discrType = discr->getType();
                auto caseVal = llvm::ConstantInt::get(discrType, value);
                switchInst->addCase(llvm::cast<llvm::ConstantInt>(caseVal), blocks[target]);
            }
            break;
        }
        case mir::MirTerminator::Return: {
            if (currentMIRFunction->name == "main") {
                // main関数は常にi32を返す
                if (currentMIRFunction->return_local < currentMIRFunction->locals.size()) {
                    auto retVal = locals[currentMIRFunction->return_local];
                    if (retVal) {
                        if (llvm::isa<llvm::AllocaInst>(retVal)) {
                            auto allocaInst = llvm::cast<llvm::AllocaInst>(retVal);
                            auto allocatedType = allocaInst->getAllocatedType();
                            retVal = builder->CreateLoad(allocatedType, retVal, "retval");
                        }
                        builder->CreateRet(retVal);
                    } else {
                        builder->CreateRet(llvm::ConstantInt::get(ctx.getI32Type(), 0));
                    }
                } else {
                    builder->CreateRet(llvm::ConstantInt::get(ctx.getI32Type(), 0));
                }
            } else {
                auto& returnLocal = currentMIRFunction->locals[currentMIRFunction->return_local];
                bool isVoidReturn =
                    returnLocal.type && returnLocal.type->kind == hir::TypeKind::Void;

                if (isVoidReturn) {
                    builder->CreateRetVoid();
                } else if (currentMIRFunction->return_local < currentMIRFunction->locals.size()) {
                    auto retVal = locals[currentMIRFunction->return_local];
                    if (retVal) {
                        if (llvm::isa<llvm::AllocaInst>(retVal)) {
                            auto allocaInst = llvm::cast<llvm::AllocaInst>(retVal);
                            auto allocatedType = allocaInst->getAllocatedType();
                            retVal = builder->CreateLoad(allocatedType, retVal, "retval");
                        }
                        builder->CreateRet(retVal);
                    } else {
                        builder->CreateRetVoid();
                    }
                } else {
                    builder->CreateRetVoid();
                }
            }
            break;
        }
        case mir::MirTerminator::Unreachable: {
            builder->CreateUnreachable();
            break;
        }
        case mir::MirTerminator::Call: {
            auto& callData = std::get<mir::MirTerminator::CallData>(term.data);

            // 関数名を取得
            std::string funcName;
            bool isIndirectCall = false;          // 関数ポインタ変数からの呼び出し
            llvm::Value* funcPtrValue = nullptr;  // 関数ポインタ値

            if (callData.func->kind == mir::MirOperand::Constant) {
                auto& constant = std::get<mir::MirConstant>(callData.func->data);
                if (auto* name = std::get_if<std::string>(&constant.value)) {
                    funcName = *name;
                }
            } else if (callData.func->kind == mir::MirOperand::FunctionRef) {
                funcName = std::get<std::string>(callData.func->data);
            } else if (callData.func->kind == mir::MirOperand::Copy ||
                       callData.func->kind == mir::MirOperand::Move) {
                // 関数ポインタ変数からの呼び出し
                isIndirectCall = true;
                funcPtrValue = convertOperand(*callData.func);
            }

            // ============================================================
            // Print/Format系の特別処理（ヘルパー関数を使用）
            // ============================================================
            if (funcName == "cm_println_format" || funcName == "cm_print_format") {
                bool isNewline = funcName.find("println") != std::string::npos;
                generatePrintFormatCall(callData, isNewline);
                builder->CreateBr(blocks[callData.success]);
                break;
            }

            if (funcName == "cm_format_string") {
                generateFormatStringCall(callData);
                builder->CreateBr(blocks[callData.success]);
                break;
            }

            if (funcName == "print" || funcName == "println" || funcName == "std::io::print" ||
                funcName == "std::io::println") {
                bool isNewline = funcName.find("println") != std::string::npos;
                generatePrintCall(callData, isNewline);
                builder->CreateBr(blocks[callData.success]);
                break;
            }

            // ============================================================
            // 通常の関数呼び出し
            // ============================================================
            std::vector<llvm::Value*> args;
            for (const auto& arg : callData.args) {
                args.push_back(convertOperand(*arg));
            }

            // インターフェースメソッド呼び出しの場合（動的ディスパッチ）
            if (callData.is_virtual && !callData.interface_name.empty() && !args.empty()) {
                if (callData.args.size() > 0) {
                    auto& firstArg = callData.args[0];
                    if (firstArg->kind == mir::MirOperand::Copy ||
                        firstArg->kind == mir::MirOperand::Move) {
                        auto& place = std::get<mir::MirPlace>(firstArg->data);
                        if (currentMIRFunction && place.local < currentMIRFunction->locals.size()) {
                            auto& local = currentMIRFunction->locals[place.local];
                            if (local.type && local.type->kind == hir::TypeKind::Struct) {
                                std::string actualTypeName = local.type->name;

                                if (isInterfaceType(actualTypeName)) {
                                    // 動的ディスパッチ
                                    auto receiverValue = args[0];
                                    auto fatPtrType = getInterfaceFatPtrType(actualTypeName);

                                    llvm::Value* dataPtr = nullptr;
                                    llvm::Value* vtablePtr = nullptr;

                                    if (receiverValue->getType()->isPointerTy()) {
                                        // ポインタとして渡された場合（古い方法）
                                        auto dataFieldPtr = builder->CreateStructGEP(
                                            fatPtrType, receiverValue, 0, "data_field_ptr");
                                        dataPtr = builder->CreateLoad(ctx.getPtrType(),
                                                                      dataFieldPtr, "data_ptr");
                                        auto vtableFieldPtr = builder->CreateStructGEP(
                                            fatPtrType, receiverValue, 1, "vtable_field_ptr");
                                        vtablePtr = builder->CreateLoad(
                                            ctx.getPtrType(), vtableFieldPtr, "vtable_ptr");
                                    } else {
                                        // 値として渡された場合（正しい方法）
                                        dataPtr = builder->CreateExtractValue(receiverValue, 0,
                                                                              "data_ptr");
                                        vtablePtr = builder->CreateExtractValue(receiverValue, 1,
                                                                                "vtable_ptr");
                                    }

                                    int methodIndex = -1;
                                    if (currentProgram) {
                                        for (const auto& iface : currentProgram->interfaces) {
                                            if (iface && iface->name == actualTypeName) {
                                                for (size_t i = 0; i < iface->methods.size(); ++i) {
                                                    if (iface->methods[i].name ==
                                                        callData.method_name) {
                                                        methodIndex = static_cast<int>(i);
                                                        break;
                                                    }
                                                }
                                                break;
                                            }
                                        }
                                    }

                                    if (methodIndex >= 0) {
                                        auto ptrSize = module->getDataLayout().getPointerSize();
                                        auto byteOffset = llvm::ConstantInt::get(
                                            ctx.getI64Type(), methodIndex * ptrSize);
                                        auto funcPtrPtr = builder->CreateGEP(
                                            ctx.getI8Type(), vtablePtr, byteOffset, "func_ptr_ptr");
                                        auto ptrPtrType =
                                            llvm::PointerType::get(ctx.getPtrType(), 0);
                                        auto funcPtrPtrCast = builder->CreateBitCast(
                                            funcPtrPtr, ptrPtrType, "func_ptr_ptr_cast");
                                        auto funcPtr = builder->CreateLoad(
                                            ctx.getPtrType(), funcPtrPtrCast, "func_ptr");

                                        std::vector<llvm::Type*> paramTypes = {ctx.getPtrType()};
                                        auto funcType = llvm::FunctionType::get(ctx.getVoidType(),
                                                                                paramTypes, false);
                                        auto funcPtrTypePtr = llvm::PointerType::get(funcType, 0);
                                        auto funcPtrCast = builder->CreateBitCast(
                                            funcPtr, funcPtrTypePtr, "func_ptr_cast");

                                        std::vector<llvm::Value*> callArgs = {dataPtr};
                                        builder->CreateCall(funcType, funcPtrCast, callArgs);
                                    }

                                    if (callData.success != mir::INVALID_BLOCK) {
                                        builder->CreateBr(blocks[callData.success]);
                                    }
                                    break;
                                } else {
                                    // 静的ディスパッチ
                                    std::string implFuncName =
                                        actualTypeName + "__" + callData.method_name;
                                    llvm::Function* implFunc = functions[implFuncName];
                                    if (!implFunc) {
                                        implFunc = declareExternalFunction(implFuncName);
                                    }

                                    if (implFunc) {
                                        auto funcType = implFunc->getFunctionType();
                                        for (size_t i = 0;
                                             i < args.size() && i < funcType->getNumParams(); ++i) {
                                            auto expectedType = funcType->getParamType(i);
                                            auto actualType = args[i]->getType();
                                            if (expectedType != actualType &&
                                                expectedType->isPointerTy() &&
                                                actualType->isPointerTy()) {
                                                args[i] =
                                                    builder->CreateBitCast(args[i], expectedType);
                                            }
                                        }

                                        auto result = builder->CreateCall(implFunc, args);
                                        if (callData.destination) {
                                            locals[callData.destination->local] = result;
                                        }
                                    }

                                    if (callData.success != mir::INVALID_BLOCK) {
                                        builder->CreateBr(blocks[callData.success]);
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            // 間接呼び出しの場合は直接呼び出しの処理をスキップ
            llvm::Function* callee = nullptr;
            if (!isIndirectCall && !funcName.empty()) {
                callee = functions[funcName];
                if (!callee) {
                    callee = declareExternalFunction(funcName);
                }
            }

            if (callee) {
                auto funcType = callee->getFunctionType();
                for (size_t i = 0; i < args.size() && i < funcType->getNumParams(); ++i) {
                    auto expectedType = funcType->getParamType(i);
                    auto actualType = args[i]->getType();

                    if (expectedType != actualType) {
                        std::string actualTypeName;
                        if (i < callData.args.size()) {
                            auto& arg = callData.args[i];
                            if ((arg->kind == mir::MirOperand::Copy ||
                                 arg->kind == mir::MirOperand::Move)) {
                                auto& place = std::get<mir::MirPlace>(arg->data);
                                if (currentMIRFunction &&
                                    place.local < currentMIRFunction->locals.size()) {
                                    auto& local = currentMIRFunction->locals[place.local];
                                    if (local.type && local.type->kind == hir::TypeKind::Struct) {
                                        actualTypeName = local.type->name;
                                    }
                                }
                            }
                        }

                        // 構造体をインターフェースパラメータに渡す場合、fat pointerを作成
                        if (!actualTypeName.empty() && !isInterfaceType(actualTypeName)) {
                            std::string expectedInterfaceName;
                            if (currentProgram) {
                                for (const auto& func : currentProgram->functions) {
                                    if (func && func->name == funcName) {
                                        if (i < func->arg_locals.size()) {
                                            auto argLocal = func->arg_locals[i];
                                            if (argLocal < func->locals.size()) {
                                                auto& paramLocal = func->locals[argLocal];
                                                if (paramLocal.type &&
                                                    isInterfaceType(paramLocal.type->name)) {
                                                    expectedInterfaceName = paramLocal.type->name;
                                                }
                                            }
                                        }
                                        break;
                                    }
                                }
                            }

                            if (!expectedInterfaceName.empty()) {
                                auto fatPtrType = getInterfaceFatPtrType(expectedInterfaceName);
                                std::string vtableKey =
                                    actualTypeName + "_" + expectedInterfaceName;
                                llvm::Value* vtablePtr = nullptr;
                                auto vtableIt = vtableGlobals.find(vtableKey);
                                if (vtableIt != vtableGlobals.end()) {
                                    vtablePtr = vtableIt->second;
                                } else {
                                    vtablePtr = llvm::Constant::getNullValue(ctx.getPtrType());
                                }

                                // 引数が構造体へのポインタの場合、そのポインタをdata
                                // pointerとして使用
                                llvm::Value* dataPtr = args[i];

                                // 構造体値の場合は、その値をヒープにコピーする
                                // これにより、インターフェース呼び出し後もデータが有効になる
                                if (!dataPtr->getType()->isPointerTy()) {
                                    // スタック上に永続的なコピーを作成（呼び出し後も有効）
                                    auto structType = dataPtr->getType();
                                    auto structAlloca = builder->CreateAlloca(structType, nullptr,
                                                                              "interface_data");
                                    builder->CreateStore(dataPtr, structAlloca);
                                    dataPtr = structAlloca;
                                }

                                auto fatPtrAlloca =
                                    builder->CreateAlloca(fatPtrType, nullptr, "fat_ptr");
                                auto dataFieldPtr = builder->CreateStructGEP(
                                    fatPtrType, fatPtrAlloca, 0, "data_field");
                                auto dataPtrCast = builder->CreateBitCast(dataPtr, ctx.getPtrType(),
                                                                          "data_ptr_cast");
                                builder->CreateStore(dataPtrCast, dataFieldPtr);

                                auto vtableFieldPtr = builder->CreateStructGEP(
                                    fatPtrType, fatPtrAlloca, 1, "vtable_field");
                                auto vtablePtrCast = builder->CreateBitCast(
                                    vtablePtr, ctx.getPtrType(), "vtable_ptr_cast");
                                builder->CreateStore(vtablePtrCast, vtableFieldPtr);

                                // Fat pointerを値として渡す
                                auto fatPtrValue =
                                    builder->CreateLoad(fatPtrType, fatPtrAlloca, "fat_ptr_value");
                                args[i] = fatPtrValue;
                                continue;
                            }
                        }

                        if (expectedType->isPointerTy() && actualType->isPointerTy()) {
                            args[i] = builder->CreateBitCast(args[i], expectedType);
                        }
                        // 整数型のサイズが異なる場合、変換
                        else if (expectedType->isIntegerTy() && actualType->isIntegerTy()) {
                            unsigned expectedBits = expectedType->getIntegerBitWidth();
                            unsigned actualBits = actualType->getIntegerBitWidth();
                            if (expectedBits > actualBits) {
                                // MIRの型情報から符号付きかどうかを判定
                                bool isSigned = true;  // デフォルトは符号付き
                                if (i < callData.args.size()) {
                                    auto argType = getOperandType(*callData.args[i]);
                                    if (argType) {
                                        // Unsigned型かどうかをチェック
                                        isSigned = argType->is_signed() ||
                                                   (argType->kind != hir::TypeKind::UTiny &&
                                                    argType->kind != hir::TypeKind::UShort &&
                                                    argType->kind != hir::TypeKind::UInt &&
                                                    argType->kind != hir::TypeKind::ULong);
                                    }
                                }
                                if (isSigned) {
                                    args[i] = builder->CreateSExt(args[i], expectedType, "sext");
                                } else {
                                    args[i] = builder->CreateZExt(args[i], expectedType, "zext");
                                }
                            } else if (expectedBits < actualBits) {
                                args[i] = builder->CreateTrunc(args[i], expectedType, "trunc");
                            }
                        }
                    }
                }

                auto result = builder->CreateCall(callee, args);
                if (callData.destination) {
                    locals[callData.destination->local] = result;
                }
            }
            // 間接呼び出し（関数ポインタ変数経由）
            else if (isIndirectCall && funcPtrValue) {
                // 関数ポインタ変数の型情報から関数型を取得
                hir::TypePtr funcPtrType = nullptr;
                if (callData.func->kind == mir::MirOperand::Copy ||
                    callData.func->kind == mir::MirOperand::Move) {
                    auto& place = std::get<mir::MirPlace>(callData.func->data);
                    if (currentMIRFunction && place.local < currentMIRFunction->locals.size()) {
                        funcPtrType = currentMIRFunction->locals[place.local].type;
                    }
                }

                if (funcPtrType && funcPtrType->kind == hir::TypeKind::Function) {
                    // 関数型を構築
                    llvm::Type* retType = funcPtrType->return_type
                                              ? convertType(funcPtrType->return_type)
                                              : ctx.getVoidType();
                    std::vector<llvm::Type*> paramTypes;
                    for (const auto& paramType : funcPtrType->param_types) {
                        paramTypes.push_back(convertType(paramType));
                    }

                    auto funcType = llvm::FunctionType::get(retType, paramTypes, false);

                    // 関数ポインタを適切な型にキャスト
                    auto funcPtrTypePtr = llvm::PointerType::get(funcType, 0);
                    auto funcPtrCast =
                        builder->CreateBitCast(funcPtrValue, funcPtrTypePtr, "func_ptr_cast");

                    // 間接呼び出し（void戻り値の場合は名前を付けない）
                    llvm::Value* result = nullptr;
                    if (retType->isVoidTy()) {
                        result = builder->CreateCall(funcType, funcPtrCast, args);
                    } else {
                        result = builder->CreateCall(funcType, funcPtrCast, args, "indirect_call");
                    }
                    if (callData.destination && result && !retType->isVoidTy()) {
                        locals[callData.destination->local] = result;
                    }
                } else {
                    // 型情報が取得できない場合のフォールバック
                    // 引数と戻り値から関数型を推測
                    std::vector<llvm::Type*> paramTypes;
                    for (const auto& arg : args) {
                        paramTypes.push_back(arg->getType());
                    }
                    auto funcType = llvm::FunctionType::get(ctx.getI32Type(), paramTypes, false);
                    auto funcPtrTypePtr = llvm::PointerType::get(funcType, 0);
                    auto funcPtrCast =
                        builder->CreateBitCast(funcPtrValue, funcPtrTypePtr, "func_ptr_cast");

                    auto result = builder->CreateCall(funcType, funcPtrCast, args, "indirect_call");
                    if (callData.destination) {
                        locals[callData.destination->local] = result;
                    }
                }
            }

            if (callData.success != mir::INVALID_BLOCK) {
                builder->CreateBr(blocks[callData.success]);
            }
            break;
        }
    }
}

}  // namespace cm::codegen::llvm_backend
