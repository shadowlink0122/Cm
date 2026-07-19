/// @file invoke.cpp
/// @brief 通常の関数呼び出し生成（直接呼び出し・クロージャ・関数ポインタ経由の間接呼び出し）

#include "internal/codegen/llvm/core/mir_to_llvm.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace cm::codegen::llvm_backend {

/// 直接/間接呼び出しの生成本体（分離元のswitch脱出用breakはreturnに置換済み）
void MIRToLLVM::generateRegularCall(const mir::MirTerminator::CallData& callData,
                                    const std::string& funcName, bool isIndirectCall,
                                    llvm::Value* funcPtrValue, std::vector<llvm::Value*>& args) {
    // 間接呼び出しの場合は直接呼び出しの処理をスキップ

    llvm::Function* callee = nullptr;
    if (!isIndirectCall && !funcName.empty()) {
        // オーバーロード対応：引数の型から関数IDを生成
        auto funcId = generateCallFunctionId(funcName, callData.args);

        callee = functions[funcId];

        // Bug#45修正: functionsテーブルのcalleeが不正なシグネチャ(void())の場合がある
        // convertFunctionSignatureがMIR内のarg_locals空の関数をvoid()で作成するため。
        // 実引数の数とcalleeの引数数が一致しない場合はcalleeを無効化する。
        // ただしvarargs関数は引数数 >= パラメータ数ならOK
        if (callee) {
            auto calleeType = callee->getFunctionType();
            if (calleeType->isVarArg()) {
                if (args.size() < calleeType->getNumParams()) {
                    callee = nullptr;
                }
            } else if (calleeType->getNumParams() != args.size()) {
                callee = nullptr;
            }
        }

        if (!callee) {
            // Bug#45修正: ベース名の前方一致でfunctionsマップを検索
            for (const auto& [fName, fFunc] : functions) {
                if (fFunc && fName.find(funcName + "_") == 0 &&
                    fFunc->getFunctionType()->getNumParams() == args.size()) {
                    callee = fFunc;
                    break;
                }
            }
        }
        // declareExternalFunctionで事前宣言済みの関数を検索
        // __builtin_/cm_プレフィックスの関数はランタイム関数なので
        // declareExternalFunctionで正しいシグネチャを取得
        if (!callee) {
            if (funcName.find("__builtin_") == 0 || funcName.find("cm_") == 0) {
                callee = declareExternalFunction(funcName);
                // calleeの引数数チェック（varargs考慮）
                if (callee) {
                    auto calleeType = callee->getFunctionType();
                    if (calleeType->isVarArg()) {
                        if (args.size() < calleeType->getNumParams()) {
                            callee = nullptr;
                        }
                    } else if (calleeType->getNumParams() != args.size()) {
                        callee = nullptr;
                    }
                }
            } else {
                // その他の関数はmodule->getFunction()で検索
                auto existingFunc = module->getFunction(funcName);
                if (existingFunc) {
                    auto funcType = existingFunc->getFunctionType();
                    if (funcType->isVarArg()) {
                        if (args.size() >= funcType->getNumParams()) {
                            callee = existingFunc;
                        }
                    } else if (funcType->getNumParams() == args.size()) {
                        callee = existingFunc;
                    }
                }
            }
        }
        if (!callee) {
            // Bug#45修正: import先のexport関数がprogram.functionsに含まれない場合、declareExternalFunctionのvoid()フォールバックに到達する。
            // 実引数のLLVM型とdestinationの戻り値型から正しいFunctionTypeを構築して宣言する。
            std::vector<llvm::Type*> paramTypes;
            for (const auto& arg : args) {
                paramTypes.push_back(arg->getType());
            }

            // 戻り値型: destinationがあればそのlocal型、なければvoid
            llvm::Type* returnType = ctx.getVoidType();
            if (callData.destination) {
                auto destLocal = callData.destination->local;
                if (currentMIRFunction && destLocal < currentMIRFunction->locals.size()) {
                    auto& local = currentMIRFunction->locals[destLocal];
                    if (local.type && local.type->kind != hir::TypeKind::Void) {
                        returnType = convertType(local.type);
                    }
                }
            }

            // MIR関数のis_variadicフラグを参照してvarargs対応
            bool isVarArg = false;
            if (currentProgram) {
                for (const auto& func : currentProgram->functions) {
                    if (func && func->name == funcName) {
                        isVarArg = func->is_variadic;
                        break;
                    }
                }
            }
            // varargs関数の場合、固定パラメータのみをparamTypesに含めるべき（可変長引数はFunctionTypeのパラメータに含めない）
            if (isVarArg && currentProgram) {
                for (const auto& func : currentProgram->functions) {
                    if (func && func->name == funcName) {
                        // MIR関数の固定パラメータ数に合わせてparamTypesを切り詰める
                        size_t fixedParams = func->arg_locals.size();
                        if (fixedParams < paramTypes.size()) {
                            paramTypes.resize(fixedParams);
                        }
                        break;
                    }
                }
            }
            auto funcType = llvm::FunctionType::get(returnType, paramTypes, isVarArg);

            // 既存のvoid()宣言がある場合は削除してから再作成
            if (auto existingFunc = module->getFunction(funcName)) {
                if (existingFunc->getFunctionType() != funcType) {
                    existingFunc->eraseFromParent();
                }
            }

            auto result = module->getOrInsertFunction(funcName, funcType);
            callee = llvm::cast<llvm::Function>(result.getCallee());
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
                        if (currentMIRFunction && place.local < currentMIRFunction->locals.size()) {
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
                        std::string vtableKey = actualTypeName + "_" + expectedInterfaceName;
                        llvm::Value* vtablePtr = nullptr;
                        auto vtableIt = vtableGlobals.find(vtableKey);
                        if (vtableIt != vtableGlobals.end()) {
                            vtablePtr = vtableIt->second;
                        } else {
                            vtablePtr = llvm::Constant::getNullValue(ctx.getPtrType());
                        }

                        // 引数が構造体へのポインタの場合、そのポインタをdata pointerとして使用
                        llvm::Value* dataPtr = args[i];

                        // 構造体値の場合は、その値をヒープにコピーする
                        // これにより、インターフェース呼び出し後もデータが有効になる
                        if (!dataPtr->getType()->isPointerTy()) {
                            // スタック上に永続的なコピーを作成（呼び出し後も有効）
                            auto structType = dataPtr->getType();
                            auto structAlloca =
                                builder->CreateAlloca(structType, nullptr, "interface_data");
                            builder->CreateStore(dataPtr, structAlloca);
                            dataPtr = structAlloca;
                        }

                        auto fatPtrAlloca = builder->CreateAlloca(fatPtrType, nullptr, "fat_ptr");
                        auto dataFieldPtr =
                            builder->CreateStructGEP(fatPtrType, fatPtrAlloca, 0, "data_field");
                        auto dataPtrCast =
                            builder->CreateBitCast(dataPtr, ctx.getPtrType(), "data_ptr_cast");
                        builder->CreateStore(dataPtrCast, dataFieldPtr);

                        auto vtableFieldPtr =
                            builder->CreateStructGEP(fatPtrType, fatPtrAlloca, 1, "vtable_field");
                        auto vtablePtrCast =
                            builder->CreateBitCast(vtablePtr, ctx.getPtrType(), "vtable_ptr_cast");
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
                    // 配列型の場合、配列の先頭要素へのポインタを取得（コピーを避けてバッファへのポインタを渡す）
                    if (actualType->isArrayTy()) {
                        // args[i]がLoadInstの場合、元のallocaからGEPを取得
                        if (auto* loadInst = llvm::dyn_cast<llvm::LoadInst>(args[i])) {
                            auto* sourcePtr = loadInst->getPointerOperand();
                            // 配列の先頭要素へのポインタを取得
                            auto* zero =
                                llvm::ConstantInt::get(ctx.getContext(), llvm::APInt(64, 0));
                            auto* elemPtr =
                                builder->CreateGEP(actualType, sourcePtr, {zero, zero}, "arr_ptr");
                            args[i] = builder->CreateBitCast(elemPtr, expectedType);
                        } else if (auto* allocaInst = llvm::dyn_cast<llvm::AllocaInst>(args[i])) {
                            // allocaの場合、直接GEPを使用
                            auto* zero =
                                llvm::ConstantInt::get(ctx.getContext(), llvm::APInt(64, 0));
                            auto* elemPtr =
                                builder->CreateGEP(actualType, allocaInst, {zero, zero}, "arr_ptr");
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
                        auto alloca = builder->CreateAlloca(actualType, nullptr, "prim_arg_tmp");
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
                            args[i] = builder->CreateLoad(expectedType, ptrToStruct, "struct_load");
                        } else {
                            // 型が異なる場合、ビットキャストしてロード
                            auto castPtr = builder->CreateBitCast(
                                ptrToStruct, llvm::PointerType::getUnqual(expectedType),
                                "struct_ptr_cast");
                            args[i] = builder->CreateLoad(expectedType, castPtr, "struct_load");
                        }
                    } else {
                        // その他のポインタ型からロード
                        args[i] = builder->CreateLoad(expectedType, ptrToStruct, "struct_load");
                    }
                }
                // MIR lowering バグ修正: typedef引数コピーの型不整合
                // 例: MemAddr(=ulong) 引数が Pointer<ULong> として伝搬され
                // load ptr (actualType=ptr) → call @func(i64) (expectedType=i64) の不整合
                // 引数が LoadInst の場合、元の alloca から expectedType で再 load する
                else if (!expectedType->isPointerTy() && actualType->isPointerTy() &&
                         (expectedType->isIntegerTy() || expectedType->isFloatingPointTy())) {
                    if (auto* loadInst = llvm::dyn_cast<llvm::LoadInst>(args[i])) {
                        auto* sourcePtr = loadInst->getPointerOperand();
                        args[i] = builder->CreateLoad(expectedType, sourcePtr, "typedef_reload");
                    } else {
                        // LoadInstでない場合は ptrtoint でフォールバック
                        args[i] = builder->CreatePtrToInt(args[i], expectedType, "ptr_to_int");
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
                // 浮動小数点幅の不一致（doubleリテラル → floatパラメータ等）
                // 従来は変換されず "Call parameter type does not match" のLLVM検証エラーになっていた
                else if (expectedType->isFloatingPointTy() && actualType->isFloatingPointTy()) {
                    if (expectedType->getPrimitiveSizeInBits() <
                        actualType->getPrimitiveSizeInBits()) {
                        args[i] = builder->CreateFPTrunc(args[i], expectedType, "fptrunc_arg");
                    } else {
                        args[i] = builder->CreateFPExt(args[i], expectedType, "fpext_arg");
                    }
                }
                // 整数 → 浮動小数点（float f = half(3) 等のリテラル渡し）
                else if (expectedType->isFloatingPointTy() && actualType->isIntegerTy()) {
                    args[i] = builder->CreateSIToFP(args[i], expectedType, "sitofp_arg");
                }
            }
        }

        // varargs引数の型変換（C ABIのdefault argument promotionに準拠）
        // varargs位置の引数（パラメータ数超過分）はCのintサイズ(i32)に合わせる
        if (funcType->isVarArg()) {
            for (size_t i = funcType->getNumParams(); i < args.size(); ++i) {
                auto argType = args[i]->getType();
                // i64をi32にtrunc（Cの%dはi32を期待）
                if (argType->isIntegerTy(64)) {
                    args[i] = builder->CreateTrunc(args[i], ctx.getI32Type(), "vararg_trunc");
                }
            }
        }

        auto result = builder->CreateCall(callee, args);

        // void関数の返り値をdestinationに格納しようとするとLLVMがクラッシュするため、void型の場合はdestination処理をスキップ
        if (callData.destination && !result->getType()->isVoidTy()) {
            auto destLocal = callData.destination->local;
            llvm::Value* resultToStore = result;

            // 格納先の型を取得
            llvm::Type* destType = nullptr;
            if (allocatedLocals.count(destLocal) > 0 && locals[destLocal]) {
                if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(locals[destLocal])) {
                    destType = alloca->getAllocatedType();
                }
            } else if (currentMIRFunction && destLocal < currentMIRFunction->locals.size()) {
                auto& local = currentMIRFunction->locals[destLocal];
                if (local.type) {
                    destType = convertType(local.type);
                }
            }

            // 型変換が必要な場合
            if (destType && result->getType() != destType) {
                // bool戻り値（i1）をメモリ格納用（i8）に変換
                if (result->getType()->isIntegerTy(1) && destType->isIntegerTy(8)) {
                    resultToStore = builder->CreateZExt(result, ctx.getI8Type(), "bool_ext");
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
                            capVal = builder->CreateLoad(allocaInst->getAllocatedType(), capVal,
                                                         "cap_load");
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
                for (size_t i = 0; i < closureArgs.size() && i < funcType->getNumParams(); ++i) {
                    auto expectedType = funcType->getParamType(i);
                    auto actualType = closureArgs[i]->getType();
                    if (expectedType != actualType) {
                        if (expectedType->isIntegerTy() && actualType->isIntegerTy()) {
                            unsigned expectedBits = expectedType->getIntegerBitWidth();
                            unsigned actualBits = actualType->getIntegerBitWidth();
                            if (expectedBits > actualBits) {
                                closureArgs[i] =
                                    builder->CreateSExt(closureArgs[i], expectedType, "sext");
                            } else if (expectedBits < actualBits) {
                                closureArgs[i] =
                                    builder->CreateTrunc(closureArgs[i], expectedType, "trunc");
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
                return;
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
            llvm::Type* retType = funcPtrType->return_type ? convertType(funcPtrType->return_type)
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
                        funcPtr =
                            builder->CreateBitCast(funcPtrValue, ctx.getPtrType(), "func_ptr_cast");
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
                funcPtr = builder->CreateIntToPtr(funcPtrValue, ctx.getPtrType(), "func_ptr_cast");
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
            auto& returnLocal = currentMIRFunction->locals[currentMIRFunction->return_local];
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
                        retVal =
                            builder->CreateLoad(allocaInst->getAllocatedType(), retVal, "retval");
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
}

}  // namespace cm::codegen::llvm_backend
