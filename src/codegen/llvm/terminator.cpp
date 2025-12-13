/// @file llvm_terminator.cpp
/// @brief ターミネータ変換処理（print/println含む）

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

            // デフォルトターゲット
            auto defaultBB = blocks[switchData.otherwise];

            // スイッチ作成
            auto switchInst = builder->CreateSwitch(discr, defaultBB, switchData.targets.size());

            // 各ケース追加
            for (const auto& [value, target] : switchData.targets) {
                // discriminantと同じ型でcaseValを作成
                auto discrType = discr->getType();
                auto caseVal = llvm::ConstantInt::get(discrType, value);
                switchInst->addCase(llvm::cast<llvm::ConstantInt>(caseVal), blocks[target]);
            }
            break;
        }
        case mir::MirTerminator::Return: {
            // main関数の特別処理
            if (currentMIRFunction->name == "main") {
                // main関数は常にi32を返す
                if (currentMIRFunction->return_local < currentMIRFunction->locals.size()) {
                    // 明示的な戻り値がある場合
                    auto retVal = locals[currentMIRFunction->return_local];
                    if (retVal) {
                        // アロケーションの場合はロード
                        if (llvm::isa<llvm::AllocaInst>(retVal)) {
                            auto allocaInst = llvm::cast<llvm::AllocaInst>(retVal);
                            auto allocatedType = allocaInst->getAllocatedType();
                            retVal = builder->CreateLoad(allocatedType, retVal, "retval");
                        }
                        builder->CreateRet(retVal);
                    } else {
                        // void mainの場合、0を返す
                        builder->CreateRet(llvm::ConstantInt::get(ctx.getI32Type(), 0));
                    }
                } else {
                    // void mainの場合、0を返す
                    builder->CreateRet(llvm::ConstantInt::get(ctx.getI32Type(), 0));
                }
            } else {
                // 通常の関数
                // 戻り型をチェック
                auto& returnLocal = currentMIRFunction->locals[currentMIRFunction->return_local];
                bool isVoidReturn =
                    returnLocal.type && returnLocal.type->kind == hir::TypeKind::Void;

                if (isVoidReturn) {
                    // void関数
                    builder->CreateRetVoid();
                } else if (currentMIRFunction->return_local < currentMIRFunction->locals.size()) {
                    // 戻り値がある場合
                    auto retVal = locals[currentMIRFunction->return_local];
                    if (retVal) {
                        // アロケーションの場合はロード
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

            // 関数名を取得（funcオペランドから）
            std::string funcName;
            if (callData.func->kind == mir::MirOperand::Constant) {
                auto& constant = std::get<mir::MirConstant>(callData.func->data);
                if (auto* name = std::get_if<std::string>(&constant.value)) {
                    funcName = *name;
                }
            } else if (callData.func->kind == mir::MirOperand::FunctionRef) {
                funcName = std::get<std::string>(callData.func->data);
            }

            // cm_println_format / cm_print_format の特別処理
            if (funcName == "cm_println_format" || funcName == "cm_print_format") {
                bool isNewline = funcName.find("println") != std::string::npos;

                if (callData.args.size() >= 2) {
                    // MIR形式: [format_string, arg_count, arg1, arg2, ...]
                    auto formatStr = convertOperand(*callData.args[0]);
                    llvm::Value* currentStr = formatStr;

                    // {{と}}のエスケープ処理を行う
                    auto unescapeFunc = module->getOrInsertFunction(
                        "cm_format_unescape_braces",
                        llvm::FunctionType::get(ctx.getPtrType(), {ctx.getPtrType()}, false));
                    currentStr = builder->CreateCall(unescapeFunc, {currentStr});

                    // 引数インデックス2以降が実際の値
                    for (size_t i = 2; i < callData.args.size(); ++i) {
                        auto value = convertOperand(*callData.args[i]);
                        auto valueType = value->getType();
                        auto hirType = getOperandType(*callData.args[i]);

                        if (valueType->isPointerTy()) {
                            // 文字列型
                            auto replaceFunc = module->getOrInsertFunction(
                                "cm_format_replace_string",
                                llvm::FunctionType::get(
                                    ctx.getPtrType(), {ctx.getPtrType(), ctx.getPtrType()}, false));
                            currentStr = builder->CreateCall(replaceFunc, {currentStr, value});
                        } else if (valueType->isIntegerTy()) {
                            auto intType = llvm::cast<llvm::IntegerType>(valueType);
                            bool isBoolType = hirType && hirType->kind == hir::TypeKind::Bool;
                            bool isCharType = hirType && hirType->kind == hir::TypeKind::Char;

                            if (isBoolType) {
                                // bool型
                                auto boolVal = value;
                                if (intType->getBitWidth() != 8) {
                                    boolVal = builder->CreateTrunc(value, ctx.getI8Type());
                                }
                                auto formatFunc = module->getOrInsertFunction(
                                    "cm_format_bool",
                                    llvm::FunctionType::get(ctx.getPtrType(), {ctx.getI8Type()},
                                                            false));
                                auto boolStr = builder->CreateCall(formatFunc, {boolVal});
                                auto replaceFunc = module->getOrInsertFunction(
                                    "cm_format_replace",
                                    llvm::FunctionType::get(ctx.getPtrType(),
                                                            {ctx.getPtrType(), ctx.getPtrType()},
                                                            false));
                                currentStr =
                                    builder->CreateCall(replaceFunc, {currentStr, boolStr});
                            } else if (isCharType) {
                                // char型
                                auto charVal = value;
                                if (intType->getBitWidth() != 8) {
                                    charVal = builder->CreateTrunc(value, ctx.getI8Type());
                                }
                                auto formatFunc = module->getOrInsertFunction(
                                    "cm_format_char",
                                    llvm::FunctionType::get(ctx.getPtrType(), {ctx.getI8Type()},
                                                            false));
                                auto charStr = builder->CreateCall(formatFunc, {charVal});
                                auto replaceFunc = module->getOrInsertFunction(
                                    "cm_format_replace",
                                    llvm::FunctionType::get(ctx.getPtrType(),
                                                            {ctx.getPtrType(), ctx.getPtrType()},
                                                            false));
                                currentStr =
                                    builder->CreateCall(replaceFunc, {currentStr, charStr});
                            } else {
                                // 整数型 - 符号なし/符号付きを区別
                                bool isUnsigned =
                                    hirType && (hirType->kind == hir::TypeKind::UTiny ||
                                                hirType->kind == hir::TypeKind::UShort ||
                                                hirType->kind == hir::TypeKind::UInt ||
                                                hirType->kind == hir::TypeKind::ULong);
                                auto intVal = value;
                                if (intType->getBitWidth() != 32) {
                                    if (isUnsigned) {
                                        intVal = builder->CreateZExt(value, ctx.getI32Type());
                                    } else {
                                        intVal = builder->CreateSExt(value, ctx.getI32Type());
                                    }
                                }
                                if (isUnsigned) {
                                    auto replaceFunc = module->getOrInsertFunction(
                                        "cm_format_replace_uint",
                                        llvm::FunctionType::get(
                                            ctx.getPtrType(), {ctx.getPtrType(), ctx.getI32Type()},
                                            false));
                                    currentStr =
                                        builder->CreateCall(replaceFunc, {currentStr, intVal});
                                } else {
                                    auto replaceFunc = module->getOrInsertFunction(
                                        "cm_format_replace_int",
                                        llvm::FunctionType::get(
                                            ctx.getPtrType(), {ctx.getPtrType(), ctx.getI32Type()},
                                            false));
                                    currentStr =
                                        builder->CreateCall(replaceFunc, {currentStr, intVal});
                                }
                            }
                        } else if (valueType->isFloatingPointTy()) {
                            auto doubleVal = value;
                            if (valueType->isFloatTy()) {
                                doubleVal = builder->CreateFPExt(value, ctx.getF64Type());
                            }
                            auto replaceFunc = module->getOrInsertFunction(
                                "cm_format_replace_double",
                                llvm::FunctionType::get(
                                    ctx.getPtrType(), {ctx.getPtrType(), ctx.getF64Type()}, false));
                            currentStr = builder->CreateCall(replaceFunc, {currentStr, doubleVal});
                        }
                    }

                    // 結果を出力
                    auto printFunc = module->getOrInsertFunction(
                        isNewline ? "cm_println_string" : "cm_print_string",
                        llvm::FunctionType::get(ctx.getVoidType(), {ctx.getPtrType()}, false));
                    builder->CreateCall(printFunc, {currentStr});
                }

                // 次のブロックへ
                builder->CreateBr(blocks[callData.success]);
                break;
            }

            // print/println の特別処理
            if (funcName == "print" || funcName == "println" || funcName == "std::io::print" ||
                funcName == "std::io::println") {
                bool isNewline = funcName.find("println") != std::string::npos;

                if (callData.args.empty()) {
                    // 引数なしの場合：改行のみ出力
                    if (isNewline) {
                        auto printFunc = module->getOrInsertFunction(
                            "cm_println_string",
                            llvm::FunctionType::get(ctx.getVoidType(), {ctx.getPtrType()}, false));
                        auto emptyStr = builder->CreateGlobalStringPtr("", "empty_str");
                        builder->CreateCall(printFunc, {emptyStr});
                    }
                } else if (callData.args.size() >= 2) {
                    // 複数引数の場合
                    auto firstArg = convertOperand(*callData.args[0]);
                    auto firstType = firstArg->getType();

                    if (firstType->isPointerTy()) {
                        // 最初の引数が文字列の場合：フォーマット文字列として処理
                        llvm::Value* formattedStr = nullptr;

                        // フォーマット指定子（{:}）があるかチェック
                        bool hasFormatSpecifiers = false;
                        if (firstArg->getType()->isPointerTy()) {
                            // firstArgがGlobalStringPtrの場合、文字列を取得してチェック
                            if (auto globalStr = llvm::dyn_cast<llvm::GlobalVariable>(
                                    firstArg->stripPointerCasts())) {
                                if (globalStr->hasInitializer()) {
                                    if (auto constData = llvm::dyn_cast<llvm::ConstantDataArray>(
                                            globalStr->getInitializer())) {
                                        std::string str = constData->getAsString().str();
                                        hasFormatSpecifiers = str.find("{:") != std::string::npos;
                                    }
                                }
                            }
                        }

                        // WASM用の特別処理：cm_format_string_1/2/3/4を使用（フォーマット指定子がない場合のみ）
                        if (ctx.getTargetConfig().target == BuildTarget::Wasm &&
                            callData.args.size() >= 3 && callData.args.size() <= 6 &&
                            !hasFormatSpecifiers) {
                            // 引数を文字列に変換
                            // MIR形式: [format_string, arg_count, arg1, arg2, ...]
                            std::vector<llvm::Value*> stringArgs;
                            stringArgs.push_back(firstArg);  // フォーマット文字列

                            // 引数インデックス2以降が実際の値
                            for (size_t i = 2; i < callData.args.size(); ++i) {
                                auto value = convertOperand(*callData.args[i]);
                                auto valueType = value->getType();
                                auto hirType = getOperandType(*callData.args[i]);

                                llvm::Value* strValue = nullptr;

                                if (valueType->isPointerTy()) {
                                    strValue = value;
                                } else if (valueType->isIntegerTy()) {
                                    auto intType = llvm::cast<llvm::IntegerType>(valueType);
                                    bool isBoolType =
                                        hirType && hirType->kind == hir::TypeKind::Bool;
                                    bool isCharType =
                                        hirType && hirType->kind == hir::TypeKind::Char;

                                    if (isBoolType && intType->getBitWidth() == 8) {
                                        auto formatFunc = module->getOrInsertFunction(
                                            "cm_format_bool",
                                            llvm::FunctionType::get(ctx.getPtrType(),
                                                                    {ctx.getI8Type()}, false));
                                        strValue = builder->CreateCall(formatFunc, {value});
                                    } else if (isCharType && intType->getBitWidth() == 8) {
                                        auto formatFunc = module->getOrInsertFunction(
                                            "cm_format_char",
                                            llvm::FunctionType::get(ctx.getPtrType(),
                                                                    {ctx.getI8Type()}, false));
                                        strValue = builder->CreateCall(formatFunc, {value});
                                    } else {
                                        // 整数を文字列に変換
                                        bool isUnsigned =
                                            hirType && (hirType->kind == hir::TypeKind::UTiny ||
                                                        hirType->kind == hir::TypeKind::UShort ||
                                                        hirType->kind == hir::TypeKind::UInt ||
                                                        hirType->kind == hir::TypeKind::ULong);
                                        auto intVal = value;
                                        if (valueType->getIntegerBitWidth() != 32) {
                                            if (isUnsigned) {
                                                intVal =
                                                    builder->CreateZExt(value, ctx.getI32Type());
                                            } else {
                                                intVal =
                                                    builder->CreateSExt(value, ctx.getI32Type());
                                            }
                                        }
                                        auto formatFunc = module->getOrInsertFunction(
                                            isUnsigned ? "cm_format_uint" : "cm_format_int",
                                            llvm::FunctionType::get(ctx.getPtrType(),
                                                                    {ctx.getI32Type()}, false));
                                        strValue = builder->CreateCall(formatFunc, {intVal});
                                    }
                                } else if (valueType->isFloatingPointTy()) {
                                    auto doubleVal = value;
                                    if (valueType->isFloatTy()) {
                                        doubleVal = builder->CreateFPExt(value, ctx.getF64Type());
                                    }
                                    auto formatFunc = module->getOrInsertFunction(
                                        "cm_format_double",
                                        llvm::FunctionType::get(ctx.getPtrType(),
                                                                {ctx.getF64Type()}, false));
                                    strValue = builder->CreateCall(formatFunc, {doubleVal});
                                }

                                if (strValue) {
                                    stringArgs.push_back(strValue);
                                }
                            }

                            // 引数の数に応じて適切な関数を呼び出す
                            if (stringArgs.size() == 2) {
                                auto formatFunc = module->getOrInsertFunction(
                                    "cm_format_string_1",
                                    llvm::FunctionType::get(ctx.getPtrType(),
                                                            {ctx.getPtrType(), ctx.getPtrType()},
                                                            false));
                                formattedStr = builder->CreateCall(formatFunc, stringArgs);
                            } else if (stringArgs.size() == 3) {
                                auto formatFunc = module->getOrInsertFunction(
                                    "cm_format_string_2",
                                    llvm::FunctionType::get(
                                        ctx.getPtrType(),
                                        {ctx.getPtrType(), ctx.getPtrType(), ctx.getPtrType()},
                                        false));
                                formattedStr = builder->CreateCall(formatFunc, stringArgs);
                            } else if (stringArgs.size() == 4) {
                                auto formatFunc = module->getOrInsertFunction(
                                    "cm_format_string_3",
                                    llvm::FunctionType::get(ctx.getPtrType(),
                                                            {ctx.getPtrType(), ctx.getPtrType(),
                                                             ctx.getPtrType(), ctx.getPtrType()},
                                                            false));
                                formattedStr = builder->CreateCall(formatFunc, stringArgs);
                            } else if (stringArgs.size() == 5) {
                                auto formatFunc = module->getOrInsertFunction(
                                    "cm_format_string_4",
                                    llvm::FunctionType::get(
                                        ctx.getPtrType(),
                                        {ctx.getPtrType(), ctx.getPtrType(), ctx.getPtrType(),
                                         ctx.getPtrType(), ctx.getPtrType()},
                                        false));
                                formattedStr = builder->CreateCall(formatFunc, stringArgs);
                            }
                        }

                        // WASMで処理されなかった場合は従来の処理
                        if (!formattedStr) {
                            // 非WASM または 4引数以上の場合は従来の処理
                            llvm::Value* currentStr = firstArg;

                            // MIR形式: [format_string, arg_count, arg1, arg2, ...]
                            // arg_count（引数1）はスキップして、実際の引数（インデックス2以降）を処理
                            size_t startIdx = 2;  // 実際の引数はインデックス2から

                            // 引数が2つだけの場合は旧形式：[format_string, arg1]
                            if (callData.args.size() == 2) {
                                startIdx = 1;
                            }

                            // 各値について処理
                            for (size_t i = startIdx; i < callData.args.size(); ++i) {
                                auto value = convertOperand(*callData.args[i]);
                                auto valueType = value->getType();
                                auto hirType = getOperandType(*callData.args[i]);

                                if (valueType->isPointerTy()) {
                                    // 文字列型：cm_format_replace_string を使用
                                    auto replaceFunc = module->getOrInsertFunction(
                                        "cm_format_replace_string",
                                        llvm::FunctionType::get(
                                            ctx.getPtrType(), {ctx.getPtrType(), ctx.getPtrType()},
                                            false));
                                    currentStr =
                                        builder->CreateCall(replaceFunc, {currentStr, value});
                                } else if (valueType->isFloatingPointTy()) {
                                    // 浮動小数点型：cm_format_replace_double を使用
                                    auto doubleVal = value;
                                    if (valueType->isFloatTy()) {
                                        doubleVal = builder->CreateFPExt(value, ctx.getF64Type());
                                    }
                                    auto replaceFunc = module->getOrInsertFunction(
                                        "cm_format_replace_double",
                                        llvm::FunctionType::get(
                                            ctx.getPtrType(), {ctx.getPtrType(), ctx.getF64Type()},
                                            false));
                                    currentStr =
                                        builder->CreateCall(replaceFunc, {currentStr, doubleVal});
                                } else if (valueType->isIntegerTy()) {
                                    auto intType = llvm::cast<llvm::IntegerType>(valueType);

                                    // 型情報を使用して bool/char/整数を判定
                                    bool isBoolType =
                                        hirType && hirType->kind == hir::TypeKind::Bool;
                                    bool isCharType =
                                        hirType && hirType->kind == hir::TypeKind::Char;
                                    bool isUnsigned =
                                        hirType && (hirType->kind == hir::TypeKind::UTiny ||
                                                    hirType->kind == hir::TypeKind::UShort ||
                                                    hirType->kind == hir::TypeKind::UInt ||
                                                    hirType->kind == hir::TypeKind::ULong);

                                    if (isBoolType) {
                                        // bool型：true/false として出力
                                        auto boolVal = value;
                                        if (intType->getBitWidth() != 8) {
                                            boolVal = builder->CreateTrunc(value, ctx.getI8Type());
                                        }
                                        auto formatBoolFunc = module->getOrInsertFunction(
                                            "cm_format_bool",
                                            llvm::FunctionType::get(ctx.getPtrType(),
                                                                    {ctx.getI8Type()}, false));
                                        auto boolStr =
                                            builder->CreateCall(formatBoolFunc, {boolVal});
                                        auto replaceFunc = module->getOrInsertFunction(
                                            "cm_format_replace",
                                            llvm::FunctionType::get(
                                                ctx.getPtrType(),
                                                {ctx.getPtrType(), ctx.getPtrType()}, false));
                                        currentStr =
                                            builder->CreateCall(replaceFunc, {currentStr, boolStr});
                                    } else if (isCharType) {
                                        // char型：文字として出力
                                        auto charVal = value;
                                        if (intType->getBitWidth() != 8) {
                                            charVal = builder->CreateTrunc(value, ctx.getI8Type());
                                        }
                                        auto formatCharFunc = module->getOrInsertFunction(
                                            "cm_format_char",
                                            llvm::FunctionType::get(ctx.getPtrType(),
                                                                    {ctx.getI8Type()}, false));
                                        auto charStr =
                                            builder->CreateCall(formatCharFunc, {charVal});
                                        auto replaceFunc = module->getOrInsertFunction(
                                            "cm_format_replace",
                                            llvm::FunctionType::get(
                                                ctx.getPtrType(),
                                                {ctx.getPtrType(), ctx.getPtrType()}, false));
                                        currentStr =
                                            builder->CreateCall(replaceFunc, {currentStr, charStr});
                                    } else {
                                        // 整数型：符号なしの場合はcm_format_replace_uint、符号付きはcm_format_replace_intを使用
                                        auto intVal = value;
                                        if (valueType->getIntegerBitWidth() != 32) {
                                            if (isUnsigned) {
                                                intVal =
                                                    builder->CreateZExt(value, ctx.getI32Type());
                                            } else {
                                                intVal =
                                                    builder->CreateSExt(value, ctx.getI32Type());
                                            }
                                        }
                                        if (isUnsigned) {
                                            auto replaceFunc = module->getOrInsertFunction(
                                                "cm_format_replace_uint",
                                                llvm::FunctionType::get(
                                                    ctx.getPtrType(),
                                                    {ctx.getPtrType(), ctx.getI32Type()}, false));
                                            currentStr = builder->CreateCall(replaceFunc,
                                                                             {currentStr, intVal});
                                        } else {
                                            auto replaceFunc = module->getOrInsertFunction(
                                                "cm_format_replace_int",
                                                llvm::FunctionType::get(
                                                    ctx.getPtrType(),
                                                    {ctx.getPtrType(), ctx.getI32Type()}, false));
                                            currentStr = builder->CreateCall(replaceFunc,
                                                                             {currentStr, intVal});
                                        }
                                    }
                                }
                            }

                            formattedStr = currentStr;
                        }

                        // 出力
                        auto printFunc =
                            isNewline ? module->getOrInsertFunction(
                                            "cm_println_string",
                                            llvm::FunctionType::get(ctx.getVoidType(),
                                                                    {ctx.getPtrType()}, false))
                                      : module->getOrInsertFunction(
                                            "cm_print_string",
                                            llvm::FunctionType::get(ctx.getVoidType(),
                                                                    {ctx.getPtrType()}, false));

                        builder->CreateCall(printFunc, {formattedStr});
                    } else {
                        // 最初の引数が文字列でない場合：全ての引数を連結して出力
                        llvm::Value* resultStr = builder->CreateGlobalStringPtr("", "concat_str");

                        for (size_t i = 0; i < callData.args.size(); ++i) {
                            auto value = convertOperand(*callData.args[i]);
                            auto valueType = value->getType();
                            auto hirType = getOperandType(*callData.args[i]);

                            llvm::Value* valueStr = nullptr;
                            if (valueType->isPointerTy()) {
                                valueStr = value;
                            } else if (valueType->isIntegerTy()) {
                                auto intType = llvm::cast<llvm::IntegerType>(valueType);
                                bool isUnsigned =
                                    hirType && (hirType->kind == hir::TypeKind::UTiny ||
                                                hirType->kind == hir::TypeKind::UShort ||
                                                hirType->kind == hir::TypeKind::UInt ||
                                                hirType->kind == hir::TypeKind::ULong);

                                auto intVal = value;
                                if (intType->getBitWidth() != 32) {
                                    if (isUnsigned) {
                                        intVal = builder->CreateZExt(value, ctx.getI32Type());
                                    } else {
                                        intVal = builder->CreateSExt(value, ctx.getI32Type());
                                    }
                                }

                                llvm::FunctionCallee formatFunc;
                                if (isUnsigned) {
                                    formatFunc = module->getOrInsertFunction(
                                        "cm_format_uint",
                                        llvm::FunctionType::get(ctx.getPtrType(),
                                                                {ctx.getI32Type()}, false));
                                } else {
                                    formatFunc = module->getOrInsertFunction(
                                        "cm_format_int",
                                        llvm::FunctionType::get(ctx.getPtrType(),
                                                                {ctx.getI32Type()}, false));
                                }
                                valueStr = builder->CreateCall(formatFunc, {intVal});
                            } else if (valueType->isFloatingPointTy()) {
                                auto doubleVal = value;
                                if (valueType->isFloatTy()) {
                                    doubleVal = builder->CreateFPExt(value, ctx.getF64Type());
                                }
                                auto formatFunc = module->getOrInsertFunction(
                                    "cm_format_double",
                                    llvm::FunctionType::get(ctx.getPtrType(), {ctx.getF64Type()},
                                                            false));
                                valueStr = builder->CreateCall(formatFunc, {doubleVal});
                            }

                            if (valueStr) {
                                auto concatFunc = module->getOrInsertFunction(
                                    "cm_string_concat",
                                    llvm::FunctionType::get(ctx.getPtrType(),
                                                            {ctx.getPtrType(), ctx.getPtrType()},
                                                            false));
                                resultStr = builder->CreateCall(concatFunc, {resultStr, valueStr});
                            }
                        }

                        // 出力
                        auto printFunc =
                            isNewline ? module->getOrInsertFunction(
                                            "cm_println_string",
                                            llvm::FunctionType::get(ctx.getVoidType(),
                                                                    {ctx.getPtrType()}, false))
                                      : module->getOrInsertFunction(
                                            "cm_print_string",
                                            llvm::FunctionType::get(ctx.getVoidType(),
                                                                    {ctx.getPtrType()}, false));

                        builder->CreateCall(printFunc, {resultStr});
                    }
                } else {
                    // 単一引数の場合
                    auto arg = convertOperand(*callData.args[0]);
                    auto argType = arg->getType();
                    auto hirType = getOperandType(*callData.args[0]);

                    if (argType->isPointerTy()) {
                        // 文字列の場合：ランタイムライブラリの関数を使用
                        auto printFunc =
                            isNewline ? module->getOrInsertFunction(
                                            "cm_println_string",
                                            llvm::FunctionType::get(ctx.getVoidType(),
                                                                    {ctx.getPtrType()}, false))
                                      : module->getOrInsertFunction(
                                            "cm_print_string",
                                            llvm::FunctionType::get(ctx.getVoidType(),
                                                                    {ctx.getPtrType()}, false));

                        builder->CreateCall(printFunc, {arg});
                    } else if (argType->isIntegerTy()) {
                        // 整数型の場合
                        auto intType = llvm::cast<llvm::IntegerType>(argType);

                        // 型情報を使用して bool/char/整数を判定
                        bool isBoolType = hirType && hirType->kind == hir::TypeKind::Bool;
                        bool isCharType = hirType && hirType->kind == hir::TypeKind::Char;
                        bool isUnsigned = hirType && (hirType->kind == hir::TypeKind::UTiny ||
                                                      hirType->kind == hir::TypeKind::UShort ||
                                                      hirType->kind == hir::TypeKind::UInt ||
                                                      hirType->kind == hir::TypeKind::ULong);

                        if (isBoolType) {
                            // bool型：cm_print_bool または cm_println_bool を使用
                            auto boolArg = arg;
                            if (intType->getBitWidth() != 8) {
                                boolArg = builder->CreateTrunc(arg, ctx.getI8Type());
                            }
                            auto printBoolFunc = module->getOrInsertFunction(
                                isNewline ? "cm_println_bool" : "cm_print_bool",
                                llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI8Type()},
                                                        false));
                            builder->CreateCall(printBoolFunc, {boolArg});
                        } else if (isCharType) {
                            // char型：cm_print_char または cm_println_char を使用
                            auto charArg = arg;
                            if (intType->getBitWidth() != 8) {
                                charArg = builder->CreateTrunc(arg, ctx.getI8Type());
                            }
                            auto printCharFunc = module->getOrInsertFunction(
                                isNewline ? "cm_println_char" : "cm_print_char",
                                llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI8Type()},
                                                        false));
                            builder->CreateCall(printCharFunc, {charArg});
                        } else {
                            // 整数型：符号なし/符号付きで別の関数を使用
                            llvm::FunctionCallee printFunc;
                            if (isUnsigned) {
                                printFunc =
                                    isNewline
                                        ? module->getOrInsertFunction(
                                              "cm_println_uint",
                                              llvm::FunctionType::get(ctx.getVoidType(),
                                                                      {ctx.getI32Type()}, false))
                                        : module->getOrInsertFunction(
                                              "cm_print_uint",
                                              llvm::FunctionType::get(ctx.getVoidType(),
                                                                      {ctx.getI32Type()}, false));
                            } else {
                                printFunc =
                                    isNewline
                                        ? module->getOrInsertFunction(
                                              "cm_println_int",
                                              llvm::FunctionType::get(ctx.getVoidType(),
                                                                      {ctx.getI32Type()}, false))
                                        : module->getOrInsertFunction(
                                              "cm_print_int",
                                              llvm::FunctionType::get(ctx.getVoidType(),
                                                                      {ctx.getI32Type()}, false));
                            }

                            // 符号なしの場合はZExt、符号付きはSExtを使用
                            auto intArg = arg;
                            if (intType->getBitWidth() < 32) {
                                if (isUnsigned) {
                                    intArg = builder->CreateZExt(arg, ctx.getI32Type());
                                } else {
                                    intArg = builder->CreateSExt(arg, ctx.getI32Type());
                                }
                            } else if (intType->getBitWidth() > 32) {
                                intArg = builder->CreateTrunc(arg, ctx.getI32Type());
                            }

                            builder->CreateCall(printFunc, {intArg});
                        }
                    } else if (argType->isFloatingPointTy()) {
                        // 浮動小数点数の場合：ランタイムライブラリの関数を使用
                        auto printFunc =
                            isNewline ? module->getOrInsertFunction(
                                            "cm_println_double",
                                            llvm::FunctionType::get(ctx.getVoidType(),
                                                                    {ctx.getF64Type()}, false))
                                      : module->getOrInsertFunction(
                                            "cm_print_double",
                                            llvm::FunctionType::get(ctx.getVoidType(),
                                                                    {ctx.getF64Type()}, false));

                        // 必要に応じてdoubleに変換
                        auto doubleArg = arg;
                        if (argType->isFloatTy()) {
                            doubleArg = builder->CreateFPExt(arg, ctx.getF64Type());
                        }

                        builder->CreateCall(printFunc, {doubleArg});
                    }
                }
            } else {
                // その他の関数の通常処理
                std::vector<llvm::Value*> args;
                for (const auto& arg : callData.args) {
                    args.push_back(convertOperand(*arg));
                }

                llvm::Function* callee = functions[funcName];
                if (!callee) {
                    callee = declareExternalFunction(funcName);
                }

                if (callee) {
                    // 引数の型が一致しない場合はbitcastで変換
                    // (インターフェース型を引数に取る関数に構造体を渡す場合など)
                    auto funcType = callee->getFunctionType();
                    for (size_t i = 0; i < args.size() && i < funcType->getNumParams(); ++i) {
                        auto expectedType = funcType->getParamType(i);
                        auto actualType = args[i]->getType();
                        if (expectedType != actualType) {
                            // ポインタ型同士の場合はbitcast
                            if (expectedType->isPointerTy() && actualType->isPointerTy()) {
                                args[i] = builder->CreateBitCast(args[i], expectedType);
                            }
                        }
                    }

                    auto result = builder->CreateCall(callee, args);

                    // 戻り値がある場合は保存先に格納
                    if (callData.destination) {
                        locals[callData.destination->local] = result;
                    }
                }
            }

            // 次のブロックへ
            if (callData.success != mir::INVALID_BLOCK) {
                builder->CreateBr(blocks[callData.success]);
            }
            break;
        }
    }
}

// 右辺値変換

}  // namespace cm::codegen::llvm_backend
