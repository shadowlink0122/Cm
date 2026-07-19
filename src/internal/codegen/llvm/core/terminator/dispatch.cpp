/// @file dispatch.cpp
/// @brief メソッド呼び出しディスパッチ（インターフェース動的/静的ディスパッチとプリミティブimpl呼び出し）

#include "internal/codegen/llvm/core/mir_to_llvm.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace cm::codegen::llvm_backend {

/// メソッド呼び出しとして処理した場合はtrueを返す（分離元のswitch脱出用breakはreturn trueに置換済み）
bool MIRToLLVM::generateMethodCallDispatch(const mir::MirTerminator::CallData& callData,
                                           std::vector<llvm::Value*>& args) {
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
                                dataPtr =
                                    builder->CreateLoad(ctx.getPtrType(), dataFieldPtr, "data_ptr");
                                auto vtableFieldPtr = builder->CreateStructGEP(
                                    fatPtrType, receiverValue, 1, "vtable_field_ptr");
                                vtablePtr = builder->CreateLoad(ctx.getPtrType(), vtableFieldPtr,
                                                                "vtable_ptr");
                            } else {
                                // 値として渡された場合（正しい方法）
                                dataPtr = builder->CreateExtractValue(receiverValue, 0, "data_ptr");
                                vtablePtr =
                                    builder->CreateExtractValue(receiverValue, 1, "vtable_ptr");
                            }

                            // インターフェース宣言からメソッド位置とシグネチャを取得
                            int methodIndex = -1;
                            const mir::MirInterfaceMethod* ifaceMethod = nullptr;
                            if (currentProgram) {
                                for (const auto& iface : currentProgram->interfaces) {
                                    if (iface && iface->name == actualTypeName) {
                                        for (size_t i = 0; i < iface->methods.size(); ++i) {
                                            if (iface->methods[i].name == callData.method_name) {
                                                methodIndex = static_cast<int>(i);
                                                ifaceMethod = &iface->methods[i];
                                                break;
                                            }
                                        }
                                        break;
                                    }
                                }
                            }

                            if (methodIndex >= 0) {
                                auto ptrSize = module->getDataLayout().getPointerSize();
                                auto byteOffset =
                                    llvm::ConstantInt::get(ctx.getI64Type(), methodIndex * ptrSize);
                                auto funcPtrPtr = builder->CreateGEP(ctx.getI8Type(), vtablePtr,
                                                                     byteOffset, "func_ptr_ptr");
                                // funcPtrPtrから関数ポインタをロード
                                llvm::Value* funcPtr =
                                    builder->CreateLoad(ctx.getPtrType(), funcPtrPtr, "func_ptr");

                                // インターフェース宣言のシグネチャから関数型を構成（旧実装は void(ptr) 固定で戻り値が破棄され、メソッド引数も渡されなかった）
                                llvm::Type* retType = ctx.getVoidType();
                                if (ifaceMethod && ifaceMethod->return_type &&
                                    ifaceMethod->return_type->kind != hir::TypeKind::Void) {
                                    retType = convertType(ifaceMethod->return_type);
                                }
                                std::vector<llvm::Type*> paramTypes = {ctx.getPtrType()};
                                std::vector<llvm::Value*> callArgs = {dataPtr};
                                for (size_t ai = 1; ai < args.size(); ++ai) {
                                    callArgs.push_back(args[ai]);
                                    paramTypes.push_back(args[ai]->getType());
                                }
                                auto funcType = llvm::FunctionType::get(retType, paramTypes, false);
#if LLVM_VERSION_MAJOR < 15
                                // LLVM 14: typed pointerが必要なので関数ポインタ型にキャスト
                                auto funcPtrType = llvm::PointerType::get(funcType, 0);
                                funcPtr =
                                    builder->CreateBitCast(funcPtr, funcPtrType, "func_ptr_cast");
#endif

                                auto callResult = builder->CreateCall(funcType, funcPtr, callArgs);

                                // 戻り値を宛先ローカルへ格納
                                if (!retType->isVoidTy() && callData.destination) {
                                    auto destLocal = callData.destination->local;
                                    if (allocatedLocals.count(destLocal) > 0 && locals[destLocal]) {
                                        builder->CreateStore(callResult, locals[destLocal]);
                                    } else {
                                        locals[destLocal] = callResult;
                                    }
                                }
                            }

                            if (callData.success != mir::INVALID_BLOCK) {
                                builder->CreateBr(blocks[callData.success]);
                            }
                            return true;
                        } else {
                            // 静的ディスパッチ
                            std::string implFuncName = actualTypeName + "__" + callData.method_name;
                            llvm::Function* implFunc = functions[implFuncName];
                            if (!implFunc) {
                                implFunc = declareExternalFunction(implFuncName);
                            }

                            if (implFunc) {
                                auto funcType = implFunc->getFunctionType();
                                for (size_t i = 0; i < args.size() && i < funcType->getNumParams();
                                     ++i) {
                                    auto expectedType = funcType->getParamType(i);
                                    auto actualType = args[i]->getType();
                                    if (expectedType != actualType) {
                                        if (expectedType->isPointerTy() &&
                                            actualType->isPointerTy()) {
                                            // ポインタ型同士: BitCast
                                            args[i] = builder->CreateBitCast(args[i], expectedType);
                                        } else if (expectedType->isPointerTy() &&
                                                   !actualType->isPointerTy()) {
                                            // プリミティブ型への借用self:
                                            // allocaを作成してポインタを渡す 例: int.get()
                                            // で int値をalloca経由でi8*として渡す
                                            auto alloca = builder->CreateAlloca(actualType, nullptr,
                                                                                "prim_self_tmp");
                                            builder->CreateStore(args[i], alloca);
                                            args[i] = alloca;
                                        }
                                    }
                                }

                                auto result = builder->CreateCall(implFunc, args);
                                if (callData.destination) {
                                    auto destLocal = callData.destination->local;
                                    if (allocatedLocals.count(destLocal) > 0 && locals[destLocal]) {
                                        builder->CreateStore(result, locals[destLocal]);
                                    } else {
                                        locals[destLocal] = result;
                                    }
                                }
                            }

                            if (callData.success != mir::INVALID_BLOCK) {
                                builder->CreateBr(blocks[callData.success]);
                            }
                            return true;
                        }
                    }
                    // プリミティブ型への impl メソッド呼び出し (int.abs() 等)
                    else if (local.type) {
                        auto typeKind = local.type->kind;
                        bool isPrimitive =
                            (typeKind == hir::TypeKind::Int || typeKind == hir::TypeKind::UInt ||
                             typeKind == hir::TypeKind::Long || typeKind == hir::TypeKind::ULong ||
                             typeKind == hir::TypeKind::Short ||
                             typeKind == hir::TypeKind::UShort ||
                             typeKind == hir::TypeKind::Float ||
                             typeKind == hir::TypeKind::Double || typeKind == hir::TypeKind::Bool ||
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
                            std::string implFuncName = primTypeName + "__" + callData.method_name;
                            llvm::Function* implFunc = functions[implFuncName];
                            if (!implFunc) {
                                implFunc = declareExternalFunction(implFuncName);
                            }

                            if (implFunc) {
                                auto funcType = implFunc->getFunctionType();
                                // 引数の型変換
                                for (size_t i = 0; i < args.size() && i < funcType->getNumParams();
                                     ++i) {
                                    auto expectedType = funcType->getParamType(i);
                                    auto actualType = args[i]->getType();
                                    if (expectedType != actualType) {
                                        if (expectedType->isPointerTy() &&
                                            !actualType->isPointerTy()) {
                                            // プリミティブ値をポインタ化
                                            auto alloca = builder->CreateAlloca(actualType, nullptr,
                                                                                "prim_self_tmp");
                                            builder->CreateStore(args[i], alloca);
                                            args[i] = alloca;
                                        } else if (expectedType->isPointerTy() &&
                                                   actualType->isPointerTy()) {
                                            args[i] = builder->CreateBitCast(args[i], expectedType);
                                        }
                                    }
                                }

                                auto result = builder->CreateCall(implFunc, args);
                                if (callData.destination) {
                                    auto destLocal = callData.destination->local;
                                    if (allocatedLocals.count(destLocal) > 0 && locals[destLocal]) {
                                        builder->CreateStore(result, locals[destLocal]);
                                    } else {
                                        locals[destLocal] = result;
                                    }
                                }
                            }

                            if (callData.success != mir::INVALID_BLOCK) {
                                builder->CreateBr(blocks[callData.success]);
                            }
                            return true;
                        }
                    }
                }
            }
        }
    }

    // どのディスパッチにも該当しない場合は通常の関数呼び出しとして処理を継続する
    return false;
}

}  // namespace cm::codegen::llvm_backend
