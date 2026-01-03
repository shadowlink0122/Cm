/// @file terminator.cpp
/// @brief ターミネータ変換処理
/// Print/Format処理はprint_codegen.cppに分離

#include "mir_to_llvm.hpp"
#include "util/debug.hpp"

#include <iostream>

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
            std::cerr << "[MIR2LLVM]         Entering Call terminator processing\n";
            auto& callData = std::get<mir::MirTerminator::CallData>(term.data);
            std::cerr << "[MIR2LLVM]         Call success block: " << callData.success << "\n";

            // 関数名を取得
            std::string funcName;
            bool isIndirectCall = false;          // 関数ポインタ変数からの呼び出し
            llvm::Value* funcPtrValue = nullptr;  // 関数ポインタ値

            // std::cout << "[CODEGEN] Call Operand Kind: " << (int)callData.func->kind << "\n"
            //           << std::flush;

            if (callData.func->kind == mir::MirOperand::Constant) {
                auto& constant = std::get<mir::MirConstant>(callData.func->data);
                if (auto* name = std::get_if<std::string>(&constant.value)) {
                    funcName = *name;
                    // std::cout << "[CODEGEN] Call Direct: " << funcName << "\n" << std::flush;
                }
            } else if (callData.func->kind == mir::MirOperand::FunctionRef) {
                funcName = std::get<std::string>(callData.func->data);
                // std::cout << "[CODEGEN] Call FuncRef: " << funcName << "\n" << std::flush;
            } else if (callData.func->kind == mir::MirOperand::Copy ||
                       callData.func->kind == mir::MirOperand::Move) {
                // 関数ポインタ変数からの呼び出し
                // std::cout << "[CODEGEN] Call Indirect Checking\n" << std::flush;
                isIndirectCall = true;
                funcPtrValue = convertOperand(*callData.func);

                // convertOperandがFunction*を返した場合、それを関数ポインタとして扱う
                if (funcPtrValue && llvm::isa<llvm::Function>(funcPtrValue)) {
                    // Function*が直接返された場合は、直接呼び出しとして扱う
                    auto func = llvm::cast<llvm::Function>(funcPtrValue);
                    funcName = func->getName().str();
                    isIndirectCall = false;
                    funcPtrValue = nullptr;
                }
                // std::cout << "[CODEGEN] Call Indirect Op Converted\n" << std::flush;
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

            if (funcName == "__print__" || funcName == "__println__" ||
                funcName == "std::io::print" || funcName == "std::io::println") {
                bool isNewline = funcName.find("println") != std::string::npos;
                generatePrintCall(callData, isNewline);
                // std::cout << "[CODEGEN] Print Call Gen Done, Br to bb" << callData.success <<
                // "\n"
                //           << std::flush;
                if (blocks.find(callData.success) == blocks.end()) {
                    std::cerr << "[CODEGEN] CRITICAL: Success block bb" << callData.success
                              << " not found! Creating unreachable.\n"
                              << std::flush;
                    builder->CreateUnreachable();
                    break;
                }
                builder->CreateBr(blocks[callData.success]);
                // std::cout << "[CODEGEN] Br Created\n" << std::flush;
                break;
            }

            // ============================================================
            // 配列スライス呼び出し
            // ============================================================
            if (funcName == "__builtin_array_slice") {
                // 引数: arr, elem_size, arr_len, start, end
                // ランタイム関数: void* __builtin_array_slice(void* arr, i64 elem_size, i64
                // arr_len, i64 start, i64 end, i64* out_len)

                std::vector<llvm::Value*> args;
                for (const auto& arg : callData.args) {
                    args.push_back(convertOperand(*arg));
                }

                // 最初の引数（配列）をポインタに変換
                llvm::Value* arrPtr = args[0];
                if (arrPtr->getType()->isArrayTy()) {
                    // 配列をallocaに格納してポインタを取得
                    auto arrAlloca = builder->CreateAlloca(arrPtr->getType(), nullptr, "arr_tmp");
                    builder->CreateStore(arrPtr, arrAlloca);
                    arrPtr = builder->CreateBitCast(arrAlloca, ctx.getPtrType(), "arr_ptr");
                } else if (!arrPtr->getType()->isPointerTy()) {
                    arrPtr = builder->CreateIntToPtr(arrPtr, ctx.getPtrType(), "arr_ptr");
                }

                // out_len用のallocaを作成
                auto outLenAlloca = builder->CreateAlloca(ctx.getI64Type(), nullptr, "out_len");
                builder->CreateStore(llvm::ConstantInt::get(ctx.getI64Type(), 0), outLenAlloca);

                // ランタイム関数を取得
                auto sliceFunc = declareExternalFunction("__builtin_array_slice");

                // 引数を整数型に変換（必要な場合）
                std::vector<llvm::Value*> callArgs;
                callArgs.push_back(arrPtr);  // void* arr
                for (size_t i = 1; i < args.size() && i <= 4; ++i) {
                    auto arg = args[i];
                    if (arg->getType() != ctx.getI64Type()) {
                        if (arg->getType()->isIntegerTy()) {
                            arg = builder->CreateSExt(arg, ctx.getI64Type(), "sext");
                        }
                    }
                    callArgs.push_back(arg);
                }
                // out_lenポインタをi8*にキャスト（LLVM 14互換性のため）
                auto outLenCast =
                    builder->CreateBitCast(outLenAlloca, ctx.getPtrType(), "out_len_cast");
                callArgs.push_back(outLenCast);  // i8* out_len

                auto result = builder->CreateCall(sliceFunc, callArgs, "slice_result");

                if (callData.destination) {
                    locals[callData.destination->local] = result;
                }

                if (callData.success != mir::INVALID_BLOCK) {
                    builder->CreateBr(blocks[callData.success]);
                }
                break;
            }

            // ============================================================
            // 固定配列比較呼び出し
            // ============================================================
            if (funcName == "cm_array_equal") {
                // 引数: lhs, rhs, lhs_len, rhs_len, elem_size
                // ランタイム関数: bool cm_array_equal(void* lhs, void* rhs, i64 lhs_len, i64
                // rhs_len, i64 elem_size)

                std::vector<llvm::Value*> args;
                for (const auto& arg : callData.args) {
                    args.push_back(convertOperand(*arg));
                }

                // lhsとrhsをポインタに変換
                auto convertToPtr = [&](llvm::Value* val) -> llvm::Value* {
                    if (val->getType()->isArrayTy()) {
                        auto arrAlloca = builder->CreateAlloca(val->getType(), nullptr, "arr_tmp");
                        builder->CreateStore(val, arrAlloca);
                        return builder->CreateBitCast(arrAlloca, ctx.getPtrType(), "arr_ptr");
                    } else if (!val->getType()->isPointerTy()) {
                        return builder->CreateIntToPtr(val, ctx.getPtrType(), "arr_ptr");
                    }
                    return val;
                };

                llvm::Value* lhsPtr = convertToPtr(args[0]);
                llvm::Value* rhsPtr = convertToPtr(args[1]);

                // 残りの引数を整数型に変換
                std::vector<llvm::Value*> callArgs;
                callArgs.push_back(lhsPtr);
                callArgs.push_back(rhsPtr);
                for (size_t i = 2; i < args.size(); ++i) {
                    auto arg = args[i];
                    if (arg->getType() != ctx.getI64Type()) {
                        if (arg->getType()->isIntegerTy()) {
                            arg = builder->CreateSExt(arg, ctx.getI64Type(), "sext");
                        }
                    }
                    callArgs.push_back(arg);
                }

                auto equalFunc = declareExternalFunction("cm_array_equal");
                auto result = builder->CreateCall(equalFunc, callArgs, "array_eq_result");

                if (callData.destination) {
                    locals[callData.destination->local] = result;
                }

                if (callData.success != mir::INVALID_BLOCK) {
                    builder->CreateBr(blocks[callData.success]);
                }
                break;
            }

            // ============================================================
            // 通常の関数呼び出し
            // ============================================================
            std::cerr << "[MIR2LLVM]         Processing normal function call: " << funcName << "\n";
            std::cerr << "[MIR2LLVM]         About to convert " << callData.args.size() << " arguments\n";

            // 引数変換時の無限ループ検出
            static int argConversionDepth = 0;
            const int MAX_ARG_DEPTH = 50;
            if (argConversionDepth >= MAX_ARG_DEPTH) {
                std::cerr << "[MIR2LLVM]         ERROR: Maximum arg conversion depth exceeded!\n";
                throw std::runtime_error("Maximum arg conversion depth exceeded in Call terminator");
            }
            argConversionDepth++;

            std::vector<llvm::Value*> args;
            for (size_t i = 0; i < callData.args.size(); ++i) {
                std::cerr << "[MIR2LLVM]         Converting arg " << i << "/" << callData.args.size()
                          << " (depth=" << argConversionDepth << ")\n";

                // 各引数変換の前後にチェック
                if (i >= 100) {
                    std::cerr << "[MIR2LLVM]         ERROR: Too many arguments being converted!\n";
                    throw std::runtime_error("Too many arguments in Call terminator");
                }

                args.push_back(convertOperand(*callData.args[i]));
                std::cerr << "[MIR2LLVM]         Arg " << i << " converted successfully\n";
            }
            argConversionDepth--;

            std::cerr << "[MIR2LLVM]         All args converted, total: " << args.size() << "\n";
            std::cerr << "[MIR2LLVM]         Checking interface dispatch: is_virtual="
                      << callData.is_virtual << ", interface_name='"
                      << callData.interface_name << "', args.empty()="
                      << args.empty() << "\n";

            // インターフェースメソッド呼び出しの場合（動的ディスパッチ）
            if (callData.is_virtual && !callData.interface_name.empty() && !args.empty()) {
                std::cerr << "[MIR2LLVM]         ENTERING interface dispatch block\n";
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
                                        // LLVM 14+: opaque pointersではBitCast不要
                                        // funcPtrPtrはすでにptr型なのでそのまま使用
                                        auto funcPtr = builder->CreateLoad(ctx.getPtrType(),
                                                                           funcPtrPtr, "func_ptr");

                                        std::vector<llvm::Type*> paramTypes = {ctx.getPtrType()};
                                        auto funcType = llvm::FunctionType::get(ctx.getVoidType(),
                                                                                paramTypes, false);
                                        // LLVM 14+: opaque pointersではBitCast不要
                                        // funcPtrはすでにptr型なのでそのまま使用

                                        std::vector<llvm::Value*> callArgs = {dataPtr};
                                        builder->CreateCall(funcType, funcPtr, callArgs);
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
                std::cerr << "[MIR2LLVM]         EXITING interface dispatch block\n";
            }
            std::cerr << "[MIR2LLVM]         After interface dispatch check\n";

            // 間接呼び出しの場合は直接呼び出しの処理をスキップ
            llvm::Function* callee = nullptr;
            if (!isIndirectCall && !funcName.empty()) {
                std::cerr << "[MIR2LLVM]           Before generateCallFunctionId: " << funcName << "\n";
                // オーバーロード対応：引数の型から関数IDを生成
                auto funcId = generateCallFunctionId(funcName, callData.args);
                std::cerr << "[MIR2LLVM]           After generateCallFunctionId: funcId=" << funcId << "\n";
                callee = functions[funcId];
                if (!callee) {
                    std::cerr << "[MIR2LLVM]           Function not found, declaring external: " << funcName << "\n";
                    callee = declareExternalFunction(funcName);
                }
                std::cerr << "[MIR2LLVM]           Callee " << (callee ? "found" : "not found") << "\n";
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
                    auto destLocal = callData.destination->local;
                    llvm::Value* resultToStore = result;

                    // 格納先の型を取得
                    llvm::Type* destType = nullptr;
                    if (currentMIRFunction && destLocal < currentMIRFunction->locals.size()) {
                        auto& local = currentMIRFunction->locals[destLocal];
                        if (local.type) {
                            destType = convertType(local.type);
                        }
                    }

                    // 型変換が必要な場合
                    if (destType && result->getType() != destType) {
                        // bool戻り値（i1）をメモリ格納用（i8）に変換
                        if (result->getType()->isIntegerTy(1) && destType->isIntegerTy(8)) {
                            resultToStore =
                                builder->CreateZExt(result, ctx.getI8Type(), "bool_ext");
                        }
                        // 整数型間の変換
                        else if (result->getType()->isIntegerTy() && destType->isIntegerTy()) {
                            unsigned resultBits = result->getType()->getIntegerBitWidth();
                            unsigned destBits = destType->getIntegerBitWidth();
                            if (resultBits > destBits) {
                                // 縮小変換 (例: i64 -> i32)
                                resultToStore = builder->CreateTrunc(result, destType, "trunc");
                            } else if (resultBits < destBits) {
                                // 拡大変換 (例: i32 -> i64)
                                resultToStore = builder->CreateZExt(result, destType, "zext");
                            }
                        }
                    }

                    locals[destLocal] = resultToStore;
                }
            }
            // 間接呼び出し（関数ポインタ変数経由）
            else if (isIndirectCall && funcPtrValue) {
                // クロージャかどうかをチェック
                bool isClosure = false;
                std::string closureFuncName;
                std::vector<mir::LocalId> capturedLocals;

                if (callData.func->kind == mir::MirOperand::Copy ||
                    callData.func->kind == mir::MirOperand::Move) {
                    auto& place = std::get<mir::MirPlace>(callData.func->data);
                    if (currentMIRFunction && place.local < currentMIRFunction->locals.size()) {
                        const auto& localDecl = currentMIRFunction->locals[place.local];
                        if (localDecl.is_closure && !localDecl.captured_locals.empty()) {
                            isClosure = true;
                            closureFuncName = localDecl.closure_func_name;
                            capturedLocals = localDecl.captured_locals;
                        }
                    }
                }

                if (isClosure && !closureFuncName.empty()) {
                    // クロージャ: 直接関数呼び出しに変換し、キャプチャ引数を追加
                    llvm::Function* closureFunc = functions[closureFuncName];
                    if (!closureFunc) {
                        closureFunc = declareExternalFunction(closureFuncName);
                    }

                    if (closureFunc) {
                        // キャプチャ引数を先頭に追加
                        std::vector<llvm::Value*> closureArgs;
                        for (mir::LocalId capLocal : capturedLocals) {
                            llvm::Value* capVal = locals[capLocal];
                            if (capVal) {
                                // allocaの場合はload
                                if (llvm::isa<llvm::AllocaInst>(capVal)) {
                                    auto allocaInst = llvm::cast<llvm::AllocaInst>(capVal);
                                    capVal = builder->CreateLoad(allocaInst->getAllocatedType(),
                                                                 capVal, "cap_load");
                                }
                                closureArgs.push_back(capVal);
                            }
                        }
                        // 通常の引数を追加
                        for (auto& arg : args) {
                            closureArgs.push_back(arg);
                        }

                        // 関数型を取得
                        auto funcType = closureFunc->getFunctionType();

                        // 引数の型変換
                        for (size_t i = 0; i < closureArgs.size() && i < funcType->getNumParams();
                             ++i) {
                            auto expectedType = funcType->getParamType(i);
                            auto actualType = closureArgs[i]->getType();
                            if (expectedType != actualType) {
                                if (expectedType->isIntegerTy() && actualType->isIntegerTy()) {
                                    unsigned expectedBits = expectedType->getIntegerBitWidth();
                                    unsigned actualBits = actualType->getIntegerBitWidth();
                                    if (expectedBits > actualBits) {
                                        closureArgs[i] = builder->CreateSExt(closureArgs[i],
                                                                             expectedType, "sext");
                                    } else if (expectedBits < actualBits) {
                                        closureArgs[i] = builder->CreateTrunc(
                                            closureArgs[i], expectedType, "trunc");
                                    }
                                }
                            }
                        }

                        auto result = builder->CreateCall(closureFunc, closureArgs);
                        if (callData.destination) {
                            locals[callData.destination->local] = result;
                        }

                        if (callData.success != mir::INVALID_BLOCK) {
                            builder->CreateBr(blocks[callData.success]);
                        }
                        break;
                    }
                }

                // 通常の間接呼び出し
                // 関数ポインタ変数の型情報から関数型を取得
                hir::TypePtr funcPtrType = nullptr;
                if (callData.func->kind == mir::MirOperand::Copy ||
                    callData.func->kind == mir::MirOperand::Move) {
                    auto& place = std::get<mir::MirPlace>(callData.func->data);
                    if (currentMIRFunction && place.local < currentMIRFunction->locals.size()) {
                        funcPtrType = currentMIRFunction->locals[place.local].type;
                    }
                }

                if (funcPtrType && funcPtrType->kind == hir::TypeKind::Pointer &&
                    funcPtrType->element_type &&
                    funcPtrType->element_type->kind == hir::TypeKind::Function) {
                    funcPtrType = funcPtrType->element_type;
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

                    // funcPtrValueが整数型の場合、関数ポインタ型にキャストする
                    llvm::Value* funcPtr = funcPtrValue;
                    if (funcPtrValue->getType()->isIntegerTy()) {
                        // 整数値を関数ポインタ型に変換
                        funcPtr = builder->CreateIntToPtr(funcPtrValue, ctx.getPtrType(),
                                                          "func_ptr_cast");
                    }

                    // 間接呼び出し（void戻り値の場合は名前を付けない）
                    llvm::Value* result = nullptr;
                    if (retType->isVoidTy()) {
                        result = builder->CreateCall(funcType, funcPtr, args);
                    } else {
                        result = builder->CreateCall(funcType, funcPtr, args, "indirect_call");
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

                    // funcPtrValueが整数型の場合、関数ポインタ型にキャストする
                    llvm::Value* funcPtr = funcPtrValue;
                    if (funcPtrValue->getType()->isIntegerTy()) {
                        // 整数値を関数ポインタ型に変換
                        funcPtr = builder->CreateIntToPtr(funcPtrValue, ctx.getPtrType(),
                                                          "func_ptr_cast");
                    }

                    auto result = builder->CreateCall(funcType, funcPtr, args, "indirect_call");
                    if (callData.destination) {
                        locals[callData.destination->local] = result;
                    }
                }
            }

            if (callData.success != mir::INVALID_BLOCK) {
                std::cerr << "[MIR2LLVM]         About to CreateBr to block " << callData.success << "\n";

                // blocksマップにsuccessブロックが存在するか確認
                auto blockIt = blocks.find(callData.success);
                if (blockIt == blocks.end()) {
                    std::cerr << "[MIR2LLVM]         ERROR: Success block " << callData.success
                              << " not found in blocks map!\n";
                    std::cerr << "[MIR2LLVM]         Available blocks: ";
                    for (const auto& [id, _] : blocks) {
                        std::cerr << id << " ";
                    }
                    std::cerr << "\n";
                    throw std::runtime_error("Success block not found in blocks map");
                }

                if (!blockIt->second) {
                    std::cerr << "[MIR2LLVM]         ERROR: Success block " << callData.success
                              << " is nullptr!\n";
                    throw std::runtime_error("Success block is nullptr");
                }

                builder->CreateBr(blockIt->second);
                std::cerr << "[MIR2LLVM]         CreateBr completed successfully\n";
            }
            break;
        }
    }
}

}  // namespace cm::codegen::llvm_backend
