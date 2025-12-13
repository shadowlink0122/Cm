/// @file mir_to_llvm.cpp
/// @brief MIR → LLVM IR 変換器（メインモジュール）

#include "mir_to_llvm.hpp"

#include "../../common/debug/codegen.hpp"

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
                auto llvmType = convertType(local.type);
                // 構造体はポインタとして渡す（構造体型のポインタ）
                if (local.type->kind == hir::TypeKind::Struct) {
                    paramTypes.push_back(llvm::PointerType::get(llvmType, 0));
                } else {
                    paramTypes.push_back(llvmType);
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

    // ヘルパー関数は不要（ランタイムライブラリを使用）

    // 構造体型を先に定義
    for (const auto& structPtr : program.structs) {
        const auto& structDef = *structPtr;
        const auto& name = structDef.name;

        // 構造体定義を保存
        structDefs[name] = &structDef;

        // LLVM構造体型を作成
        std::vector<llvm::Type*> fieldTypes;
        for (const auto& field : structDef.fields) {
            fieldTypes.push_back(convertType(field.type));
        }

        // 構造体型を作成（名前付き）
        auto structType = llvm::StructType::create(ctx.getContext(), fieldTypes, name);
        structTypes[name] = structType;
    }

    // 関数宣言（先に全て宣言）
    for (const auto& func : program.functions) {
        auto llvmFunc = convertFunctionSignature(*func);
        functions[func->name] = llvmFunc;
    }

    // 関数実装
    for (const auto& func : program.functions) {
        convertFunction(*func);
    }

    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMConvertEnd,
                            "MIR to LLVM conversion complete");
}

// 関数変換
void MIRToLLVM::convertFunction(const mir::MirFunction& func) {
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMFunction, func.name,
                            cm::debug::Level::Debug);

    currentFunction = functions[func.name];
    currentMIRFunction = &func;
    locals.clear();
    blocks.clear();

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
                auto alloca =
                    builder->CreateAlloca(llvmType, nullptr, "local_" + std::to_string(i));
                locals[i] = alloca;
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
                // Placeに値を格納
                auto addr = convertPlaceToAddress(assign.place);
                if (addr) {
                    // allocaの場合、その割り当て型を確認して適切な型変換を行う
                    if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(addr)) {
                        auto targetType = alloca->getAllocatedType();
                        auto sourceType = rvalue->getType();

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
                    }
                    builder->CreateStore(rvalue, addr);
                } else {
                    // 直接ローカル変数に格納（SSA形式）
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

                    // 最後のプロジェクションからフィールド型を取得
                    const auto& lastProj = place.projections.back();
                    if (lastProj.kind == mir::ProjectionKind::Field) {
                        // ローカル変数の型情報から構造体定義を取得
                        if (currentMIRFunction && place.local < currentMIRFunction->locals.size()) {
                            auto& local = currentMIRFunction->locals[place.local];
                            if (local.type && local.type->kind == hir::TypeKind::Struct) {
                                auto structIt = structDefs.find(local.type->name);
                                if (structIt != structDefs.end()) {
                                    auto& fields = structIt->second->fields;
                                    if (lastProj.field_id < fields.size()) {
                                        fieldType = convertType(fields[lastProj.field_id].type);
                                    }
                                }
                            }
                        }
                    }

                    if (!fieldType) {
                        // フォールバック: i32として扱う
                        fieldType = ctx.getI32Type();
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
            return val;
        }
        case mir::MirOperand::Constant: {
            auto& constant = std::get<mir::MirConstant>(operand.data);
            return convertConstant(constant);
        }
        default:
            return nullptr;
    }
}

// Place変換（アドレス取得）
llvm::Value* MIRToLLVM::convertPlaceToAddress(const mir::MirPlace& place) {
    auto addr = locals[place.local];

    // 投影処理
    for (const auto& proj : place.projections) {
        switch (proj.kind) {
            case mir::ProjectionKind::Field: {
                // 構造体フィールドアクセス
                if (!addr) {
                    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMError,
                                            "Field projection on null address",
                                            cm::debug::Level::Error);
                    return nullptr;
                }

                // GEP (GetElementPtr) を使用してフィールドのアドレスを取得
                // 現在のローカル変数の型を取得
                llvm::Type* structType = nullptr;
                if (currentMIRFunction && place.local < currentMIRFunction->locals.size()) {
                    auto& local = currentMIRFunction->locals[place.local];
                    if (local.type && local.type->kind == hir::TypeKind::Struct) {
                        auto it = structTypes.find(local.type->name);
                        if (it != structTypes.end()) {
                            structType = it->second;
                        }
                    }
                }

                if (!structType) {
                    // 型情報が取得できない場合は、addrの型から推測
                    if (auto allocaInst = llvm::dyn_cast<llvm::AllocaInst>(addr)) {
                        structType = allocaInst->getAllocatedType();
                    } else if (addr->getType()->isPointerTy()) {
                        // ポインタ型の場合（関数引数として渡された構造体など）
                        // MIR関数情報から構造体型を取得
                        if (currentMIRFunction && place.local < currentMIRFunction->locals.size()) {
                            auto& local = currentMIRFunction->locals[place.local];
                            if (local.type && local.type->kind == hir::TypeKind::Struct) {
                                auto it = structTypes.find(local.type->name);
                                if (it != structTypes.end()) {
                                    structType = it->second;
                                }
                            }
                        }
                    }
                }

                if (!structType) {
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
                break;
            }
            case mir::ProjectionKind::Index: {
                // 配列インデックス（未実装）
                break;
            }
            case mir::ProjectionKind::Deref: {
                // デリファレンス
                addr = builder->CreateLoad(ctx.getPtrType(), addr);
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

}  // namespace cm::codegen::llvm_backend
