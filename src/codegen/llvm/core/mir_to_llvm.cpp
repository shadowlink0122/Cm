/// @file mir_to_llvm.cpp
/// @brief MIR → LLVM IR 変換器（メインモジュール）

#include "mir_to_llvm.hpp"

#include "../../../common/debug/codegen.hpp"

#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

namespace cm::codegen::llvm_backend {

llvm::Function* MIRToLLVM::convertFunctionSignature(const mir::MirFunction& func) {
    // パラメータ型
    std::vector<llvm::Type*> paramTypes;
    for (const auto& arg_local : func.arg_locals) {
        // 引数の型を適切に変換
        if (arg_local < func.locals.size()) {
            auto& local = func.locals[arg_local];
            if (local.type) {
                // インターフェース型かチェック
                if (isInterfaceType(local.type->name)) {
                    // インターフェース型はfat pointer構造体を値渡し
                    auto fatPtrType = getInterfaceFatPtrType(local.type->name);
                    paramTypes.push_back(fatPtrType);
                } else {
                    auto llvmType = convertType(local.type);
                    // 構造体はポインタとして渡す（構造体型のポインタ）
                    if (local.type->kind == hir::TypeKind::Struct) {
                        paramTypes.push_back(llvm::PointerType::get(llvmType, 0));
                    } else {
                        paramTypes.push_back(llvmType);
                    }
                }
            } else {
                paramTypes.push_back(ctx.getI32Type());  // デフォルト
            }
        } else {
            paramTypes.push_back(ctx.getI32Type());  // デフォルト
        }
    }

    // 戻り値型
    // main関数は常にi32を返す（C標準準拠）
    llvm::Type* returnType;
    if (func.name == "main") {
        returnType = ctx.getI32Type();
    } else {
        returnType = ctx.getVoidType();
        if (func.return_local < func.locals.size()) {
            auto& returnLocal = func.locals[func.return_local];
            if (returnLocal.type && returnLocal.type->kind != hir::TypeKind::Void) {
                returnType = convertType(returnLocal.type);
            }
        }
    }

    // 関数型
    auto funcType = llvm::FunctionType::get(returnType, paramTypes, false);

    // 関数作成
    auto llvmFunc =
        llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, func.name, module);

    // パラメータ名設定
    size_t idx = 0;
    for (auto& arg : llvmFunc->args()) {
        if (idx < func.arg_locals.size()) {
            arg.setName("arg" + std::to_string(idx));
        }
        idx++;
    }

    return llvmFunc;
}

// MIRプログラム全体を変換
void MIRToLLVM::convert(const mir::MirProgram& program) {
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMConvert, "Starting MIR to LLVM conversion");

    currentProgram = &program;

    // インターフェース名を収集
    for (const auto& iface : program.interfaces) {
        if (iface) {
            interfaceNames.insert(iface->name);
        }
    }

    // 構造体型を先に定義（2パスアプローチ）
    // パス1: 全ての構造体をopaque型として作成
    for (const auto& structPtr : program.structs) {
        const auto& structDef = *structPtr;
        const auto& name = structDef.name;

        // 構造体定義を保存
        structDefs[name] = &structDef;

        // LLVM構造体型を作成（まずopaque型として）
        auto structType = llvm::StructType::create(ctx.getContext(), name);
        structTypes[name] = structType;
    }

    // パス2: フィールド型を設定
    for (const auto& structPtr : program.structs) {
        const auto& structDef = *structPtr;
        const auto& name = structDef.name;

        std::vector<llvm::Type*> fieldTypes;
        for (const auto& field : structDef.fields) {
            fieldTypes.push_back(convertType(field.type));
        }

        // 構造体のボディを設定
        auto structType = structTypes[name];
        structType->setBody(fieldTypes);
    }

    // インターフェース型（fat pointer）を定義
    for (const auto& iface : program.interfaces) {
        if (iface) {
            getInterfaceFatPtrType(iface->name);
        }
    }

    // 関数宣言（先に全て宣言）- vtable生成前に必要
    for (const auto& func : program.functions) {
        auto llvmFunc = convertFunctionSignature(*func);
        functions[func->name] = llvmFunc;
    }

    // vtableを生成（関数宣言後に実行）
    generateVTables(program);

    // 関数実装
    for (const auto& func : program.functions) {
        convertFunction(*func);
    }

    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMConvertEnd,
                            "MIR to LLVM conversion complete");
}

// 関数変換
void MIRToLLVM::convertFunction(const mir::MirFunction& func) {
    // 外部関数（extern）は宣言のみで本体を生成しない
    if (func.is_extern) {
        return;
    }

    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMFunction, func.name,
                            cm::debug::Level::Debug);

    currentFunction = functions[func.name];
    currentMIRFunction = &func;
    locals.clear();
    blocks.clear();
    allocatedLocals.clear();

    // エントリーブロック作成
    auto entryBB = llvm::BasicBlock::Create(ctx.getContext(), "entry", currentFunction);
    builder->SetInsertPoint(entryBB);

    // パラメータをローカル変数にマップ
    size_t argIdx = 0;
    for (auto& arg : currentFunction->args()) {
        if (argIdx < func.arg_locals.size()) {
            locals[func.arg_locals[argIdx]] = &arg;
        }
        argIdx++;
    }

    // ローカル変数のアロケーション
    for (size_t i = 0; i < func.locals.size(); ++i) {
        if (std::find(func.arg_locals.begin(), func.arg_locals.end(), i) == func.arg_locals.end() &&
            i != func.return_local) {  // 引数と戻り値以外
            // 引数以外のローカル変数
            auto& local = func.locals[i];
            if (local.type) {
                // void型はアロケーションしない
                if (local.type->kind == hir::TypeKind::Void) {
                    continue;
                }
                // 文字列型の一時変数はアロケーションしない（直接値を使用）
                if (local.type->kind == hir::TypeKind::String && !local.is_user_variable) {
                    continue;
                }
                auto llvmType = convertType(local.type);

                // static変数はグローバル変数として作成
                if (local.is_static) {
                    std::string staticKey = func.name + "_" + local.name;
                    auto it = staticVariables.find(staticKey);
                    if (it == staticVariables.end()) {
                        // 初期値を設定（デフォルトはゼロ初期化）
                        llvm::Constant* initialValue = llvm::Constant::getNullValue(llvmType);
                        auto globalVar = new llvm::GlobalVariable(
                            *module, llvmType, false, llvm::GlobalValue::InternalLinkage,
                            initialValue, staticKey);
                        staticVariables[staticKey] = globalVar;
                        locals[i] = globalVar;
                    } else {
                        locals[i] = it->second;
                    }
                    allocatedLocals.insert(i);  // グローバル変数もallocated扱い
                } else {
                    auto alloca =
                        builder->CreateAlloca(llvmType, nullptr, "local_" + std::to_string(i));
                    locals[i] = alloca;
                    allocatedLocals.insert(i);  // allocaされた変数を記録
                }
            }
        }
    }

    // 戻り値用のアロケーション（必要な場合）
    if (func.return_local < func.locals.size()) {
        auto& returnLocal = func.locals[func.return_local];
        if (returnLocal.type && returnLocal.type->kind != hir::TypeKind::Void) {
            auto llvmType = convertType(returnLocal.type);
            auto alloca = builder->CreateAlloca(llvmType, nullptr, "retval");
            locals[func.return_local] = alloca;
            allocatedLocals.insert(func.return_local);  // allocaされた変数を記録
        }
    }

    // 基本ブロック作成
    for (size_t i = 0; i < func.basic_blocks.size(); ++i) {
        auto bbName = "bb" + std::to_string(i);
        blocks[i] = llvm::BasicBlock::Create(ctx.getContext(), bbName, currentFunction);
    }

    // 最初のブロックへジャンプ
    if (!func.basic_blocks.empty()) {
        builder->CreateBr(blocks[0]);
    }

    // 各ブロックを変換
    for (size_t i = 0; i < func.basic_blocks.size(); ++i) {
        convertBasicBlock(*func.basic_blocks[i]);
    }
}

// 基本ブロック変換
void MIRToLLVM::convertBasicBlock(const mir::BasicBlock& block) {
    builder->SetInsertPoint(blocks[block.id]);

    // ステートメント処理
    for (const auto& stmt : block.statements) {
        convertStatement(*stmt);
    }

    // ターミネータ処理
    if (block.terminator) {
        convertTerminator(*block.terminator);
    }
}

// ステートメント変換
void MIRToLLVM::convertStatement(const mir::MirStatement& stmt) {
    switch (stmt.kind) {
        case mir::MirStatement::Assign: {
            auto& assign = std::get<mir::MirStatement::AssignData>(stmt.data);
            auto rvalue = convertRvalue(*assign.rvalue);
            if (rvalue) {
                // projectionsがある場合（構造体フィールドなど）は常にstoreが必要
                bool hasProjections = !assign.place.projections.empty();

                // allocaされた変数かどうかをチェック
                bool isAllocated = allocatedLocals.count(assign.place.local) > 0;

                if (hasProjections || isAllocated) {
                    // Placeに値を格納
                    auto addr = convertPlaceToAddress(assign.place);

                    if (addr) {
                        // ターゲット型を取得（allocaまたはGEPの場合）
                        llvm::Type* targetType = nullptr;
                        if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(addr)) {
                            targetType = alloca->getAllocatedType();
                        } else if (auto gep = llvm::dyn_cast<llvm::GetElementPtrInst>(addr)) {
                            targetType = gep->getResultElementType();
                        }

                        // Derefプロジェクションがある場合、MIRの型情報から要素型を取得
                        bool hasDeref = false;
                        for (const auto& proj : assign.place.projections) {
                            if (proj.kind == mir::ProjectionKind::Deref) {
                                hasDeref = true;
                                break;
                            }
                        }

                        if (hasDeref && !targetType && currentMIRFunction) {
                            auto& local = currentMIRFunction->locals[assign.place.local];
                            if (local.type && local.type->kind == hir::TypeKind::Pointer &&
                                local.type->element_type) {
                                targetType = convertType(local.type->element_type);
                            }
                        }

                        auto sourceType = rvalue->getType();

                        if (targetType) {
                            // sourceがポインタで、targetが構造体の場合（構造体のコピー）
                            if (sourceType->isPointerTy() && targetType->isStructTy()) {
                                // ポインタからロードして構造体値を取得
                                rvalue = builder->CreateLoad(targetType, rvalue, "struct_load");
                                sourceType = rvalue->getType();
                            }

                            // i1からi8への変換が必要な場合（bool値の格納）
                            if (sourceType->isIntegerTy(1) && targetType->isIntegerTy(8)) {
                                rvalue = builder->CreateZExt(rvalue, ctx.getI8Type(), "bool_ext");
                            }
                            // i1からの拡張は常にゼロ拡張
                            else if (sourceType->isIntegerTy(1) && targetType->isIntegerTy()) {
                                rvalue = builder->CreateZExt(rvalue, targetType, "bool_zext");
                            }
                            // 整数型間の変換
                            else if (sourceType->isIntegerTy() && targetType->isIntegerTy()) {
                                auto sourceBits = sourceType->getIntegerBitWidth();
                                auto targetBits = targetType->getIntegerBitWidth();
                                if (sourceBits > targetBits) {
                                    // 縮小変換 (例: i32 -> i8, i32 -> i16)
                                    rvalue = builder->CreateTrunc(rvalue, targetType, "trunc");
                                } else if (sourceBits < targetBits) {
                                    // 拡大変換 (例: i8 -> i32)
                                    rvalue = builder->CreateSExt(rvalue, targetType, "sext");
                                }
                            }
                            // 浮動小数点型間の変換
                            else if (sourceType->isFloatingPointTy() &&
                                     targetType->isFloatingPointTy()) {
                                if (sourceType->isDoubleTy() && targetType->isFloatTy()) {
                                    // double -> float
                                    rvalue = builder->CreateFPTrunc(rvalue, targetType, "fptrunc");
                                } else if (sourceType->isFloatTy() && targetType->isDoubleTy()) {
                                    // float -> double
                                    rvalue = builder->CreateFPExt(rvalue, targetType, "fpext");
                                }
                            }
                            // ポインタ型の変換（LLVM 14の型付きポインタ対応）
                            else if (sourceType->isPointerTy() && targetType->isPointerTy()) {
                                // 型付きポインタ環境では bitcast が必要
                                if (sourceType != targetType) {
                                    rvalue = builder->CreateBitCast(rvalue, targetType, "ptr_cast");
                                }
                            }
                        }

                        // Deref時: アドレスを適切な型のポインタにbitcast
                        if (hasDeref && targetType) {
                            auto addrPtrType = llvm::PointerType::get(targetType, 0);
                            if (addr->getType() != addrPtrType) {
                                addr = builder->CreateBitCast(addr, addrPtrType, "deref_addr_cast");
                            }
                        }

                        builder->CreateStore(rvalue, addr);
                    }
                } else {
                    // SSA形式：allocaがない変数に直接値を格納
                    locals[assign.place.local] = rvalue;
                }
            }
            break;
        }
        case mir::MirStatement::StorageLive:
        case mir::MirStatement::StorageDead:
        case mir::MirStatement::Nop:
            // これらは無視
            break;
    }
}

llvm::Value* MIRToLLVM::convertRvalue(const mir::MirRvalue& rvalue) {
    switch (rvalue.kind) {
        case mir::MirRvalue::Use: {
            auto& useData = std::get<mir::MirRvalue::UseData>(rvalue.data);
            if (useData.operand) {
                return convertOperand(*useData.operand);
            }
            return nullptr;
        }
        case mir::MirRvalue::BinaryOp: {
            auto& binop = std::get<mir::MirRvalue::BinaryOpData>(rvalue.data);
            auto lhs = convertOperand(*binop.lhs);
            auto rhs = convertOperand(*binop.rhs);
            return convertBinaryOp(binop.op, lhs, rhs);
        }
        case mir::MirRvalue::UnaryOp: {
            auto& unop = std::get<mir::MirRvalue::UnaryOpData>(rvalue.data);
            auto operand = convertOperand(*unop.operand);
            return convertUnaryOp(unop.op, operand);
        }
        case mir::MirRvalue::FormatConvert: {
            auto& fmtData = std::get<mir::MirRvalue::FormatConvertData>(rvalue.data);
            auto value = convertOperand(*fmtData.operand);
            return convertFormatConvert(value, fmtData.format_spec);
        }
        case mir::MirRvalue::Ref: {
            // アドレス取得（&）
            auto& refData = std::get<mir::MirRvalue::RefData>(rvalue.data);
            auto local = refData.place.local;

            if (locals.find(local) == locals.end() || !locals[local]) {
                return nullptr;
            }

            llvm::Value* basePtr = locals[local];

            // プロジェクションがある場合（配列要素など）
            if (!refData.place.projections.empty()) {
                for (const auto& proj : refData.place.projections) {
                    if (proj.kind == mir::ProjectionKind::Index) {
                        // 配列要素へのアドレス
                        auto& localInfo = currentMIRFunction->locals[local];
                        if (localInfo.type && localInfo.type->kind == hir::TypeKind::Array) {
                            auto elemType = convertType(localInfo.type->element_type);
                            auto arraySize = localInfo.type->array_size.value_or(0);
                            auto arrayType = llvm::ArrayType::get(elemType, arraySize);

                            llvm::Value* indexVal = nullptr;
                            if (locals.find(proj.index_local) != locals.end() &&
                                locals[proj.index_local]) {
                                auto& idxLocal = currentMIRFunction->locals[proj.index_local];
                                auto idxType = convertType(idxLocal.type);
                                indexVal = builder->CreateLoad(idxType, locals[proj.index_local]);
                                // i64に拡張
                                if (indexVal->getType()->isIntegerTy() &&
                                    indexVal->getType()->getIntegerBitWidth() < 64) {
                                    indexVal = builder->CreateSExt(indexVal, ctx.getI64Type());
                                }
                            } else {
                                indexVal = llvm::ConstantInt::get(ctx.getI64Type(), 0);
                            }

                            // GEPで配列要素のアドレスを取得
                            basePtr = builder->CreateGEP(
                                arrayType, basePtr,
                                {llvm::ConstantInt::get(ctx.getI64Type(), 0), indexVal},
                                "arr_elem_ptr");
                        }
                    } else if (proj.kind == mir::ProjectionKind::Field) {
                        // 構造体フィールドへのアドレス
                        auto& localInfo = currentMIRFunction->locals[local];
                        hir::TypePtr structType = localInfo.type;

                        // 既にGEPで移動している場合、現在の型を追跡
                        // フィールドアクセスでは元のローカル変数の型から辿る
                        if (structType && structType->kind == hir::TypeKind::Struct) {
                            auto it = structTypes.find(structType->name);
                            if (it != structTypes.end()) {
                                std::vector<llvm::Value*> indices;
                                indices.push_back(llvm::ConstantInt::get(ctx.getI32Type(), 0));
                                indices.push_back(
                                    llvm::ConstantInt::get(ctx.getI32Type(), proj.field_id));
                                basePtr =
                                    builder->CreateGEP(it->second, basePtr, indices, "field_ptr");
                            }
                        }
                    }
                }
            }

            return basePtr;
        }
        case mir::MirRvalue::Cast: {
            // 型変換
            auto& castData = std::get<mir::MirRvalue::CastData>(rvalue.data);
            if (!castData.operand) {
                return nullptr;
            }

            auto value = convertOperand(*castData.operand);
            if (!value) {
                return nullptr;
            }

            auto targetType = convertType(castData.target_type);
            if (!targetType) {
                return value;  // 変換できない場合はそのまま
            }

            auto sourceType = value->getType();

            // 同じ型なら変換不要
            if (sourceType == targetType) {
                return value;
            }

            // float <-> double 変換
            if (sourceType->isFloatTy() && targetType->isDoubleTy()) {
                return builder->CreateFPExt(value, targetType, "fpext");
            }
            if (sourceType->isDoubleTy() && targetType->isFloatTy()) {
                return builder->CreateFPTrunc(value, targetType, "fptrunc");
            }

            // int <-> float/double 変換
            if (sourceType->isIntegerTy() && targetType->isFloatingPointTy()) {
                return builder->CreateSIToFP(value, targetType, "sitofp");
            }
            if (sourceType->isFloatingPointTy() && targetType->isIntegerTy()) {
                return builder->CreateFPToSI(value, targetType, "fptosi");
            }

            // int サイズ変換
            if (sourceType->isIntegerTy() && targetType->isIntegerTy()) {
                auto srcBits = sourceType->getIntegerBitWidth();
                auto dstBits = targetType->getIntegerBitWidth();
                if (srcBits < dstBits) {
                    return builder->CreateSExt(value, targetType, "sext");
                } else if (srcBits > dstBits) {
                    return builder->CreateTrunc(value, targetType, "trunc");
                }
            }

            // ポインタキャスト
            if (sourceType->isPointerTy() && targetType->isPointerTy()) {
                return builder->CreateBitCast(value, targetType, "ptr_cast");
            }

            return value;
        }
        default:
            return nullptr;
    }
}

// フォーマット変換
llvm::Value* MIRToLLVM::convertFormatConvert(llvm::Value* value, const std::string& format_spec) {
    if (!value)
        return nullptr;

    auto valueType = value->getType();
    llvm::FunctionCallee formatFunc;

    // 文字列型はi8*
    auto stringType = ctx.getPtrType();

    // フォーマット指定子に基づいて適切なフォーマット関数を選択
    if (format_spec == "x") {
        // 16進数（小文字）
        formatFunc = module->getOrInsertFunction(
            "cm_format_int_hex", llvm::FunctionType::get(stringType, {ctx.getI64Type()}, false));
        if (valueType->isIntegerTy() && valueType->getIntegerBitWidth() < 64) {
            value = builder->CreateSExt(value, ctx.getI64Type());
        }
        return builder->CreateCall(formatFunc, {value});
    } else if (format_spec == "X") {
        // 16進数（大文字）
        formatFunc = module->getOrInsertFunction(
            "cm_format_int_HEX", llvm::FunctionType::get(stringType, {ctx.getI64Type()}, false));
        if (valueType->isIntegerTy() && valueType->getIntegerBitWidth() < 64) {
            value = builder->CreateSExt(value, ctx.getI64Type());
        }
        return builder->CreateCall(formatFunc, {value});
    } else if (format_spec == "b") {
        // 2進数
        formatFunc = module->getOrInsertFunction(
            "cm_format_int_binary", llvm::FunctionType::get(stringType, {ctx.getI64Type()}, false));
        if (valueType->isIntegerTy() && valueType->getIntegerBitWidth() < 64) {
            value = builder->CreateSExt(value, ctx.getI64Type());
        }
        return builder->CreateCall(formatFunc, {value});
    } else if (format_spec == "o") {
        // 8進数
        formatFunc = module->getOrInsertFunction(
            "cm_format_int_octal", llvm::FunctionType::get(stringType, {ctx.getI64Type()}, false));
        if (valueType->isIntegerTy() && valueType->getIntegerBitWidth() < 64) {
            value = builder->CreateSExt(value, ctx.getI64Type());
        }
        return builder->CreateCall(formatFunc, {value});
    } else if (format_spec.find('.') != std::string::npos) {
        // 浮動小数点の精度指定
        int precision = 2;  // デフォルト
        try {
            precision = std::stoi(format_spec.substr(1));
        } catch (...) {}

        formatFunc = module->getOrInsertFunction(
            "cm_format_double_precision",
            llvm::FunctionType::get(stringType, {ctx.getF64Type(), ctx.getI32Type()}, false));

        if (!valueType->isDoubleTy()) {
            if (valueType->isFloatTy()) {
                value = builder->CreateFPExt(value, ctx.getF64Type());
            } else if (valueType->isIntegerTy()) {
                value = builder->CreateSIToFP(value, ctx.getF64Type());
            }
        }
        auto precisionVal = llvm::ConstantInt::get(ctx.getI32Type(), precision);
        return builder->CreateCall(formatFunc, {value, precisionVal});
    } else {
        // デフォルト：toString相当
        if (valueType->isDoubleTy() || valueType->isFloatTy()) {
            formatFunc = module->getOrInsertFunction(
                "cm_format_double", llvm::FunctionType::get(stringType, {ctx.getF64Type()}, false));
            if (valueType->isFloatTy()) {
                value = builder->CreateFPExt(value, ctx.getF64Type());
            }
            return builder->CreateCall(formatFunc, {value});
        } else if (valueType->isIntegerTy()) {
            formatFunc = module->getOrInsertFunction(
                "cm_format_int", llvm::FunctionType::get(stringType, {ctx.getI32Type()}, false));
            if (valueType->getIntegerBitWidth() > 32) {
                value = builder->CreateTrunc(value, ctx.getI32Type());
            } else if (valueType->getIntegerBitWidth() < 32) {
                value = builder->CreateSExt(value, ctx.getI32Type());
            }
            return builder->CreateCall(formatFunc, {value});
        } else {
            // その他（文字列など）はそのまま返す
            return value;
        }
    }
}

// オペランド変換
llvm::Value* MIRToLLVM::convertOperand(const mir::MirOperand& operand) {
    switch (operand.kind) {
        case mir::MirOperand::Copy:
        case mir::MirOperand::Move: {
            auto& place = std::get<mir::MirPlace>(operand.data);

            // プロジェクションがある場合（フィールドアクセスなど）
            if (!place.projections.empty()) {
                auto addr = convertPlaceToAddress(place);
                if (addr) {
                    // フィールドの型を取得してロード
                    llvm::Type* fieldType = nullptr;

                    // プロジェクションチェーンを辿って最終的なフィールド型を取得
                    hir::TypePtr currentType = nullptr;
                    if (currentMIRFunction && place.local < currentMIRFunction->locals.size()) {
                        currentType = currentMIRFunction->locals[place.local].type;
                    }

                    for (const auto& proj : place.projections) {
                        if (proj.kind == mir::ProjectionKind::Field && currentType) {
                            if (currentType->kind == hir::TypeKind::Struct) {
                                auto structIt = structDefs.find(currentType->name);
                                if (structIt != structDefs.end()) {
                                    auto& fields = structIt->second->fields;
                                    if (proj.field_id < fields.size()) {
                                        currentType = fields[proj.field_id].type;
                                    }
                                }
                            }
                        } else if (proj.kind == mir::ProjectionKind::Index && currentType) {
                            if (currentType->kind == hir::TypeKind::Array &&
                                currentType->element_type) {
                                currentType = currentType->element_type;
                            }
                        } else if (proj.kind == mir::ProjectionKind::Deref && currentType) {
                            if (currentType->kind == hir::TypeKind::Pointer &&
                                currentType->element_type) {
                                currentType = currentType->element_type;
                            }
                        }
                    }

                    // 最終的な型を使用
                    if (currentType) {
                        fieldType = convertType(currentType);
                    }

                    if (!fieldType) {
                        // フォールバック: i32として扱う
                        fieldType = ctx.getI32Type();
                    }

                    // Deref時: アドレスを適切な型のポインタにbitcast
                    const auto& lastProj = place.projections.back();
                    if (lastProj.kind == mir::ProjectionKind::Deref && fieldType) {
                        auto addrPtrType = llvm::PointerType::get(fieldType, 0);
                        if (addr->getType() != addrPtrType) {
                            addr = builder->CreateBitCast(addr, addrPtrType, "deref_load_cast");
                        }
                    }

                    return builder->CreateLoad(fieldType, addr, "field_load");
                }
                return nullptr;
            }

            // 通常のローカル変数
            auto local = place.local;
            auto val = locals[local];
            if (val && llvm::isa<llvm::AllocaInst>(val)) {
                // アロケーションの場合
                auto allocaInst = llvm::cast<llvm::AllocaInst>(val);
                auto allocatedType = allocaInst->getAllocatedType();

                // 構造体型の場合はポインタをそのまま返す（値渡しではなくポインタ渡し）
                if (allocatedType->isStructTy()) {
                    return val;
                }

                // スカラー型の場合はロード
                return builder->CreateLoad(allocatedType, val, "load");
            }
            // static変数（GlobalVariable）の場合もロードが必要
            if (val && llvm::isa<llvm::GlobalVariable>(val)) {
                auto globalVar = llvm::cast<llvm::GlobalVariable>(val);
                auto valueType = globalVar->getValueType();
                return builder->CreateLoad(valueType, val, "static_load");
            }
            return val;
        }
        case mir::MirOperand::Constant: {
            auto& constant = std::get<mir::MirConstant>(operand.data);
            return convertConstant(constant);
        }
        case mir::MirOperand::FunctionRef: {
            // 関数参照: LLVMの関数ポインタを返す
            const std::string& funcName = std::get<std::string>(operand.data);
            auto func = module->getFunction(funcName);
            if (func) {
                return func;
            }
            // 関数が見つからない場合はnull
            cm::debug::codegen::log(cm::debug::codegen::Id::LLVMError,
                                    "Function not found for FunctionRef: " + funcName,
                                    cm::debug::Level::Warn);
            return nullptr;
        }
        default:
            return nullptr;
    }
}

// Place変換（アドレス取得）
llvm::Value* MIRToLLVM::convertPlaceToAddress(const mir::MirPlace& place) {
    auto addr = locals[place.local];

    // 現在の型を追跡（ネストしたフィールドアクセス用）
    hir::TypePtr currentType = nullptr;
    if (currentMIRFunction && place.local < currentMIRFunction->locals.size()) {
        currentType = currentMIRFunction->locals[place.local].type;
    }

    // 投影処理
    for (size_t projIdx = 0; projIdx < place.projections.size(); ++projIdx) {
        const auto& proj = place.projections[projIdx];
        switch (proj.kind) {
            case mir::ProjectionKind::Field: {
                // 構造体フィールドアクセス
                if (!addr) {
                    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMError,
                                            "Field projection on null address",
                                            cm::debug::Level::Error);
                    return nullptr;
                }

                // 現在の型から構造体型を取得
                llvm::Type* structType = nullptr;
                std::string structName;

                if (currentType && currentType->kind == hir::TypeKind::Struct) {
                    structName = currentType->name;
                    auto it = structTypes.find(structName);
                    if (it != structTypes.end()) {
                        structType = it->second;
                    }
                }

                if (!structType) {
                    // 型情報が取得できない場合は、addrの型から推測
                    if (auto allocaInst = llvm::dyn_cast<llvm::AllocaInst>(addr)) {
                        structType = allocaInst->getAllocatedType();
                    } else if (auto loadInst = llvm::dyn_cast<llvm::LoadInst>(addr)) {
                        // LoadInst（デリファレンス後）の場合、ロードされた型から構造体型を取得
                        auto loadedType = loadInst->getType();
                        if (loadedType->isPointerTy()) {
#if LLVM_VERSION_MAJOR >= 15
                            // opaque pointer: currentTypeから取得済み
#else
                            // typed pointer: ポインタの要素型を取得
                            structType = loadedType->getPointerElementType();
#endif
                        }
                    } else if (auto gepInst = llvm::dyn_cast<llvm::GetElementPtrInst>(addr)) {
                        // GEPの結果型から構造体型を取得
                        // opaque pointerモードでは getSourceElementType と indices から推測
                        auto srcElemType = gepInst->getSourceElementType();
                        if (srcElemType && srcElemType->isArrayTy()) {
                            // 配列からのGEPの場合、要素型を取得
                            structType = srcElemType->getArrayElementType();
                        } else if (srcElemType && srcElemType->isStructTy()) {
                            // 構造体からのGEPの場合、最後のインデックスでフィールド型を取得
                            auto structTy = llvm::cast<llvm::StructType>(srcElemType);
                            // フィールド型を取得（最後のインデックスを使用）
                            if (gepInst->getNumIndices() >= 2) {
                                if (auto constIdx = llvm::dyn_cast<llvm::ConstantInt>(
                                        gepInst->getOperand(gepInst->getNumIndices()))) {
                                    auto fieldIdx = constIdx->getZExtValue();
                                    if (fieldIdx < structTy->getNumElements()) {
                                        structType = structTy->getElementType(fieldIdx);
                                    }
                                }
                            }
                        } else {
                            structType = srcElemType;
                        }
                    }
                }

                if (!structType || !structType->isStructTy()) {
                    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMError,
                                            "Cannot determine struct type for field access",
                                            cm::debug::Level::Error);
                    return nullptr;
                }

                std::vector<llvm::Value*> indices;
                indices.push_back(llvm::ConstantInt::get(ctx.getI32Type(), 0));  // 構造体ベース
                indices.push_back(llvm::ConstantInt::get(ctx.getI32Type(),
                                                         proj.field_id));  // フィールドインデックス

                addr = builder->CreateGEP(structType, addr, indices, "field_ptr");

                // 次のプロジェクションのために型を更新
                if (currentType && currentType->kind == hir::TypeKind::Struct &&
                    !structName.empty()) {
                    auto struct_it = structDefs.find(structName);
                    if (struct_it != structDefs.end() &&
                        proj.field_id < struct_it->second->fields.size()) {
                        currentType = struct_it->second->fields[proj.field_id].type;
                    }
                }
                break;
            }
            case mir::ProjectionKind::Index: {
                // 配列インデックスアクセス
                if (!addr) {
                    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMError,
                                            "Index projection on null address",
                                            cm::debug::Level::Error);
                    return nullptr;
                }

                // インデックス値を取得
                llvm::Value* indexVal = nullptr;
                auto idx_it = locals.find(proj.index_local);
                if (idx_it != locals.end()) {
                    indexVal = idx_it->second;
                    // allocaの場合はloadする
                    if (allocatedLocals.count(proj.index_local)) {
                        // 実際の型を取得してloadする
                        llvm::Type* idxType = ctx.getI64Type();  // デフォルト
                        if (currentMIRFunction &&
                            proj.index_local < currentMIRFunction->locals.size()) {
                            auto& idxLocal = currentMIRFunction->locals[proj.index_local];
                            idxType = convertType(idxLocal.type);
                        }
                        indexVal = builder->CreateLoad(idxType, indexVal, "idx_load");
                        // i32の場合はi64に拡張（GEPはi64を期待）
                        if (idxType->isIntegerTy(32)) {
                            indexVal = builder->CreateSExt(indexVal, ctx.getI64Type(), "idx_ext");
                        }
                    }
                }

                if (!indexVal) {
                    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMError,
                                            "Cannot get index value for array access",
                                            cm::debug::Level::Error);
                    return nullptr;
                }

                // 配列の要素型を取得（currentTypeを使用）
                llvm::Type* arrayType = nullptr;
                if (currentType && currentType->kind == hir::TypeKind::Array) {
                    arrayType = convertType(currentType);
                }

                if (!arrayType) {
                    // フォールバック: place.localの型を使用
                    if (currentMIRFunction && place.local < currentMIRFunction->locals.size()) {
                        auto& local = currentMIRFunction->locals[place.local];
                        if (local.type && local.type->kind == hir::TypeKind::Array) {
                            arrayType = convertType(local.type);
                        }
                    }
                }

                if (!arrayType) {
                    // 型情報が取得できない場合は、allocaまたはGEPから推測
                    if (auto allocaInst = llvm::dyn_cast<llvm::AllocaInst>(addr)) {
                        arrayType = allocaInst->getAllocatedType();
                    } else if (auto gepInst = llvm::dyn_cast<llvm::GetElementPtrInst>(addr)) {
                        arrayType = gepInst->getResultElementType();
                    }
                }

                if (!arrayType || !arrayType->isArrayTy()) {
                    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMError,
                                            "Cannot determine array type for index access",
                                            cm::debug::Level::Error);
                    return nullptr;
                }

                // GEPで配列要素のアドレスを取得
                std::vector<llvm::Value*> indices;
                indices.push_back(llvm::ConstantInt::get(ctx.getI64Type(), 0));  // 配列ベース
                indices.push_back(indexVal);  // 要素インデックス

                addr = builder->CreateGEP(arrayType, addr, indices, "elem_ptr");

                // 次のプロジェクションのために型を更新
                if (currentType && currentType->kind == hir::TypeKind::Array &&
                    currentType->element_type) {
                    currentType = currentType->element_type;
                }
                break;
            }
            case mir::ProjectionKind::Deref: {
                // デリファレンス：ポインタ変数から実際のポインタ値をロード
                // LLVM 14では型付きポインタを使用するため、適切な型でロードする必要がある
                llvm::Type* loadType = ctx.getPtrType();  // デフォルトはi8*/ptr

                // 次のプロジェクションがFieldの場合、構造体へのポインタとしてロード
                if (currentType && currentType->kind == hir::TypeKind::Pointer &&
                    currentType->element_type) {
                    auto elemType = currentType->element_type;
                    if (elemType->kind == hir::TypeKind::Struct) {
                        auto it = structTypes.find(elemType->name);
                        if (it != structTypes.end()) {
                            loadType = llvm::PointerType::getUnqual(it->second);
                        }
                    }
                }

                addr = builder->CreateLoad(loadType, addr);

                // 次のプロジェクションのために型を更新
                if (currentType && currentType->kind == hir::TypeKind::Pointer &&
                    currentType->element_type) {
                    currentType = currentType->element_type;
                }
                break;
            }
        }
    }

    // プロジェクションがある場合はGEPの結果をそのまま返す
    if (!place.projections.empty() && addr) {
        return addr;
    }

    // allocaインストラクションの場合はそのまま返す
    if (addr && llvm::isa<llvm::AllocaInst>(addr)) {
        return addr;
    }

    // ポインタ型の場合（関数引数など）はそのまま返す
    if (addr && addr->getType()->isPointerTy()) {
        return addr;
    }

    // それ以外はnullptr（SSA形式で直接値を使う）
    return nullptr;
}

// インターフェース関連の実装は interface.cpp に移動

}  // namespace cm::codegen::llvm_backend
