#include "mir_to_llvm.hpp"

#include "../../common/debug/codegen.hpp"

#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

namespace cm::codegen::llvm_backend {

// 関数シグネチャ変換（メンバ関数に変更）
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
    for (const auto& structDef : program.structs) {
        // 構造体定義を保存
        structDefs[structDef->name] = structDef.get();

        // LLVM構造体型を作成
        std::vector<llvm::Type*> fieldTypes;
        for (const auto& field : structDef->fields) {
            fieldTypes.push_back(convertType(field.type));
        }

        // 構造体型を作成（名前付き）
        auto structType = llvm::StructType::create(ctx.getContext(), fieldTypes, structDef->name);
        structTypes[structDef->name] = structType;
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

// ターミネータ変換
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
                if (currentMIRFunction->return_local < currentMIRFunction->locals.size()) {
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

            // 関数オペランドを取得
            std::string funcName;
            if (callData.func->kind == mir::MirOperand::FunctionRef) {
                funcName = std::get<std::string>(callData.func->data);
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
                            callData.args.size() >= 2 && callData.args.size() <= 5 &&
                            !hasFormatSpecifiers) {
                            // 引数を文字列に変換
                            std::vector<llvm::Value*> stringArgs;
                            stringArgs.push_back(firstArg);  // フォーマット文字列

                            for (size_t i = 1; i < callData.args.size(); ++i) {
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

                            // 各値について処理
                            for (size_t i = 1; i < callData.args.size(); ++i) {
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

                                    if (isBoolType && intType->getBitWidth() == 8) {
                                        // bool型：true/false として出力
                                        auto formatBoolFunc = module->getOrInsertFunction(
                                            "cm_format_bool",
                                            llvm::FunctionType::get(ctx.getPtrType(),
                                                                    {ctx.getI8Type()}, false));
                                        auto boolStr = builder->CreateCall(formatBoolFunc, {value});
                                        auto replaceFunc = module->getOrInsertFunction(
                                            "cm_format_replace",
                                            llvm::FunctionType::get(
                                                ctx.getPtrType(),
                                                {ctx.getPtrType(), ctx.getPtrType()}, false));
                                        currentStr =
                                            builder->CreateCall(replaceFunc, {currentStr, boolStr});
                                    } else if (isCharType && intType->getBitWidth() == 8) {
                                        // char型：文字として出力
                                        auto formatCharFunc = module->getOrInsertFunction(
                                            "cm_format_char",
                                            llvm::FunctionType::get(ctx.getPtrType(),
                                                                    {ctx.getI8Type()}, false));
                                        auto charStr = builder->CreateCall(formatCharFunc, {value});
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
                        bool isUnsigned = hirType && (hirType->kind == hir::TypeKind::UTiny ||
                                                      hirType->kind == hir::TypeKind::UShort ||
                                                      hirType->kind == hir::TypeKind::UInt ||
                                                      hirType->kind == hir::TypeKind::ULong);

                        if (isBoolType && intType->getBitWidth() == 8) {
                            // bool型：ランタイムライブラリの関数を使用
                            auto printBoolFunc = module->getOrInsertFunction(
                                "cm_print_bool",
                                llvm::FunctionType::get(ctx.getVoidType(),
                                                        {ctx.getI8Type(), ctx.getI8Type()}, false));

                            auto withNewline = isNewline
                                                   ? llvm::ConstantInt::get(ctx.getI8Type(), 1)
                                                   : llvm::ConstantInt::get(ctx.getI8Type(), 0);

                            builder->CreateCall(printBoolFunc, {arg, withNewline});
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
llvm::Value* MIRToLLVM::convertRvalue(const mir::MirRvalue& rvalue) {
    switch (rvalue.kind) {
        case mir::MirRvalue::Use: {
            auto& useData = std::get<mir::MirRvalue::UseData>(rvalue.data);
            return convertOperand(*useData.operand);
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
        case mir::MirOperand::FunctionRef: {
            // 関数ポインタ（簡易実装）
            return nullptr;
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

// 型変換
llvm::Type* MIRToLLVM::convertType(const hir::TypePtr& type) {
    if (!type)
        return ctx.getI32Type();

    switch (type->kind) {
        case hir::TypeKind::Void:
            return ctx.getVoidType();
        case hir::TypeKind::Bool:
            // メモリ上ではi8として扱う（i1は演算時のみ）
            return ctx.getI8Type();
        case hir::TypeKind::Tiny:
        case hir::TypeKind::UTiny:
        case hir::TypeKind::Char:
            return ctx.getI8Type();
        case hir::TypeKind::Short:
        case hir::TypeKind::UShort:
            return ctx.getI16Type();
        case hir::TypeKind::Int:
        case hir::TypeKind::UInt:
            return ctx.getI32Type();
        case hir::TypeKind::Long:
        case hir::TypeKind::ULong:
            return ctx.getI64Type();
        case hir::TypeKind::Float:
            return ctx.getF32Type();
        case hir::TypeKind::Double:
            return ctx.getF64Type();
        case hir::TypeKind::String:
            return ctx.getPtrType();
        case hir::TypeKind::Pointer:
        case hir::TypeKind::Reference:
            return ctx.getPtrType();
        case hir::TypeKind::Array: {
            auto elemType = convertType(type->element_type);
            size_t size = type->array_size.value_or(0);
            return llvm::ArrayType::get(elemType, size);
        }
        case hir::TypeKind::Struct: {
            // 構造体型を検索
            auto it = structTypes.find(type->name);
            if (it != structTypes.end()) {
                return it->second;
            }
            // 見つからない場合は不透明型として扱う
            return llvm::StructType::create(ctx.getContext(), type->name);
        }
        default:
            return ctx.getI32Type();
    }
}

// 定数変換
llvm::Constant* MIRToLLVM::convertConstant(const mir::MirConstant& constant) {
    // std::variant を処理
    if (std::holds_alternative<bool>(constant.value)) {
        // bool定数はi8として生成（メモリ格納用）
        return llvm::ConstantInt::get(ctx.getI8Type(), std::get<bool>(constant.value));
    } else if (std::holds_alternative<char>(constant.value)) {
        // 文字リテラルはi8として生成
        return llvm::ConstantInt::get(ctx.getI8Type(), std::get<char>(constant.value));
    } else if (std::holds_alternative<int64_t>(constant.value)) {
        return llvm::ConstantInt::get(ctx.getI32Type(), std::get<int64_t>(constant.value));
    } else if (std::holds_alternative<double>(constant.value)) {
        return llvm::ConstantFP::get(ctx.getF64Type(), std::get<double>(constant.value));
    } else if (std::holds_alternative<std::string>(constant.value)) {
        // 文字列リテラル
        auto& str = std::get<std::string>(constant.value);
        return builder->CreateGlobalStringPtr(str, "str");
    } else {
        // nullまたは未知の型
        return llvm::Constant::getNullValue(ctx.getI32Type());
    }
}

// 二項演算変換
llvm::Value* MIRToLLVM::convertBinaryOp(mir::MirBinaryOp op, llvm::Value* lhs, llvm::Value* rhs) {
    switch (op) {
        // 算術演算
        case mir::MirBinaryOp::Add: {
            // 文字列連結の処理
            auto lhsType = lhs->getType();
            auto rhsType = rhs->getType();

            // 両方またはどちらかがポインタ（文字列）の場合
            if (lhsType->isPointerTy() || rhsType->isPointerTy()) {
                // 左側を文字列に変換
                llvm::Value* lhsStr = lhs;
                if (!lhsType->isPointerTy()) {
                    if (lhsType->isFloatingPointTy()) {
                        // 浮動小数点を文字列に変換
                        auto formatFunc = module->getOrInsertFunction(
                            "cm_format_double",
                            llvm::FunctionType::get(ctx.getPtrType(), {ctx.getF64Type()}, false));
                        auto lhsDouble = lhs;
                        if (lhsType->isFloatTy()) {
                            lhsDouble = builder->CreateFPExt(lhs, ctx.getF64Type());
                        }
                        lhsStr = builder->CreateCall(formatFunc, {lhsDouble});
                    } else if (lhsType->isIntegerTy()) {
                        // 整数を文字列に変換
                        auto intType = llvm::cast<llvm::IntegerType>(lhsType);
                        if (intType->getBitWidth() == 8) {
                            // i8: 定数値の場合のみ簡易判定（0,1ならbool）
                            // 実行時判定は複雑なので、ここでは char として扱う
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
                        // 浮動小数点を文字列に変換
                        auto formatFunc = module->getOrInsertFunction(
                            "cm_format_double",
                            llvm::FunctionType::get(ctx.getPtrType(), {ctx.getF64Type()}, false));
                        auto rhsDouble = rhs;
                        if (rhsType->isFloatTy()) {
                            rhsDouble = builder->CreateFPExt(rhs, ctx.getF64Type());
                        }
                        rhsStr = builder->CreateCall(formatFunc, {rhsDouble});
                    } else if (rhsType->isIntegerTy()) {
                        // 整数を文字列に変換
                        auto intType = llvm::cast<llvm::IntegerType>(rhsType);
                        if (intType->getBitWidth() == 8) {
                            // i8: char として扱う
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
            if (lhs->getType()->isFloatingPointTy()) {
                return builder->CreateFAdd(lhs, rhs, "fadd");
            }
            return builder->CreateAdd(lhs, rhs, "add");
        }
        case mir::MirBinaryOp::Sub: {
            if (lhs->getType()->isFloatingPointTy()) {
                return builder->CreateFSub(lhs, rhs, "fsub");
            }
            return builder->CreateSub(lhs, rhs, "sub");
        }
        case mir::MirBinaryOp::Mul: {
            if (lhs->getType()->isFloatingPointTy()) {
                return builder->CreateFMul(lhs, rhs, "fmul");
            }
            return builder->CreateMul(lhs, rhs, "mul");
        }
        case mir::MirBinaryOp::Div: {
            if (lhs->getType()->isFloatingPointTy()) {
                return builder->CreateFDiv(lhs, rhs, "fdiv");
            }
            return builder->CreateSDiv(lhs, rhs, "div");
        }
        case mir::MirBinaryOp::Mod: {
            if (lhs->getType()->isFloatingPointTy()) {
                return builder->CreateFRem(lhs, rhs, "frem");
            }
            return builder->CreateSRem(lhs, rhs, "mod");
        }

        // 比較演算
        case mir::MirBinaryOp::Eq: {
            if (lhs->getType()->isFloatingPointTy()) {
                return builder->CreateFCmpOEQ(lhs, rhs, "feq");
            }
            // 文字列比較（両方がポインタ型の場合）
            if (lhs->getType()->isPointerTy() && rhs->getType()->isPointerTy()) {
                // strcmp(lhs, rhs) == 0
                auto strcmpFunc = module->getOrInsertFunction(
                    "strcmp", llvm::FunctionType::get(ctx.getI32Type(),
                                                      {ctx.getPtrType(), ctx.getPtrType()}, false));
                auto cmpResult = builder->CreateCall(strcmpFunc, {lhs, rhs}, "strcmp");
                return builder->CreateICmpEQ(cmpResult, llvm::ConstantInt::get(ctx.getI32Type(), 0),
                                             "streq");
            }
            // 整数型のビット幅を揃える
            if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
                auto lhsBits = lhs->getType()->getIntegerBitWidth();
                auto rhsBits = rhs->getType()->getIntegerBitWidth();
                if (lhsBits < rhsBits) {
                    lhs = builder->CreateSExt(lhs, rhs->getType());
                } else if (rhsBits < lhsBits) {
                    rhs = builder->CreateSExt(rhs, lhs->getType());
                }
            }
            return builder->CreateICmpEQ(lhs, rhs, "eq");
        }
        case mir::MirBinaryOp::Ne: {
            if (lhs->getType()->isFloatingPointTy()) {
                return builder->CreateFCmpONE(lhs, rhs, "fne");
            }
            // 文字列比較（両方がポインタ型の場合）
            if (lhs->getType()->isPointerTy() && rhs->getType()->isPointerTy()) {
                // strcmp(lhs, rhs) != 0
                auto strcmpFunc = module->getOrInsertFunction(
                    "strcmp", llvm::FunctionType::get(ctx.getI32Type(),
                                                      {ctx.getPtrType(), ctx.getPtrType()}, false));
                auto cmpResult = builder->CreateCall(strcmpFunc, {lhs, rhs}, "strcmp");
                return builder->CreateICmpNE(cmpResult, llvm::ConstantInt::get(ctx.getI32Type(), 0),
                                             "strne");
            }
            // 整数型のビット幅を揃える
            if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
                auto lhsBits = lhs->getType()->getIntegerBitWidth();
                auto rhsBits = rhs->getType()->getIntegerBitWidth();
                if (lhsBits < rhsBits) {
                    lhs = builder->CreateSExt(lhs, rhs->getType());
                } else if (rhsBits < lhsBits) {
                    rhs = builder->CreateSExt(rhs, lhs->getType());
                }
            }
            return builder->CreateICmpNE(lhs, rhs, "ne");
        }
        case mir::MirBinaryOp::Lt: {
            if (lhs->getType()->isFloatingPointTy()) {
                return builder->CreateFCmpOLT(lhs, rhs, "flt");
            }
            // 整数型のビット幅を揃える
            if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
                auto lhsBits = lhs->getType()->getIntegerBitWidth();
                auto rhsBits = rhs->getType()->getIntegerBitWidth();
                if (lhsBits < rhsBits) {
                    lhs = builder->CreateSExt(lhs, rhs->getType());
                } else if (rhsBits < lhsBits) {
                    rhs = builder->CreateSExt(rhs, lhs->getType());
                }
            }
            return builder->CreateICmpSLT(lhs, rhs, "lt");
        }
        case mir::MirBinaryOp::Le: {
            if (lhs->getType()->isFloatingPointTy()) {
                return builder->CreateFCmpOLE(lhs, rhs, "fle");
            }
            // 整数型のビット幅を揃える
            if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
                auto lhsBits = lhs->getType()->getIntegerBitWidth();
                auto rhsBits = rhs->getType()->getIntegerBitWidth();
                if (lhsBits < rhsBits) {
                    lhs = builder->CreateSExt(lhs, rhs->getType());
                } else if (rhsBits < lhsBits) {
                    rhs = builder->CreateSExt(rhs, lhs->getType());
                }
            }
            return builder->CreateICmpSLE(lhs, rhs, "le");
        }
        case mir::MirBinaryOp::Gt: {
            if (lhs->getType()->isFloatingPointTy()) {
                return builder->CreateFCmpOGT(lhs, rhs, "fgt");
            }
            // 整数型のビット幅を揃える
            if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
                auto lhsBits = lhs->getType()->getIntegerBitWidth();
                auto rhsBits = rhs->getType()->getIntegerBitWidth();
                if (lhsBits < rhsBits) {
                    lhs = builder->CreateSExt(lhs, rhs->getType());
                } else if (rhsBits < lhsBits) {
                    rhs = builder->CreateSExt(rhs, lhs->getType());
                }
            }
            return builder->CreateICmpSGT(lhs, rhs, "gt");
        }
        case mir::MirBinaryOp::Ge: {
            if (lhs->getType()->isFloatingPointTy()) {
                return builder->CreateFCmpOGE(lhs, rhs, "fge");
            }
            // 整数型のビット幅を揃える
            if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
                auto lhsBits = lhs->getType()->getIntegerBitWidth();
                auto rhsBits = rhs->getType()->getIntegerBitWidth();
                if (lhsBits < rhsBits) {
                    lhs = builder->CreateSExt(lhs, rhs->getType());
                } else if (rhsBits < lhsBits) {
                    rhs = builder->CreateSExt(rhs, lhs->getType());
                }
            }
            return builder->CreateICmpSGE(lhs, rhs, "ge");
        }

        // ビット演算
        case mir::MirBinaryOp::BitAnd:
            return builder->CreateAnd(lhs, rhs, "bitand");
        case mir::MirBinaryOp::BitOr:
            return builder->CreateOr(lhs, rhs, "bitor");
        case mir::MirBinaryOp::BitXor:
            return builder->CreateXor(lhs, rhs, "xor");
        case mir::MirBinaryOp::Shl:
            return builder->CreateShl(lhs, rhs, "shl");
        case mir::MirBinaryOp::Shr:
            return builder->CreateAShr(lhs, rhs, "shr");

        // 論理演算
        case mir::MirBinaryOp::And:
            return builder->CreateAnd(lhs, rhs, "and");
        case mir::MirBinaryOp::Or:
            return builder->CreateOr(lhs, rhs, "or");

        default:
            return nullptr;
    }
}

// 単項演算変換
llvm::Value* MIRToLLVM::convertUnaryOp(mir::MirUnaryOp op, llvm::Value* operand) {
    switch (op) {
        case mir::MirUnaryOp::Not: {
            // bool値（i8で0または1）の場合、論理否定を行う
            // i8の場合: 0 -> 1, 1 -> 0
            auto operandType = operand->getType();
            if (operandType->isIntegerTy(8)) {
                // i8の場合、論理否定: (value == 0) ? 1 : 0
                auto zero = llvm::ConstantInt::get(ctx.getI8Type(), 0);
                auto one = llvm::ConstantInt::get(ctx.getI8Type(), 1);
                auto isZero = builder->CreateICmpEQ(operand, zero, "is_zero");
                return builder->CreateSelect(isZero, one, zero, "logical_not");
            } else {
                // その他の整数型の場合はビット反転
                return builder->CreateNot(operand, "not");
            }
        }
        case mir::MirUnaryOp::Neg:
            return builder->CreateNeg(operand, "neg");
        default:
            return nullptr;
    }
}

// 外部関数宣言
llvm::Function* MIRToLLVM::declareExternalFunction(const std::string& name) {
    if (name == "print" || name == "println") {
        // printを printf として宣言
        auto printfType = llvm::FunctionType::get(ctx.getI32Type(), {ctx.getPtrType()},
                                                  true  // 可変長引数
        );
        auto func = module->getOrInsertFunction("printf", printfType);
        return llvm::cast<llvm::Function>(func.getCallee());
    } else if (name == "puts") {
        // puts関数
        auto putsType = llvm::FunctionType::get(ctx.getI32Type(), {ctx.getPtrType()}, false);
        auto func = module->getOrInsertFunction("puts", putsType);
        return llvm::cast<llvm::Function>(func.getCallee());
    }

    // その他の関数は void() として宣言
    auto funcType = llvm::FunctionType::get(ctx.getVoidType(), false);
    auto func = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, name, module);
    return func;
}

// 組み込み関数呼び出し（将来の実装用）
llvm::Value* MIRToLLVM::callIntrinsic([[maybe_unused]] const std::string& name,
                                      [[maybe_unused]] llvm::ArrayRef<llvm::Value*> args) {
    // 簡易実装：組み込み関数は未実装
    return nullptr;
}

// ヘルパー関数生成
// generateHelperFunctions() は削除：ランタイムライブラリを使用するため不要

// パニック生成
void MIRToLLVM::generatePanic(const std::string& message) {
    // パニックメッセージを出力
    auto msgStr = builder->CreateGlobalStringPtr(message, "panic_msg");
    auto putsFunc = declareExternalFunction("puts");
    builder->CreateCall(putsFunc, {msgStr});

    // プログラム終了
    auto exitType = llvm::FunctionType::get(ctx.getVoidType(), {ctx.getI32Type()}, false);
    auto exitFunc = module->getOrInsertFunction("exit", exitType);
    builder->CreateCall(exitFunc, {llvm::ConstantInt::get(ctx.getI32Type(), 1)});
    builder->CreateUnreachable();
}

// MIRオペランドからHIR型情報を取得
hir::TypePtr MIRToLLVM::getOperandType(const mir::MirOperand& operand) {
    switch (operand.kind) {
        case mir::MirOperand::Constant: {
            auto& constant = std::get<mir::MirConstant>(operand.data);
            return constant.type;
        }
        case mir::MirOperand::Copy:
        case mir::MirOperand::Move: {
            auto& place = std::get<mir::MirPlace>(operand.data);
            if (currentMIRFunction && place.local < currentMIRFunction->locals.size()) {
                return currentMIRFunction->locals[place.local].type;
            }
            return nullptr;
        }
        default:
            return nullptr;
    }
}

}  // namespace cm::codegen::llvm_backend