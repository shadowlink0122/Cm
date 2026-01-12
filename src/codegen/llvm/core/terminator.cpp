/// @file terminator.cpp
/// @brief ターミネータ変換処理
/// Print/Format処理はprint_codegen.cppに分離

#include "mir_to_llvm.hpp"

#include <iostream>

namespace cm::codegen::llvm_backend {

void MIRToLLVM::convertTerminator(const mir::MirTerminator& term) {
    // // debug_msg("MIR2LLVM", "convertTerminator");
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
            // NOTE: ベアメタル対応 - スタック配列は関数終了時に自動解放

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
            // // debug_msg("MIR2LLVM", "Call terminator processing...");
            auto& callData = std::get<mir::MirTerminator::CallData>(term.data);

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
                // // debug_msg("MIR2LLVM", "Call: Indirect call, converting operand...");
                isIndirectCall = true;
                funcPtrValue = convertOperand(*callData.func);
                // // debug_msg("MIR2LLVM", "Call: Operand converted");

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
                    auto destLocal = callData.destination->local;
                    if (allocatedLocals.count(destLocal) > 0 && locals[destLocal]) {
                        builder->CreateStore(result, locals[destLocal]);
                    } else {
                        locals[destLocal] = result;
                    }
                }

                if (callData.success != mir::INVALID_BLOCK) {
                    builder->CreateBr(blocks[callData.success]);
                }
                break;
            }

            // ============================================================
            // 配列 map/filter 呼び出し
            // ============================================================
            if (funcName.find("__builtin_array_") == 0 &&
                (funcName.find("map") != std::string::npos ||
                 funcName.find("filter") != std::string::npos ||
                 funcName.find("some") != std::string::npos ||
                 funcName.find("every") != std::string::npos ||
                 funcName.find("findIndex") != std::string::npos ||
                 funcName.find("reduce") != std::string::npos ||
                 funcName.find("forEach") != std::string::npos)) {
                std::vector<llvm::Value*> args;
                for (const auto& arg : callData.args) {
                    args.push_back(convertOperand(*arg));
                }

                // 引数の型を適切に変換
                // 最初の引数（配列）をポインタに変換
                if (!args.empty()) {
                    llvm::Value* arrPtr = args[0];

                    // LoadInstの場合、ロード元のポインタを使用
                    if (auto* loadInst = llvm::dyn_cast<llvm::LoadInst>(arrPtr)) {
                        auto ptrOperand = loadInst->getPointerOperand();
                        if (auto* allocaInst = llvm::dyn_cast<llvm::AllocaInst>(ptrOperand)) {
                            auto allocatedType = allocaInst->getAllocatedType();
                            // 配列型のallocaの場合、最初の要素へのポインタを取得
                            if (allocatedType->isArrayTy()) {
                                arrPtr = builder->CreateGEP(
                                    allocatedType, ptrOperand,
                                    {llvm::ConstantInt::get(ctx.getI32Type(), 0),
                                     llvm::ConstantInt::get(ctx.getI32Type(), 0)},
                                    "array_elem_ptr");
                                // i8*にビットキャスト
                                arrPtr =
                                    builder->CreateBitCast(arrPtr, ctx.getPtrType(), "arr_cast");
                            } else if (allocatedType->isPointerTy()) {
                                // ポインタ型のallocaの場合、ロードした値（ポインタ）を使用
                                // arrPtr = loadInst (既にロード済み)
                            }
                        }
                    } else if (arrPtr->getType()->isPointerTy()) {
                        // ポインタ型の場合、配列ポインタかチェック
                        if (auto* allocaInst = llvm::dyn_cast<llvm::AllocaInst>(arrPtr)) {
                            auto allocatedType = allocaInst->getAllocatedType();
                            if (allocatedType->isArrayTy()) {
                                // 配列型のallocaの場合、最初の要素へのポインタを取得
                                arrPtr = builder->CreateGEP(
                                    allocatedType, arrPtr,
                                    {llvm::ConstantInt::get(ctx.getI32Type(), 0),
                                     llvm::ConstantInt::get(ctx.getI32Type(), 0)},
                                    "array_ptr");
                            }
                        }
                        // i8*にキャスト
                        if (arrPtr->getType() != ctx.getPtrType()) {
                            arrPtr = builder->CreateBitCast(arrPtr, ctx.getPtrType(), "arr_cast");
                        }
                    } else if (arrPtr->getType()->isArrayTy()) {
                        // 配列値の場合、allocaに格納してポインタを取得
                        auto arrAlloca =
                            builder->CreateAlloca(arrPtr->getType(), nullptr, "arr_tmp");
                        builder->CreateStore(arrPtr, arrAlloca);
                        // 最初の要素へのポインタを取得
                        arrPtr = builder->CreateGEP(arrPtr->getType(), arrAlloca,
                                                    {llvm::ConstantInt::get(ctx.getI32Type(), 0),
                                                     llvm::ConstantInt::get(ctx.getI32Type(), 0)},
                                                    "array_ptr");
                        arrPtr = builder->CreateBitCast(arrPtr, ctx.getPtrType(), "arr_cast");
                    }
                    args[0] = arrPtr;
                }

                // 2番目の引数（配列長）をi64に変換
                if (args.size() >= 2) {
                    auto lengthArg = args[1];
                    if (lengthArg->getType() != ctx.getI64Type()) {
                        if (lengthArg->getType()->isIntegerTy()) {
                            // 整数型の場合、i64に拡張
                            if (lengthArg->getType()->getIntegerBitWidth() < 64) {
                                lengthArg =
                                    builder->CreateSExt(lengthArg, ctx.getI64Type(), "length_i64");
                            } else {
                                lengthArg =
                                    builder->CreateTrunc(lengthArg, ctx.getI64Type(), "length_i64");
                            }
                            args[1] = lengthArg;
                        }
                    }
                }

                // 関数ポインタ引数（3番目）も適切にキャスト
                if (args.size() >= 3) {
                    auto funcPtr = args[2];
                    if (funcPtr->getType() != ctx.getPtrType()) {
                        funcPtr = builder->CreateBitCast(funcPtr, ctx.getPtrType(), "func_cast");
                        args[2] = funcPtr;
                    }
                }

                // 関数を呼び出す
                auto func = declareExternalFunction(funcName);
                auto result = builder->CreateCall(func, args);

                if (callData.destination) {
                    auto destLocal = callData.destination->local;
                    if (allocatedLocals.count(destLocal) > 0 && locals[destLocal]) {
                        builder->CreateStore(result, locals[destLocal]);
                    } else {
                        locals[destLocal] = result;
                    }
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
                    auto destLocal = callData.destination->local;
                    // bool戻り値（i1）をメモリ格納用（i8）に変換
                    llvm::Value* resultToStore = result;
                    if (result->getType()->isIntegerTy(1)) {
                        resultToStore = builder->CreateZExt(result, ctx.getI8Type(), "bool_ext");
                    }
                    if (allocatedLocals.count(destLocal) > 0 && locals[destLocal]) {
                        builder->CreateStore(resultToStore, locals[destLocal]);
                    } else {
                        locals[destLocal] = resultToStore;
                    }
                }

                if (callData.success != mir::INVALID_BLOCK) {
                    builder->CreateBr(blocks[callData.success]);
                }
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
                                        // funcPtrPtrから関数ポインタをロード
                                        llvm::Value* funcPtr = builder->CreateLoad(
                                            ctx.getPtrType(), funcPtrPtr, "func_ptr");

                                        std::vector<llvm::Type*> paramTypes = {ctx.getPtrType()};
                                        auto funcType = llvm::FunctionType::get(ctx.getVoidType(),
                                                                                paramTypes, false);
#if LLVM_VERSION_MAJOR < 15
                                        // LLVM 14: typed
                                        // pointerが必要なので関数ポインタ型にキャスト
                                        auto funcPtrType = llvm::PointerType::get(funcType, 0);
                                        funcPtr = builder->CreateBitCast(funcPtr, funcPtrType,
                                                                         "func_ptr_cast");
#endif

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
                                            if (expectedType != actualType) {
                                                if (expectedType->isPointerTy() &&
                                                    actualType->isPointerTy()) {
                                                    // ポインタ型同士: BitCast
                                                    args[i] = builder->CreateBitCast(args[i],
                                                                                     expectedType);
                                                } else if (expectedType->isPointerTy() &&
                                                           !actualType->isPointerTy()) {
                                                    // プリミティブ型への借用self:
                                                    // allocaを作成してポインタを渡す 例: int.get()
                                                    // で int値をalloca経由でi8*として渡す
                                                    auto alloca = builder->CreateAlloca(
                                                        actualType, nullptr, "prim_self_tmp");
                                                    builder->CreateStore(args[i], alloca);
                                                    args[i] = alloca;
                                                }
                                            }
                                        }

                                        auto result = builder->CreateCall(implFunc, args);
                                        if (callData.destination) {
                                            auto destLocal = callData.destination->local;
                                            if (allocatedLocals.count(destLocal) > 0 &&
                                                locals[destLocal]) {
                                                builder->CreateStore(result, locals[destLocal]);
                                            } else {
                                                locals[destLocal] = result;
                                            }
                                        }
                                    }

                                    if (callData.success != mir::INVALID_BLOCK) {
                                        builder->CreateBr(blocks[callData.success]);
                                    }
                                    break;
                                }
                            }
                            // プリミティブ型への impl メソッド呼び出し (int.abs() 等)
                            else if (local.type) {
                                auto typeKind = local.type->kind;
                                bool isPrimitive = (typeKind == hir::TypeKind::Int ||
                                                    typeKind == hir::TypeKind::UInt ||
                                                    typeKind == hir::TypeKind::Long ||
                                                    typeKind == hir::TypeKind::ULong ||
                                                    typeKind == hir::TypeKind::Short ||
                                                    typeKind == hir::TypeKind::UShort ||
                                                    typeKind == hir::TypeKind::Float ||
                                                    typeKind == hir::TypeKind::Double ||
                                                    typeKind == hir::TypeKind::Bool ||
                                                    typeKind == hir::TypeKind::Char);

                                if (isPrimitive) {
                                    // プリミティブ型名を取得
                                    std::string primTypeName;
                                    switch (typeKind) {
                                        case hir::TypeKind::Int:
                                            primTypeName = "int";
                                            break;
                                        case hir::TypeKind::UInt:
                                            primTypeName = "uint";
                                            break;
                                        case hir::TypeKind::Long:
                                            primTypeName = "long";
                                            break;
                                        case hir::TypeKind::ULong:
                                            primTypeName = "ulong";
                                            break;
                                        case hir::TypeKind::Short:
                                            primTypeName = "short";
                                            break;
                                        case hir::TypeKind::UShort:
                                            primTypeName = "ushort";
                                            break;
                                        case hir::TypeKind::Float:
                                            primTypeName = "float";
                                            break;
                                        case hir::TypeKind::Double:
                                            primTypeName = "double";
                                            break;
                                        case hir::TypeKind::Bool:
                                            primTypeName = "bool";
                                            break;
                                        case hir::TypeKind::Char:
                                            primTypeName = "char";
                                            break;
                                        default:
                                            primTypeName = "int";
                                            break;
                                    }

                                    // impl関数名を生成 (例: int__abs)
                                    std::string implFuncName =
                                        primTypeName + "__" + callData.method_name;
                                    llvm::Function* implFunc = functions[implFuncName];
                                    if (!implFunc) {
                                        implFunc = declareExternalFunction(implFuncName);
                                    }

                                    if (implFunc) {
                                        auto funcType = implFunc->getFunctionType();
                                        // 引数の型変換
                                        for (size_t i = 0;
                                             i < args.size() && i < funcType->getNumParams(); ++i) {
                                            auto expectedType = funcType->getParamType(i);
                                            auto actualType = args[i]->getType();
                                            if (expectedType != actualType) {
                                                if (expectedType->isPointerTy() &&
                                                    !actualType->isPointerTy()) {
                                                    // プリミティブ値をポインタ化
                                                    auto alloca = builder->CreateAlloca(
                                                        actualType, nullptr, "prim_self_tmp");
                                                    builder->CreateStore(args[i], alloca);
                                                    args[i] = alloca;
                                                } else if (expectedType->isPointerTy() &&
                                                           actualType->isPointerTy()) {
                                                    args[i] = builder->CreateBitCast(args[i],
                                                                                     expectedType);
                                                }
                                            }
                                        }

                                        auto result = builder->CreateCall(implFunc, args);
                                        if (callData.destination) {
                                            auto destLocal = callData.destination->local;
                                            if (allocatedLocals.count(destLocal) > 0 &&
                                                locals[destLocal]) {
                                                builder->CreateStore(result, locals[destLocal]);
                                            } else {
                                                locals[destLocal] = result;
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
                    }
                }
            }

            // 間接呼び出しの場合は直接呼び出しの処理をスキップ
            llvm::Function* callee = nullptr;
            if (!isIndirectCall && !funcName.empty()) {
                // オーバーロード対応：引数の型から関数IDを生成
                auto funcId = generateCallFunctionId(funcName, callData.args);
                callee = functions[funcId];
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
                        // プリミティブ型への借用self: ポインタ化
                        else if (expectedType->isPointerTy() && !actualType->isPointerTy()) {
                            // 配列型の場合、配列の先頭要素へのポインタを取得
                            // （コピーを避けてバッファへのポインタを渡す）
                            if (actualType->isArrayTy()) {
                                // args[i]がLoadInstの場合、元のallocaからGEPを取得
                                if (auto* loadInst = llvm::dyn_cast<llvm::LoadInst>(args[i])) {
                                    auto* sourcePtr = loadInst->getPointerOperand();
                                    // 配列の先頭要素へのポインタを取得
                                    auto* zero = llvm::ConstantInt::get(ctx.getContext(),
                                                                        llvm::APInt(64, 0));
                                    auto* elemPtr = builder->CreateGEP(actualType, sourcePtr,
                                                                       {zero, zero}, "arr_ptr");
                                    args[i] = builder->CreateBitCast(elemPtr, expectedType);
                                } else if (auto* allocaInst =
                                               llvm::dyn_cast<llvm::AllocaInst>(args[i])) {
                                    // allocaの場合、直接GEPを使用
                                    auto* zero = llvm::ConstantInt::get(ctx.getContext(),
                                                                        llvm::APInt(64, 0));
                                    auto* elemPtr = builder->CreateGEP(actualType, allocaInst,
                                                                       {zero, zero}, "arr_ptr");
                                    args[i] = builder->CreateBitCast(elemPtr, expectedType);
                                } else {
                                    // その他の場合は従来通りallocaを使用（フォールバック）
                                    auto alloca =
                                        builder->CreateAlloca(actualType, nullptr, "prim_arg_tmp");
                                    builder->CreateStore(args[i], alloca);
                                    args[i] = builder->CreateBitCast(alloca, expectedType);
                                }
                            } else {
                                // プリミティブ型の場合は従来通り
                                auto alloca =
                                    builder->CreateAlloca(actualType, nullptr, "prim_arg_tmp");
                                builder->CreateStore(args[i], alloca);
                                // ポインタ型をexpectedType（ptr/i8*）に変換
                                args[i] = builder->CreateBitCast(alloca, expectedType);
                            }
                        }
                        // 構造体値渡し: ポインタから値型への変換（小さな構造体のC ABI対応）
                        else if (!expectedType->isPointerTy() && actualType->isPointerTy() &&
                                 (expectedType->isStructTy() || expectedType->isArrayTy())) {
                            // ポインタから構造体値をロード
                            auto ptrToStruct = args[i];
                            // ポインタがAllocaInst（構造体ポインタ）の場合
                            if (auto* allocaInst = llvm::dyn_cast<llvm::AllocaInst>(ptrToStruct)) {
                                auto allocatedType = allocaInst->getAllocatedType();
                                if (allocatedType == expectedType) {
                                    // 直接ロード
                                    args[i] = builder->CreateLoad(expectedType, ptrToStruct,
                                                                  "struct_load");
                                } else {
                                    // 型が異なる場合、ビットキャストしてロード
                                    auto castPtr = builder->CreateBitCast(
                                        ptrToStruct, llvm::PointerType::getUnqual(expectedType),
                                        "struct_ptr_cast");
                                    args[i] =
                                        builder->CreateLoad(expectedType, castPtr, "struct_load");
                                }
                            } else {
                                // その他のポインタ型からロード
                                args[i] =
                                    builder->CreateLoad(expectedType, ptrToStruct, "struct_load");
                            }
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
                    if (allocatedLocals.count(destLocal) > 0 && locals[destLocal]) {
                        if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(locals[destLocal])) {
                            destType = alloca->getAllocatedType();
                        }
                    } else if (currentMIRFunction &&
                               destLocal < currentMIRFunction->locals.size()) {
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
                        // ポインタから構造体への変換（スライスget等）
                        else if (result->getType()->isPointerTy() && destType->isStructTy()) {
                            // ポインタから構造体をロード
                            resultToStore = builder->CreateLoad(destType, result, "struct_load");
                        }
                    }

                    if (allocatedLocals.count(destLocal) > 0 && locals[destLocal]) {
                        builder->CreateStore(resultToStore, locals[destLocal]);
                    } else {
                        locals[destLocal] = resultToStore;
                    }
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
                            auto destLocal = callData.destination->local;
                            if (allocatedLocals.count(destLocal) > 0 && locals[destLocal]) {
                                builder->CreateStore(result, locals[destLocal]);
                            } else {
                                locals[destLocal] = result;
                            }
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

                    // funcPtrValueの型を確認して適切に変換
                    llvm::Value* funcPtr = funcPtrValue;
                    if (funcPtrValue) {
                        // ポインタ型でない場合（整数値など）は変換が必要
                        if (!funcPtrValue->getType()->isPointerTy()) {
                            if (funcPtrValue->getType()->isIntegerTy()) {
                                // 整数値を関数ポインタ型に変換
                                funcPtr = builder->CreateIntToPtr(funcPtrValue, ctx.getPtrType(),
                                                                  "func_ptr_from_int");
                            } else {
                                // その他の型の場合もポインタにキャスト
                                funcPtr = builder->CreateBitCast(funcPtrValue, ctx.getPtrType(),
                                                                 "func_ptr_cast");
                            }
                        }
                        // すでにポインタ型の場合はそのまま使用
                    }

                    // 間接呼び出し（void戻り値の場合は名前を付けない）
                    llvm::Value* result = nullptr;
                    if (retType->isVoidTy()) {
                        result = builder->CreateCall(funcType, funcPtr, args);
                    } else {
                        result = builder->CreateCall(funcType, funcPtr, args, "indirect_call");
                    }
                    if (callData.destination && result && !retType->isVoidTy()) {
                        auto destLocal = callData.destination->local;
                        if (allocatedLocals.count(destLocal) > 0 && locals[destLocal]) {
                            builder->CreateStore(result, locals[destLocal]);
                        } else {
                            locals[destLocal] = result;
                        }
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
                        auto destLocal = callData.destination->local;
                        if (allocatedLocals.count(destLocal) > 0 && locals[destLocal]) {
                            builder->CreateStore(result, locals[destLocal]);
                        } else {
                            locals[destLocal] = result;
                        }
                    }
                }
            }

            if (callData.success != mir::INVALID_BLOCK && blocks.count(callData.success) > 0) {
                builder->CreateBr(blocks[callData.success]);
            } else {
                // success == INVALID_BLOCK または ブロックがDCEで削除された場合
                // ターミネータがないとLLVMがハングするため、適切なターミネータを生成
                if (currentMIRFunction &&
                    currentMIRFunction->return_local < currentMIRFunction->locals.size()) {
                    auto& returnLocal =
                        currentMIRFunction->locals[currentMIRFunction->return_local];
                    if (returnLocal.type && returnLocal.type->kind == hir::TypeKind::Void) {
                        // ベアメタル対応 - スタック配列は自動解放
                        builder->CreateRetVoid();
                    } else if (currentMIRFunction->name == "main") {
                        // ベアメタル対応 - スタック配列は自動解放
                        // main関数はi32 0を返す
                        builder->CreateRet(llvm::ConstantInt::get(ctx.getI32Type(), 0));
                    } else {
                        // 他の関数: ローカル変数から戻り値を取得
                        auto retVal = locals[currentMIRFunction->return_local];
                        if (retVal) {
                            if (llvm::isa<llvm::AllocaInst>(retVal)) {
                                auto allocaInst = llvm::cast<llvm::AllocaInst>(retVal);
                                retVal = builder->CreateLoad(allocaInst->getAllocatedType(), retVal,
                                                             "retval");
                            }
                            // ベアメタル対応 - スタック配列は自動解放
                            builder->CreateRet(retVal);
                        } else {
                            builder->CreateUnreachable();
                        }
                    }
                } else {
                    builder->CreateUnreachable();
                }
            }
            break;
        }
    }
}

}  // namespace cm::codegen::llvm_backend
