/// @file print_codegen.cpp
/// @brief Print/Format関連のコード生成
/// terminator.cppから分離したprint/println/format処理

#include "mir_to_llvm.hpp"

namespace cm::codegen::llvm_backend {

// ============================================================
// Helper: 値を文字列に変換
// ============================================================

llvm::Value* MIRToLLVM::generateValueToString(llvm::Value* value, const hir::TypePtr& hirType) {
    auto valueType = value->getType();

    if (valueType->isPointerTy()) {
        // 既に文字列
        return value;
    }

    if (valueType->isIntegerTy()) {
        auto intType = llvm::cast<llvm::IntegerType>(valueType);
        bool isBoolType = hirType && hirType->kind == hir::TypeKind::Bool;
        bool isCharType = hirType && hirType->kind == hir::TypeKind::Char;
        bool isUnsigned =
            hirType &&
            (hirType->kind == hir::TypeKind::UTiny || hirType->kind == hir::TypeKind::UShort ||
             hirType->kind == hir::TypeKind::UInt || hirType->kind == hir::TypeKind::ULong);

        if (isBoolType) {
            auto boolVal = value;
            if (intType->getBitWidth() != 8) {
                boolVal = builder->CreateTrunc(value, ctx.getI8Type());
            }
            auto formatFunc = module->getOrInsertFunction(
                "cm_format_bool",
                llvm::FunctionType::get(ctx.getPtrType(), {ctx.getI8Type()}, false));
            return builder->CreateCall(formatFunc, {boolVal});
        }

        if (isCharType) {
            auto charVal = value;
            if (intType->getBitWidth() != 8) {
                charVal = builder->CreateTrunc(value, ctx.getI8Type());
            }
            auto formatFunc = module->getOrInsertFunction(
                "cm_format_char",
                llvm::FunctionType::get(ctx.getPtrType(), {ctx.getI8Type()}, false));
            return builder->CreateCall(formatFunc, {charVal});
        }

        // 整数型
        auto intVal = value;
        if (intType->getBitWidth() != 32) {
            if (isUnsigned) {
                intVal = builder->CreateZExt(value, ctx.getI32Type());
            } else {
                intVal = builder->CreateSExt(value, ctx.getI32Type());
            }
        }

        auto formatFunc = module->getOrInsertFunction(
            isUnsigned ? "cm_format_uint" : "cm_format_int",
            llvm::FunctionType::get(ctx.getPtrType(), {ctx.getI32Type()}, false));
        return builder->CreateCall(formatFunc, {intVal});
    }

    if (valueType->isFloatingPointTy()) {
        auto doubleVal = value;
        if (valueType->isFloatTy()) {
            doubleVal = builder->CreateFPExt(value, ctx.getF64Type());
        }
        auto formatFunc = module->getOrInsertFunction(
            "cm_format_double",
            llvm::FunctionType::get(ctx.getPtrType(), {ctx.getF64Type()}, false));
        return builder->CreateCall(formatFunc, {doubleVal});
    }

    // 未対応の型
    return builder->CreateGlobalStringPtr("<?>");
}

// ============================================================
// Helper: フォーマット置換を生成
// ============================================================

llvm::Value* MIRToLLVM::generateFormatReplace(llvm::Value* currentStr, llvm::Value* value,
                                              const hir::TypePtr& hirType) {
    auto valueType = value->getType();

    if (valueType->isPointerTy()) {
        // 文字列型
        auto replaceFunc = module->getOrInsertFunction(
            "cm_format_replace_string",
            llvm::FunctionType::get(ctx.getPtrType(), {ctx.getPtrType(), ctx.getPtrType()}, false));
        return builder->CreateCall(replaceFunc, {currentStr, value});
    }

    if (valueType->isIntegerTy()) {
        auto intType = llvm::cast<llvm::IntegerType>(valueType);
        bool isBoolType = hirType && hirType->kind == hir::TypeKind::Bool;
        bool isCharType = hirType && hirType->kind == hir::TypeKind::Char;
        bool isUnsigned =
            hirType &&
            (hirType->kind == hir::TypeKind::UTiny || hirType->kind == hir::TypeKind::UShort ||
             hirType->kind == hir::TypeKind::UInt || hirType->kind == hir::TypeKind::ULong);

        if (isBoolType) {
            auto boolVal = value;
            if (intType->getBitWidth() != 8) {
                boolVal = builder->CreateTrunc(value, ctx.getI8Type());
            }
            auto formatFunc = module->getOrInsertFunction(
                "cm_format_bool",
                llvm::FunctionType::get(ctx.getPtrType(), {ctx.getI8Type()}, false));
            auto boolStr = builder->CreateCall(formatFunc, {boolVal});
            auto replaceFunc = module->getOrInsertFunction(
                "cm_format_replace",
                llvm::FunctionType::get(ctx.getPtrType(), {ctx.getPtrType(), ctx.getPtrType()},
                                        false));
            return builder->CreateCall(replaceFunc, {currentStr, boolStr});
        }

        if (isCharType) {
            auto charVal = value;
            if (intType->getBitWidth() != 8) {
                charVal = builder->CreateTrunc(value, ctx.getI8Type());
            }
            auto formatFunc = module->getOrInsertFunction(
                "cm_format_char",
                llvm::FunctionType::get(ctx.getPtrType(), {ctx.getI8Type()}, false));
            auto charStr = builder->CreateCall(formatFunc, {charVal});
            auto replaceFunc = module->getOrInsertFunction(
                "cm_format_replace",
                llvm::FunctionType::get(ctx.getPtrType(), {ctx.getPtrType(), ctx.getPtrType()},
                                        false));
            return builder->CreateCall(replaceFunc, {currentStr, charStr});
        }

        // 整数型
        auto intVal = value;
        if (intType->getBitWidth() != 32) {
            if (isUnsigned) {
                intVal = builder->CreateZExt(value, ctx.getI32Type());
            } else {
                intVal = builder->CreateSExt(value, ctx.getI32Type());
            }
        }

        auto replaceFunc = module->getOrInsertFunction(
            isUnsigned ? "cm_format_replace_uint" : "cm_format_replace_int",
            llvm::FunctionType::get(ctx.getPtrType(), {ctx.getPtrType(), ctx.getI32Type()}, false));
        return builder->CreateCall(replaceFunc, {currentStr, intVal});
    }

    if (valueType->isFloatingPointTy()) {
        auto doubleVal = value;
        if (valueType->isFloatTy()) {
            doubleVal = builder->CreateFPExt(value, ctx.getF64Type());
        }
        auto replaceFunc = module->getOrInsertFunction(
            "cm_format_replace_double",
            llvm::FunctionType::get(ctx.getPtrType(), {ctx.getPtrType(), ctx.getF64Type()}, false));
        return builder->CreateCall(replaceFunc, {currentStr, doubleVal});
    }

    // 未対応の型はそのまま返す
    return currentStr;
}

// ============================================================
// cm_println_format / cm_print_format の処理
// ============================================================

void MIRToLLVM::generatePrintFormatCall(const mir::MirTerminator::CallData& callData,
                                        bool isNewline) {
    if (callData.args.size() < 2)
        return;

    // MIR形式: [format_string, arg_count, arg1, arg2, ...]
    auto formatStr = convertOperand(*callData.args[0]);
    llvm::Value* currentStr = formatStr;

    // {{と}}のエスケープ処理
    auto unescapeFunc = module->getOrInsertFunction(
        "cm_format_unescape_braces",
        llvm::FunctionType::get(ctx.getPtrType(), {ctx.getPtrType()}, false));
    currentStr = builder->CreateCall(unescapeFunc, {currentStr});

    // 引数インデックス2以降が実際の値
    for (size_t i = 2; i < callData.args.size(); ++i) {
        auto value = convertOperand(*callData.args[i]);
        auto hirType = getOperandType(*callData.args[i]);
        currentStr = generateFormatReplace(currentStr, value, hirType);
    }

    // 結果を出力
    auto printFunc = module->getOrInsertFunction(
        isNewline ? "cm_println_string" : "cm_print_string",
        llvm::FunctionType::get(ctx.getVoidType(), {ctx.getPtrType()}, false));
    builder->CreateCall(printFunc, {currentStr});
}

// ============================================================
// cm_format_string の処理
// ============================================================

void MIRToLLVM::generateFormatStringCall(const mir::MirTerminator::CallData& callData) {
    if (callData.args.size() < 2)
        return;

    // MIR形式: [format_string, arg_count, arg1, arg2, ...]
    auto formatStr = convertOperand(*callData.args[0]);
    llvm::Value* currentStr = formatStr;

    // {{と}}のエスケープ処理
    auto unescapeFunc = module->getOrInsertFunction(
        "cm_format_unescape_braces",
        llvm::FunctionType::get(ctx.getPtrType(), {ctx.getPtrType()}, false));
    currentStr = builder->CreateCall(unescapeFunc, {currentStr});

    // 引数インデックス2以降が実際の値
    for (size_t i = 2; i < callData.args.size(); ++i) {
        auto value = convertOperand(*callData.args[i]);
        auto hirType = getOperandType(*callData.args[i]);
        currentStr = generateFormatReplace(currentStr, value, hirType);
    }

    // 結果をローカル変数に格納
    if (callData.destination.has_value()) {
        auto destPlace = callData.destination.value();
        auto destLocal = locals[destPlace.local];
        if (destLocal) {
            builder->CreateStore(currentStr, destLocal);
        }
    }
}

// ============================================================
// print/println の処理
// ============================================================

void MIRToLLVM::generatePrintCall(const mir::MirTerminator::CallData& callData, bool isNewline) {
    if (callData.args.empty()) {
        // 引数なしの場合：改行のみ出力
        if (isNewline) {
            auto printFunc = module->getOrInsertFunction(
                "cm_println_string",
                llvm::FunctionType::get(ctx.getVoidType(), {ctx.getPtrType()}, false));
            auto emptyStr = builder->CreateGlobalStringPtr("", "empty_str");
            builder->CreateCall(printFunc, {emptyStr});
        }
        return;
    }

    if (callData.args.size() >= 2) {
        // 複数引数の場合
        auto firstArg = convertOperand(*callData.args[0]);
        auto firstType = firstArg->getType();

        if (firstType->isPointerTy()) {
            // 最初の引数が文字列の場合：フォーマット文字列として処理
            llvm::Value* formattedStr = nullptr;

            // フォーマット指定子（{:}）があるかチェック
            bool hasFormatSpecifiers = false;
            if (auto globalStr =
                    llvm::dyn_cast<llvm::GlobalVariable>(firstArg->stripPointerCasts())) {
                if (globalStr->hasInitializer()) {
                    if (auto constData =
                            llvm::dyn_cast<llvm::ConstantDataArray>(globalStr->getInitializer())) {
                        std::string str = constData->getAsString().str();
                        hasFormatSpecifiers = str.find("{:") != std::string::npos;
                    }
                }
            }

            // WASM用の特別処理（フォーマット指定子がない場合のみ）
            if (ctx.getTargetConfig().target == BuildTarget::Wasm && callData.args.size() >= 3 &&
                callData.args.size() <= 6 && !hasFormatSpecifiers) {
                std::vector<llvm::Value*> stringArgs;
                stringArgs.push_back(firstArg);

                for (size_t i = 2; i < callData.args.size(); ++i) {
                    auto value = convertOperand(*callData.args[i]);
                    auto hirType = getOperandType(*callData.args[i]);
                    auto strValue = generateValueToString(value, hirType);
                    if (strValue) {
                        stringArgs.push_back(strValue);
                    }
                }

                // 引数の数に応じて適切な関数を呼び出す
                const char* funcNames[] = {nullptr, "cm_format_string_1", "cm_format_string_2",
                                           "cm_format_string_3", "cm_format_string_4"};
                size_t numArgs = stringArgs.size() - 1;  // フォーマット文字列を除く

                if (numArgs >= 1 && numArgs <= 4) {
                    std::vector<llvm::Type*> paramTypes(stringArgs.size(), ctx.getPtrType());
                    auto formatFunc = module->getOrInsertFunction(
                        funcNames[numArgs],
                        llvm::FunctionType::get(ctx.getPtrType(), paramTypes, false));
                    formattedStr = builder->CreateCall(formatFunc, stringArgs);
                }
            }

            // WASMで処理されなかった場合は従来の処理
            if (!formattedStr) {
                llvm::Value* currentStr = firstArg;

                // MIR形式: [format_string, arg_count, arg1, arg2, ...]
                size_t startIdx = 2;
                if (callData.args.size() == 2) {
                    startIdx = 1;  // 旧形式
                }

                for (size_t i = startIdx; i < callData.args.size(); ++i) {
                    auto value = convertOperand(*callData.args[i]);
                    auto hirType = getOperandType(*callData.args[i]);
                    currentStr = generateFormatReplace(currentStr, value, hirType);
                }

                formattedStr = currentStr;
            }

            // 出力
            auto printFunc = module->getOrInsertFunction(
                isNewline ? "cm_println_string" : "cm_print_string",
                llvm::FunctionType::get(ctx.getVoidType(), {ctx.getPtrType()}, false));
            builder->CreateCall(printFunc, {formattedStr});
        } else {
            // 最初の引数が文字列でない場合：全ての引数を連結
            llvm::Value* resultStr = builder->CreateGlobalStringPtr("", "concat_str");

            for (size_t i = 0; i < callData.args.size(); ++i) {
                auto value = convertOperand(*callData.args[i]);
                auto hirType = getOperandType(*callData.args[i]);
                auto valueStr = generateValueToString(value, hirType);

                auto concatFunc = module->getOrInsertFunction(
                    "cm_string_concat",
                    llvm::FunctionType::get(ctx.getPtrType(), {ctx.getPtrType(), ctx.getPtrType()},
                                            false));
                resultStr = builder->CreateCall(concatFunc, {resultStr, valueStr});
            }

            auto printFunc = module->getOrInsertFunction(
                isNewline ? "cm_println_string" : "cm_print_string",
                llvm::FunctionType::get(ctx.getVoidType(), {ctx.getPtrType()}, false));
            builder->CreateCall(printFunc, {resultStr});
        }
    } else {
        // 単一引数の場合
        auto arg = convertOperand(*callData.args[0]);
        auto argType = arg->getType();
        auto hirType = getOperandType(*callData.args[0]);

        if (argType->isPointerTy()) {
            auto printFunc = module->getOrInsertFunction(
                isNewline ? "cm_println_string" : "cm_print_string",
                llvm::FunctionType::get(ctx.getVoidType(), {ctx.getPtrType()}, false));
            builder->CreateCall(printFunc, {arg});
        } else if (argType->isIntegerTy()) {
            auto intType = llvm::cast<llvm::IntegerType>(argType);
            bool isBoolType = hirType && hirType->kind == hir::TypeKind::Bool;
            bool isCharType = hirType && hirType->kind == hir::TypeKind::Char;
            bool isUnsigned =
                hirType &&
                (hirType->kind == hir::TypeKind::UTiny || hirType->kind == hir::TypeKind::UShort ||
                 hirType->kind == hir::TypeKind::UInt || hirType->kind == hir::TypeKind::ULong);

            if (isBoolType) {
                auto boolArg = arg;
                if (intType->getBitWidth() != 8) {
                    boolArg = builder->CreateTrunc(arg, ctx.getI8Type());
                }
                auto printBoolFunc = module->getOrInsertFunction(
                    isNewline ? "cm_println_bool" : "cm_print_bool",
                    llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI8Type()}, false));
                builder->CreateCall(printBoolFunc, {boolArg});
            } else if (isCharType) {
                auto charArg = arg;
                if (intType->getBitWidth() != 8) {
                    charArg = builder->CreateTrunc(arg, ctx.getI8Type());
                }
                auto printCharFunc = module->getOrInsertFunction(
                    isNewline ? "cm_println_char" : "cm_print_char",
                    llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI8Type()}, false));
                builder->CreateCall(printCharFunc, {charArg});
            } else {
                llvm::FunctionCallee printFunc;
                if (isUnsigned) {
                    printFunc = module->getOrInsertFunction(
                        isNewline ? "cm_println_uint" : "cm_print_uint",
                        llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI32Type()}, false));
                } else {
                    printFunc = module->getOrInsertFunction(
                        isNewline ? "cm_println_int" : "cm_print_int",
                        llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI32Type()}, false));
                }

                auto intArg = arg;
                if (intType->getBitWidth() < 32) {
                    intArg = isUnsigned ? builder->CreateZExt(arg, ctx.getI32Type())
                                        : builder->CreateSExt(arg, ctx.getI32Type());
                } else if (intType->getBitWidth() > 32) {
                    intArg = builder->CreateTrunc(arg, ctx.getI32Type());
                }
                builder->CreateCall(printFunc, {intArg});
            }
        } else if (argType->isFloatingPointTy()) {
            auto printFunc = module->getOrInsertFunction(
                isNewline ? "cm_println_double" : "cm_print_double",
                llvm::FunctionType::get(ctx.getVoidType(), {ctx.getF64Type()}, false));

            auto doubleArg = arg;
            if (argType->isFloatTy()) {
                doubleArg = builder->CreateFPExt(arg, ctx.getF64Type());
            }
            builder->CreateCall(printFunc, {doubleArg});
        }
    }
}

}  // namespace cm::codegen::llvm_backend
