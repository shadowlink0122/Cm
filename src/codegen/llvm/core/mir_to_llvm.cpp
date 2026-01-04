/// @file mir_to_llvm.cpp
/// @brief MIR → LLVM IR 変換器（メインモジュール）

#include "mir_to_llvm.hpp"

#include "../../../common/debug/codegen.hpp"
#include "../monitoring/compilation_guard.hpp"

#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <set>
#include <sstream>

namespace cm::codegen::llvm_backend {

// 関数の一意なIDを生成（オーバーロードを区別するため）
std::string MIRToLLVM::generateFunctionId(const mir::MirFunction& func) {
    // main関数は特別扱い
    if (func.name == "main") {
        return "main";
    }

    // ラムダ関数はそのまま
    if (func.name.find("__lambda_") == 0) {
        return func.name;
    }

    // ランタイム関数（cm_で始まる）はそのまま
    if (func.name.find("cm_") == 0) {
        return func.name;
    }

    // 外部関数（extern）はそのまま
    if (func.is_extern) {
        return func.name;
    }

    // 引数がない場合はそのまま
    if (func.arg_locals.empty()) {
        return func.name;
    }

    // 引数の型からサフィックスを生成
    std::string suffix;
    for (const auto& arg_local : func.arg_locals) {
        if (arg_local < func.locals.size()) {
            auto& local = func.locals[arg_local];
            if (local.type) {
                if (!suffix.empty())
                    suffix += "_";
                switch (local.type->kind) {
                    case hir::TypeKind::Void:
                        suffix += "v";
                        break;
                    case hir::TypeKind::Bool:
                        suffix += "b";
                        break;
                    case hir::TypeKind::Char:
                        suffix += "c";
                        break;
                    case hir::TypeKind::Tiny:
                        suffix += "i8";
                        break;
                    case hir::TypeKind::UTiny:
                        suffix += "u8";
                        break;
                    case hir::TypeKind::Short:
                        suffix += "i16";
                        break;
                    case hir::TypeKind::UShort:
                        suffix += "u16";
                        break;
                    case hir::TypeKind::Int:
                        suffix += "i";
                        break;
                    case hir::TypeKind::UInt:
                        suffix += "u";
                        break;
                    case hir::TypeKind::Long:
                        suffix += "i64";
                        break;
                    case hir::TypeKind::ULong:
                        suffix += "u64";
                        break;
                    case hir::TypeKind::Float:
                        suffix += "f";
                        break;
                    case hir::TypeKind::Double:
                        suffix += "d";
                        break;
                    case hir::TypeKind::String:
                        suffix += "s";
                        break;
                    case hir::TypeKind::Pointer:
                        suffix += "p";
                        break;
                    case hir::TypeKind::Struct:
                        suffix += "S" + local.type->name;
                        break;
                    default:
                        suffix += "x";
                        break;
                }
            }
        }
    }

    return suffix.empty() ? func.name : func.name + "_" + suffix;
}

// 呼び出し時の引数型から関数IDを生成
std::string MIRToLLVM::generateCallFunctionId(const std::string& baseName,
                                              const std::vector<mir::MirOperandPtr>& args) {
    // main関数は特別扱い
    if (baseName == "main") {
        return "main";
    }

    // ラムダ関数はそのまま
    if (baseName.find("__lambda_") == 0) {
        return baseName;
    }

    // ランタイム関数（cm_で始まる）はそのまま
    if (baseName.find("cm_") == 0) {
        return baseName;
    }

    // ビルトイン関数（__builtin_で始まる）はそのまま（無限ループ回避）
    if (baseName.find("__builtin_") == 0) {
        return baseName;
    }

    // 引数がない場合はそのまま
    if (args.empty()) {
        return baseName;
    }

    // 引数の型からサフィックスを生成
    std::string suffix;
    for (const auto& arg : args) {
        auto type = getOperandType(*arg);
        if (type) {
            if (!suffix.empty())
                suffix += "_";
            switch (type->kind) {
                case hir::TypeKind::Void:
                    suffix += "v";
                    break;
                case hir::TypeKind::Bool:
                    suffix += "b";
                    break;
                case hir::TypeKind::Char:
                    suffix += "c";
                    break;
                case hir::TypeKind::Tiny:
                    suffix += "i8";
                    break;
                case hir::TypeKind::UTiny:
                    suffix += "u8";
                    break;
                case hir::TypeKind::Short:
                    suffix += "i16";
                    break;
                case hir::TypeKind::UShort:
                    suffix += "u16";
                    break;
                case hir::TypeKind::Int:
                    suffix += "i";
                    break;
                case hir::TypeKind::UInt:
                    suffix += "u";
                    break;
                case hir::TypeKind::Long:
                    suffix += "i64";
                    break;
                case hir::TypeKind::ULong:
                    suffix += "u64";
                    break;
                case hir::TypeKind::Float:
                    suffix += "f";
                    break;
                case hir::TypeKind::Double:
                    suffix += "d";
                    break;
                case hir::TypeKind::String:
                    suffix += "s";
                    break;
                case hir::TypeKind::Pointer:
                    suffix += "p";
                    break;
                case hir::TypeKind::Struct:
                    suffix += "S" + type->name;
                    break;
                default:
                    suffix += "x";
                    break;
            }
        }
    }

    auto funcId = suffix.empty() ? baseName : baseName + "_" + suffix;

    // マップに存在するか確認
    if (functions.count(funcId) > 0) {
        return funcId;
    }

    // 見つからない場合、ベース名で検索（インターフェースパラメータを持つ関数の可能性）
    if (functions.count(baseName) > 0) {
        return baseName;
    }

    // インターフェース型を含む関数名のパターンマッチング
    // 例: print_it_SPrintable を print_it_SPoint の代わりに見つける
    for (const auto& [funcName, func] : functions) {
        // ベース名が一致し、かつ引数の数が同じ関数を探す
        if (funcName.find(baseName + "_") == 0) {
            // インターフェース名を含むサフィックスか確認
            auto funcSuffix = funcName.substr(baseName.length() + 1);
            // 構造体型が含まれているか確認（Sで始まる）
            if (funcSuffix.find("S") != std::string::npos) {
                return funcName;
            }
        }
    }

    return baseName;
}

llvm::Function* MIRToLLVM::convertFunctionSignature(const mir::MirFunction& func) {
    // ランタイム関数（cm_で始まる）は既存の宣言を使用
    if (func.name.find("cm_") == 0) {
        // 既存の関数があればそれを返す
        if (auto existingFunc = module->getFunction(func.name)) {
            return existingFunc;
        }
        // なければ declareExternalFunction で宣言
        return declareExternalFunction(func.name);
    }

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
                    // 構造体はポインタとして渡す（opaque pointer）
                    if (local.type->kind == hir::TypeKind::Struct) {
                        // LLVM 14+: opaque pointerを使用
                        paramTypes.push_back(ctx.getPtrType());
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

    // 関数型（可変長引数を考慮）
    auto funcType = llvm::FunctionType::get(returnType, paramTypes, func.is_variadic);

    // extern関数の場合は既存の関数を再利用（重複宣言を防ぐ）
    if (func.is_extern) {
        // 既存の関数があればそれを返す
        if (auto existingFunc = module->getFunction(func.name)) {
            return existingFunc;
        }
        // なければ宣言のみ作成
        auto callee = module->getOrInsertFunction(func.name, funcType);
        return llvm::cast<llvm::Function>(callee.getCallee());
    }

    // 関数作成
    auto llvmFunc =
        llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, func.name, module);

    // アロケータ関数にはnoinline属性を追加
    // LLVMが積極的にインライン化してから削除するのを防ぐ
    if (func.name.find("alloc") != std::string::npos ||
        func.name.find("dealloc") != std::string::npos ||
        func.name.find("reallocate") != std::string::npos) {
        llvmFunc->addFnAttr(llvm::Attribute::NoInline);
    }

    // main関数にはoptnone属性を追加
    // LLVMの最適化がcontrol flowを破壊するのを防ぐ
    if (func.name == "main") {
        llvmFunc->addFnAttr(llvm::Attribute::NoInline);
        llvmFunc->addFnAttr(llvm::Attribute::OptimizeNone);
    }

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

    // std::cerr << "[MIR2LLVM] Starting conversion with " << program.functions.size()
    // << " functions\n";

    currentProgram = &program;

    // インターフェース名を収集
    // std::cerr << "[MIR2LLVM] Collecting interfaces (" << program.interfaces.size() << ")...\n";
    size_t iface_count = 0;
    const size_t MAX_INTERFACES = 10000;  // 無限ループ防止
    for (const auto& iface : program.interfaces) {
        if (++iface_count > MAX_INTERFACES) {
            // debug_msg("MIR2LLVM", "ERROR: Too many interfaces, possible infinite loop");
            throw std::runtime_error("Too many interfaces in MIR program");
        }
        if (iface) {
            // std::cerr << "[MIR2LLVM]   Interface: " << iface->name << "\n";
            interfaceNames.insert(iface->name);
        }
    }
    // debug_msg("MIR2LLVM", "Interfaces collected");

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
    // 重複した関数はスキップ
    std::set<std::string> declaredFunctions;
    for (const auto& func : program.functions) {
        auto funcId = generateFunctionId(*func);
        // 既に宣言済みの場合はスキップ（重複を防ぐ）
        if (declaredFunctions.count(funcId) > 0) {
            continue;
        }
        declaredFunctions.insert(funcId);
        auto llvmFunc = convertFunctionSignature(*func);
        functions[funcId] = llvmFunc;
    }

    // vtableを生成（関数宣言後に実行）
    generateVTables(program);

    // 関数実装
    // 重複した関数はスキップ
    declaredFunctions.clear();
    // std::cerr << "[MIR2LLVM] Converting " << program.functions.size() << " functions...\n";
    for (size_t i = 0; i < program.functions.size(); ++i) {
        const auto& func = program.functions[i];
        auto funcId = generateFunctionId(*func);
        // std::cerr << "[MIR2LLVM] [" << (i + 1) << "/" << program.functions.size()
        // << "] Converting function: " << funcId << "\n";
        if (declaredFunctions.count(funcId) > 0) {
            // debug_msg("MIR2LLVM", "-> Skipping duplicate");
            continue;
        }
        declaredFunctions.insert(funcId);
        convertFunction(*func);
        // std::cerr << "[MIR2LLVM]   -> Done converting " << funcId << "\n";
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

    // CompilationGuardを使用した無限ループ検出
    auto& guard = get_compilation_guard();

    // 関数のハッシュ値を計算（簡単のため名前とサイズから）
    size_t func_hash = std::hash<std::string>{}(func.name) ^ func.basic_blocks.size();

    try {
        // 関数生成の開始を記録
        ScopedFunctionGuard func_guard(func.name, func_hash);

        // デバッグ用プログレス表示
        guard.show_progress("Function", 0, func.basic_blocks.size());

        // ランタイム関数（cm_print_*, cm_println_*など）はスキップ
        // これらはランタイムライブラリで実装されている
        if (func.name.find("cm_print") == 0 || func.name.find("cm_println") == 0 ||
            func.name.find("cm_int_to_string") == 0 || func.name.find("cm_uint_to_string") == 0 ||
            func.name.find("cm_double_to_string") == 0 ||
            func.name.find("cm_float_to_string") == 0 || func.name.find("cm_bool_to_string") == 0 ||
            func.name.find("cm_char_to_string") == 0 || func.name.find("cm_string_concat") == 0) {
            return;
        }

        cm::debug::codegen::log(cm::debug::codegen::Id::LLVMFunction, func.name,
                                cm::debug::Level::Debug);

        // std::cout << "[CODEGEN] Processing function: " << func.name << "\n" << std::flush;

        auto funcId = generateFunctionId(func);
        currentFunction = functions[funcId];
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
            if (std::find(func.arg_locals.begin(), func.arg_locals.end(), i) ==
                    func.arg_locals.end() &&
                i != func.return_local) {  // 引数と戻り値以外
                // 引数以外のローカル変数
                auto& local = func.locals[i];
                if (local.type) {
                    // void型はアロケーションしない
                    if (local.type->kind == hir::TypeKind::Void) {
                        continue;
                    }
                    // 関数ポインタ型はアロケーションしない（SSA形式で扱う）
                    if (local.type->kind == hir::TypeKind::Function ||
                        (local.type->kind == hir::TypeKind::Pointer && local.type->element_type &&
                         local.type->element_type->kind == hir::TypeKind::Function)) {
                        continue;
                    }
                    // 配列へのポインタ型の一時変数はアロケーションしない（SSA形式で扱う）
                    if (local.type->kind == hir::TypeKind::Pointer && local.type->element_type &&
                        local.type->element_type->kind == hir::TypeKind::Array &&
                        !local.is_user_variable) {
                        continue;
                    }
                    // 文字列型の一時変数はアロケーションしない（直接値を使用）
                    if (local.type->kind == hir::TypeKind::String && !local.is_user_variable) {
                        continue;
                    }

                    // 動的配列（スライス）の場合
                    if (local.type->kind == hir::TypeKind::Array &&
                        !local.type->array_size.has_value()) {
                        // スライスポインタを格納するallocaを作成
                        auto alloca = builder->CreateAlloca(ctx.getPtrType(), nullptr,
                                                            "slice_" + std::to_string(i));

                        // 要素サイズを計算
                        int64_t elemSize = 4;
                        if (local.type->element_type) {
                            auto elemKind = local.type->element_type->kind;
                            if (elemKind == hir::TypeKind::Array) {
                                // 多次元スライス: 要素はCmSlice構造体（32バイト）
                                elemSize = 32;
                            } else if (elemKind == hir::TypeKind::Long ||
                                       elemKind == hir::TypeKind::ULong ||
                                       elemKind == hir::TypeKind::Double ||
                                       elemKind == hir::TypeKind::Pointer ||
                                       elemKind == hir::TypeKind::String) {
                                elemSize = 8;
                            } else if (elemKind == hir::TypeKind::Char ||
                                       elemKind == hir::TypeKind::Bool) {
                                elemSize = 1;
                            } else if (elemKind == hir::TypeKind::Short ||
                                       elemKind == hir::TypeKind::UShort) {
                                elemSize = 2;
                            }
                        }

                        // cm_slice_new呼び出しでスライスを初期化
                        auto sliceNewFunc = declareExternalFunction("cm_slice_new");
                        auto elemSizeVal = llvm::ConstantInt::get(ctx.getI64Type(), elemSize);
                        auto initialCap = llvm::ConstantInt::get(ctx.getI64Type(), 4);
                        auto slicePtr = builder->CreateCall(sliceNewFunc, {elemSizeVal, initialCap},
                                                            "slice_ptr");
                        builder->CreateStore(slicePtr, alloca);

                        locals[i] = alloca;
                        allocatedLocals.insert(i);
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

                        // 構造体型の場合、スライスメンバーを初期化
                        if (local.type->kind == hir::TypeKind::Struct) {
                            auto structName = local.type->name;
                            auto structDefIt = structDefs.find(structName);
                            if (structDefIt != structDefs.end()) {
                                const auto* structDef = structDefIt->second;
                                auto* structLLVMType = structTypes[structName];

                                for (size_t fieldIdx = 0; fieldIdx < structDef->fields.size();
                                     ++fieldIdx) {
                                    const auto& field = structDef->fields[fieldIdx];
                                    // スライスフィールドを探す
                                    if (field.type && field.type->kind == hir::TypeKind::Array &&
                                        !field.type->array_size.has_value()) {
                                        // スライスフィールドのGEPを取得
                                        auto fieldPtr = builder->CreateStructGEP(
                                            structLLVMType, alloca, fieldIdx,
                                            "slice_field_" + field.name);

                                        // 要素サイズを計算
                                        int64_t elemSize = 4;
                                        if (field.type->element_type) {
                                            auto elemKind = field.type->element_type->kind;
                                            if (elemKind == hir::TypeKind::Long ||
                                                elemKind == hir::TypeKind::ULong ||
                                                elemKind == hir::TypeKind::Double ||
                                                elemKind == hir::TypeKind::Pointer ||
                                                elemKind == hir::TypeKind::String) {
                                                elemSize = 8;
                                            } else if (elemKind == hir::TypeKind::Char ||
                                                       elemKind == hir::TypeKind::Bool) {
                                                elemSize = 1;
                                            } else if (elemKind == hir::TypeKind::Short ||
                                                       elemKind == hir::TypeKind::UShort) {
                                                elemSize = 2;
                                            } else if (elemKind == hir::TypeKind::Struct) {
                                                // 構造体のサイズはポインタサイズ（簡略化）
                                                elemSize = 8;
                                            }
                                        }

                                        // cm_slice_new呼び出しでスライスを初期化
                                        auto sliceNewFunc = declareExternalFunction("cm_slice_new");
                                        auto elemSizeVal =
                                            llvm::ConstantInt::get(ctx.getI64Type(), elemSize);
                                        auto initialCap =
                                            llvm::ConstantInt::get(ctx.getI64Type(), 4);
                                        auto slicePtr = builder->CreateCall(
                                            sliceNewFunc, {elemSizeVal, initialCap},
                                            "slice_init_" + field.name);
                                        builder->CreateStore(slicePtr, fieldPtr);
                                    }
                                }
                            }
                        }
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
            // DCEで削除されたブロックはスキップ
            if (!func.basic_blocks[i])
                continue;
            auto bbName = "bb" + std::to_string(i);
            blocks[i] = llvm::BasicBlock::Create(ctx.getContext(), bbName, currentFunction);
        }

        // 最初のブロックへジャンプ
        if (!func.basic_blocks.empty() && func.basic_blocks[0]) {
            builder->CreateBr(blocks[0]);
        }

        // 各ブロックを変換（CompilationGuardによる監視）
        // std::cerr << "[MIR2LLVM] Function " << func.name << " has " << func.basic_blocks.size()
        // << " blocks\n";
        for (size_t i = 0; i < func.basic_blocks.size(); ++i) {
            // DCEで削除されたブロックはスキップ
            if (!func.basic_blocks[i]) {
                // std::cerr << "[MIR2LLVM]   Block " << i << " is null (DCE removed), skipping\n";
                continue;
            }

            // std::cerr << "[MIR2LLVM]   Converting block " << i << "/" << func.basic_blocks.size()
            // << "\n";

            // プログレス表示
            guard.show_progress("Function", i + 1, func.basic_blocks.size());

            convertBasicBlock(*func.basic_blocks[i]);

            // std::cerr << "[MIR2LLVM]   Block " << i << " converted successfully\n";
        }
    } catch (const std::runtime_error& e) {
        // 無限ループエラーのハンドリング
        guard.handle_infinite_loop_error(e);
        throw;  // エラーを再スロー
    }
}

// 基本ブロック変換
void MIRToLLVM::convertBasicBlock(const mir::BasicBlock& block) {
    // std::cerr << "[MIR2LLVM]     Entering convertBasicBlock for block " << block.id << "\n";
    // std::cerr << "[MIR2LLVM]       Block has " << block.statements.size() << " statements\n";
    if (block.terminator) {
        // std::cerr << "[MIR2LLVM]       Block has terminator type "
        // << static_cast<int>(block.terminator->kind) << "\n";
    } else {
        // debug_msg("MIR2LLVM", "Block has no terminator");
    }

    // blocksはunordered_mapなので、countで存在確認
    // std::cerr << "[MIR2LLVM]       Checking if block " << block.id << " is in blocks map...\n";
    if (blocks.count(block.id) > 0) {
        // std::cerr << "[MIR2LLVM]       Setting insert point for block " << block.id << "\n";
        builder->SetInsertPoint(blocks[block.id]);
        // debug_msg("MIR2LLVM", "Insert point set");
    } else {
        // ブロックがblocks mapに存在しない（DCEで削除された可能性）
        // std::cerr << "[MIR2LLVM]       Block " << block.id << " not in blocks map, skipping\n";
        if (cm::debug::g_debug_mode) {
            debug_msg("CODEGEN",
                      "Warning: BB " + std::to_string(block.id) + " not in blocks map, skipping");
        }
        return;
    }

    // CompilationGuardによるブロックレベル監視
    auto& guard = get_compilation_guard();
    std::string block_name = "BB" + std::to_string(block.id);
    ScopedBlockGuard block_guard(currentMIRFunction ? currentMIRFunction->name : "unknown",
                                 block_name);

    // ステートメント処理
    // デバッグ: main::bb0のステートメントを確認
    if (cm::debug::g_debug_mode && currentMIRFunction && currentMIRFunction->name == "main" &&
        block.id == 0) {
        debug_msg("MIR",
                  "main::bb0 has " + std::to_string(block.statements.size()) + " statements");
        for (size_t j = 0; j < block.statements.size(); j++) {
            if (block.statements[j]->kind == mir::MirStatement::Assign) {
                auto& assign = std::get<mir::MirStatement::AssignData>(block.statements[j]->data);
                debug_msg("MIR", "  Statement " + std::to_string(j) + ": assign to local " +
                                     std::to_string(assign.place.local));
            }
        }
    }

    // std::cerr << "[MIR2LLVM]       Starting statement loop, total statements: "
    // << block.statements.size() << "\n";

    for (size_t stmt_idx = 0; stmt_idx < block.statements.size(); ++stmt_idx) {
        const auto& stmt = block.statements[stmt_idx];

        // ステートメント処理開始のログ
        // std::cerr << "[MIR2LLVM]       Processing statement " << stmt_idx << "/"
        // << block.statements.size() << " (kind=" << static_cast<int>(stmt->kind) << ")\n";

        // 問題のある12個目のステートメントの詳細ログ
        if (currentMIRFunction && currentMIRFunction->name == "main" && stmt_idx == 11) {
            // debug_msg("MIR2LLVM", "WARNING: This is statement 11 that causes infinite loop");
            if (stmt->kind == mir::MirStatement::Assign) {
                auto& assign = std::get<mir::MirStatement::AssignData>(stmt->data);
                // std::cerr << "[MIR2LLVM]       Assign to local " << assign.place.local << "\n";
                if (assign.rvalue) {
                    // std::cerr << "[MIR2LLVM]       Rvalue kind: "
                    // << static_cast<int>(assign.rvalue->kind) << "\n";
                }
            }
        }

        // 各命令の生成を記録（より詳細な情報を含める）
        std::ostringstream inst_str;
        inst_str << "stmt_kind_" << static_cast<int>(stmt->kind);

        // Assign文の場合は、左辺のローカル変数IDも含める
        if (stmt->kind == mir::MirStatement::Assign) {
            auto& assign = std::get<mir::MirStatement::AssignData>(stmt->data);
            inst_str << "_L" << assign.place.local;
        }

        guard.add_instruction(inst_str.str());

        convertStatement(*stmt);

        // std::cerr << "[MIR2LLVM]       Statement " << stmt_idx << " processed successfully\n";
        // std::cerr << "[MIR2LLVM]       About to increment stmt_idx from " << stmt_idx << " to "
        // << (stmt_idx + 1) << "\n";
        // ループの最後の反復かチェック
        if (stmt_idx == block.statements.size() - 1) {
            // debug_msg("MIR2LLVM", "This was the LAST statement, about to exit loop");
            // std::cerr << "[MIR2LLVM]       Exiting for loop iteration " << stmt_idx << "\n";
        }
        // std::cerr << "[MIR2LLVM]       End of for loop body for stmt_idx=" << stmt_idx << "\n";
    }

    // debug_msg("MIR2LLVM", "FOR LOOP EXITED - All statements processed, checking terminator...");

    // ターミネータ処理
    if (block.terminator) {
        // std::cerr << "[MIR2LLVM]       Terminator exists, processing terminator (kind="
        // << static_cast<int>(block.terminator->kind) << ")\n";

        // ターミネータの生成を記録（より詳細な情報を含める）
        std::ostringstream term_str;
        term_str << "term_kind_" << static_cast<int>(block.terminator->kind);

        // Call terminatorの場合は関数名も含める
        if (block.terminator->kind == mir::MirTerminator::Call) {
            auto& callData = std::get<mir::MirTerminator::CallData>(block.terminator->data);
            if (callData.func) {
                if (callData.func->kind == mir::MirOperand::FunctionRef) {
                    term_str << "_" << std::get<std::string>(callData.func->data);
                } else if (callData.func->kind == mir::MirOperand::Constant) {
                    auto& constant = std::get<mir::MirConstant>(callData.func->data);
                    if (auto* name = std::get_if<std::string>(&constant.value)) {
                        term_str << "_" << *name;
                    }
                }
            }
        }

        guard.add_instruction(term_str.str());

        convertTerminator(*block.terminator);
    } else {
        if (cm::debug::g_debug_mode) {
            debug_msg("CODEGEN",
                      "ERROR: BB " + std::to_string(block.id) + " has no terminator in MIR!");
        }
    }
}

// ステートメント変換
void MIRToLLVM::convertStatement(const mir::MirStatement& stmt) {
    // 無限ループ検出用のカウンタ
    static thread_local std::unordered_map<const mir::MirStatement*, int> statementProcessCount;
    static thread_local const mir::MirFunction* lastFunction = nullptr;

    // 新しい関数に入った場合はカウンタをリセット
    if (currentMIRFunction != lastFunction) {
        statementProcessCount.clear();
        lastFunction = currentMIRFunction;
    }

    // 同じステートメントが過度に処理されている場合はエラー
    auto& count = statementProcessCount[&stmt];
    count++;
    if (count > 100) {
        // std::cerr << "[MIR2LLVM] ERROR: Infinite loop detected! Statement at address " << &stmt
        // << " processed " << count << " times\n";
        if (currentMIRFunction) {
            // std::cerr << "[MIR2LLVM] Function: " << currentMIRFunction->name << "\n";
        }
        throw std::runtime_error("Infinite loop detected in convertStatement");
    }

    if (cm::debug::g_debug_mode && currentMIRFunction && currentMIRFunction->name == "main") {
        debug_msg("MIR",
                  "Processing statement kind: " + std::to_string(static_cast<int>(stmt.kind)));
    }
    switch (stmt.kind) {
        case mir::MirStatement::Assign: {
            auto& assign = std::get<mir::MirStatement::AssignData>(stmt.data);
            // デバッグ: main関数での代入を詳しく見る
            if (cm::debug::g_debug_mode && currentMIRFunction &&
                currentMIRFunction->name == "main") {
                std::string msg =
                    "Assignment to local " + std::to_string(assign.place.local) + ", rvalue kind: ";
                if (assign.rvalue->kind == mir::MirRvalue::Use) {
                    auto& useData = std::get<mir::MirRvalue::UseData>(assign.rvalue->data);
                    if (useData.operand) {
                        if (useData.operand->kind == mir::MirOperand::Copy) {
                            auto& place = std::get<mir::MirPlace>(useData.operand->data);
                            msg += "Copy from local " + std::to_string(place.local);
                        } else if (useData.operand->kind == mir::MirOperand::FunctionRef) {
                            auto& funcName = std::get<std::string>(useData.operand->data);
                            msg += "FunctionRef '" + funcName + "'";
                        } else {
                            msg += "Other operand type";
                        }
                    }
                } else {
                    msg += "Not Use";
                }
                debug_msg("MIR", msg);
            }
            auto rvalue = convertRvalue(*assign.rvalue);
            if (rvalue) {
                // 関数参照の特別処理
                bool isFunctionValue = false;
                if (llvm::isa<llvm::Function>(rvalue)) {
                    // Function*の場合、直接localsに格納（allocaせずにSSA形式で扱う）
                    locals[assign.place.local] = rvalue;
                    // 確認: 実際に格納されたか
                    if (cm::debug::g_debug_mode && currentMIRFunction &&
                        currentMIRFunction->name == "main") {
                        auto func = llvm::cast<llvm::Function>(rvalue);
                        debug_msg("MIR", "Stored function '" + func->getName().str() +
                                             "' to local " + std::to_string(assign.place.local) +
                                             " (locals.size=" + std::to_string(locals.size()) +
                                             ")");
                        // local 2の状態を確認
                        if (locals.size() > 2 && locals[2]) {
                            debug_msg("MIR", "Local 2 is now: " +
                                                 std::string(llvm::isa<llvm::Function>(locals[2])
                                                                 ? "Function"
                                                                 : "Other"));
                        }
                    }
                    break;
                }

                // projectionsがある場合（構造体フィールドなど）は常にstoreが必要
                bool hasProjections = !assign.place.projections.empty();

                // allocaされた変数かどうかをチェック
                bool isAllocated = allocatedLocals.count(assign.place.local) > 0;

                // 関数ポインタのSSA形式での代入も処理
                if (!hasProjections && !isAllocated) {
                    // SSA形式の変数への代入（関数ポインタ等）
                    locals[assign.place.local] = rvalue;
                    break;
                }

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
                        hir::TypePtr derefTargetType = nullptr;
                        for (const auto& proj : assign.place.projections) {
                            if (proj.kind == mir::ProjectionKind::Deref) {
                                hasDeref = true;
                                break;
                            }
                        }

                        if (hasDeref && currentMIRFunction &&
                            assign.place.local < currentMIRFunction->locals.size()) {
                            auto& local = currentMIRFunction->locals[assign.place.local];
                            // プロジェクションチェーンを辿って最終的なターゲット型を取得
                            hir::TypePtr currentType = local.type;
                            for (const auto& proj : assign.place.projections) {
                                if (!currentType)
                                    break;
                                if (proj.kind == mir::ProjectionKind::Deref) {
                                    if (currentType->kind == hir::TypeKind::Pointer &&
                                        currentType->element_type) {
                                        currentType = currentType->element_type;
                                    }
                                } else if (proj.kind == mir::ProjectionKind::Field) {
                                    if (currentType->kind == hir::TypeKind::Struct) {
                                        auto structIt = structDefs.find(currentType->name);
                                        if (structIt != structDefs.end() &&
                                            proj.field_id < structIt->second->fields.size()) {
                                            currentType =
                                                structIt->second->fields[proj.field_id].type;
                                        }
                                    }
                                }
                            }
                            // 最終的な型（フィールドの型など）を使用
                            if (currentType) {
                                targetType = convertType(currentType);
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
                            // LLVM 14+: opaque pointersではポインタ間のBitCastは不要
                            // すべてのポインタは単に ptr 型
                            else if (sourceType->isPointerTy() && targetType->isPointerTy()) {
                                // opaque pointersでは何もしない
                                // rvalueはそのまま使用可能
                            }
                        }

                        // LLVM 14+対応: ポインタ型の場合は、すべてopaque pointerとして扱う
                        // 型検証を追加して安全にstore
                        if (addr && rvalue) {
                            // Deref時: アドレスをターゲット型のポインタにbitcast
                            // LLVM 14 typed pointers
                            // modeでは、store先のポインタ型とrvalueの型が一致する必要がある
                            if (hasDeref && targetType && !targetType->isPointerTy()) {
                                auto targetPtrType = llvm::PointerType::get(targetType, 0);
                                if (addr->getType() != targetPtrType) {
                                    addr = builder->CreateBitCast(addr, targetPtrType, "typed_ptr");
                                }
                            }
                            // LLVM 14+: opaque pointerでは型情報をMIRから取得する必要がある
                            // 正しい型でストアするため、必要に応じてキャストを行う

                            // MIRの型情報からstore先の型を取得
                            hir::TypePtr targetType = nullptr;

                            // Placeから型情報を取得
                            if (assign.place.local < currentMIRFunction->locals.size()) {
                                auto& local = currentMIRFunction->locals[assign.place.local];
                                targetType = local.type;

                                // プロジェクションがある場合、最終的な型を取得
                                for (const auto& proj : assign.place.projections) {
                                    if (!targetType)
                                        break;

                                    switch (proj.kind) {
                                        case mir::ProjectionKind::Field:
                                            if (targetType->kind == hir::TypeKind::Struct) {
                                                auto structDefIt =
                                                    structDefs.find(targetType->name);
                                                if (structDefIt != structDefs.end() &&
                                                    proj.field_id <
                                                        structDefIt->second->fields.size()) {
                                                    targetType =
                                                        structDefIt->second->fields[proj.field_id]
                                                            .type;
                                                }
                                            }
                                            break;
                                        case mir::ProjectionKind::Deref:
                                            targetType = targetType->element_type;
                                            break;
                                        case mir::ProjectionKind::Index:
                                            targetType = targetType->element_type;
                                            break;
                                    }
                                }
                            }

                            // ポインタ型同士の場合、型が異なればキャストを行う
                            if (targetType && targetType->kind == hir::TypeKind::Pointer &&
                                rvalue->getType()->isPointerTy()) {
                                // allocaされたポインタ変数にポインタ値を格納する場合
                                // addrは i8** 型、rvalueは具体的なポインタ型（例：i32*）
                                // rvalueを i8* にビットキャストしてから格納
                                if (rvalue->getType() != ctx.getPtrType()) {
                                    rvalue = builder->CreateBitCast(rvalue, ctx.getPtrType(),
                                                                    "ptr_cast");
                                }
                            }

                            // Store操作を実行
                            builder->CreateStore(rvalue, addr);
                        }
                    } else {
                        // addr取得失敗: フォールバックとしてlocalsに直接格納（SSA形式）
                        locals[assign.place.local] = rvalue;
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

    // 関数終了のデバッグ
    count--;
    // std::cerr << "[MIR2LLVM]         convertStatement EXITING (depth=" << count
    // << ", kind=" << static_cast<int>(stmt.kind) << ")\n";
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
            // std::cerr << "[MIR2LLVM]         Converting BinaryOp, op=" <<
            // static_cast<int>(binop.op)
            // << "\n";

            // debug_msg("MIR2LLVM", "Converting LHS operand...");
            auto lhs = convertOperand(*binop.lhs);
            if (!lhs) {
                // debug_msg("MIR2LLVM", "ERROR: Failed to convert LHS operand");
                return nullptr;
            }
            // debug_msg("MIR2LLVM", "LHS operand converted successfully");

            // debug_msg("MIR2LLVM", "Converting RHS operand...");
            auto rhs = convertOperand(*binop.rhs);
            if (!rhs) {
                // debug_msg("MIR2LLVM", "ERROR: Failed to convert RHS operand");
                return nullptr;
            }
            // debug_msg("MIR2LLVM", "RHS operand converted successfully");

            // debug_msg("MIR2LLVM", "Calling convertBinaryOp...");
            auto result = convertBinaryOp(binop.op, lhs, rhs, binop.result_type);
            // debug_msg("MIR2LLVM", "BinaryOp converted successfully");
            return result;
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
                                // LLVM 14: typed pointers require bitcast
#if LLVM_VERSION_MAJOR < 15
                                auto structPtrType = llvm::PointerType::get(it->second, 0);
                                if (basePtr->getType() != structPtrType) {
                                    basePtr = builder->CreateBitCast(basePtr, structPtrType,
                                                                     "struct_ptr_cast");
                                }
#endif
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

            // LLVM 14+: opaque pointersではポインタ間のBitCast不要
            if (sourceType->isPointerTy() && targetType->isPointerTy()) {
                // opaque pointersでは全てのポインタは ptr 型なので変換不要
                return value;
            }

            // int <-> ポインタ変換
            if (sourceType->isIntegerTy() && targetType->isPointerTy()) {
                return builder->CreateIntToPtr(value, targetType, "inttoptr");
            }
            if (sourceType->isPointerTy() && targetType->isIntegerTy()) {
                return builder->CreatePtrToInt(value, targetType, "ptrtoint");
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
    // 再帰深度の追跡と制限
    static thread_local int recursion_depth = 0;
    static thread_local std::unordered_set<const mir::MirOperand*> processing;

    // 最大再帰深度の制限
    const int MAX_RECURSION_DEPTH = 100;

    // 循環参照の検出
    if (processing.count(&operand) > 0) {
        // debug_msg("MIR2LLVM", "ERROR: Circular reference detected in convertOperand");
        // std::cerr << "[MIR2LLVM]        Operand kind: " << static_cast<int>(operand.kind) <<
        // "\n";
        if (operand.kind == mir::MirOperand::Copy || operand.kind == mir::MirOperand::Move) {
            auto& place = std::get<mir::MirPlace>(operand.data);
            // std::cerr << "[MIR2LLVM]        Place local: " << place.local << "\n";
        }
        return llvm::UndefValue::get(ctx.getI64Type());
    }

    if (recursion_depth >= MAX_RECURSION_DEPTH) {
        // debug_msg("MIR2LLVM", "ERROR: Maximum recursion depth exceeded in convertOperand");
        // std::cerr << "[MIR2LLVM]        Current depth: " << recursion_depth << "\n";
        // std::cerr << "[MIR2LLVM]        Operand kind: " << static_cast<int>(operand.kind) <<
        // "\n";
        return llvm::UndefValue::get(ctx.getI64Type());
    }

    // RAII for tracking recursion depth and processing set
    struct RecursionGuard {
        int& depth;
        std::unordered_set<const mir::MirOperand*>& set;
        const mir::MirOperand* op;
        RecursionGuard(int& d, std::unordered_set<const mir::MirOperand*>& s,
                       const mir::MirOperand* o)
            : depth(d), set(s), op(o) {
            depth++;
            set.insert(op);
        }
        ~RecursionGuard() {
            depth--;
            set.erase(op);
        }
    };

    RecursionGuard guard(recursion_depth, processing, &operand);

    // // debug_msg("MIR2LLVM", "convertOperand called");

    switch (operand.kind) {
        case mir::MirOperand::Copy:
        case mir::MirOperand::Move: {
            auto& place = std::get<mir::MirPlace>(operand.data);
            // // debug_msg("MIR2LLVM", "Place operand");

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
                    // LLVM 14+: opaque pointersではBitCast不要
                    const auto& lastProj = place.projections.back();
                    if (lastProj.kind == mir::ProjectionKind::Deref && fieldType) {
                        // opaque pointersでは全てのポインタはptr型なので何もしない
                        // addrはそのまま使用可能
                    }

                    return builder->CreateLoad(fieldType, addr, "field_load");
                }
                return nullptr;
            }

            // 通常のローカル変数
            auto local = place.local;
            auto val = locals[local];
            // デバッグ: main関数でのコピー操作を確認
            if (cm::debug::g_debug_mode && currentMIRFunction &&
                currentMIRFunction->name == "main" && local <= 2) {
                if (val) {
                    if (llvm::isa<llvm::Function>(val)) {
                        auto func = llvm::cast<llvm::Function>(val);
                        debug_msg("MIR", "Copying function '" + func->getName().str() +
                                             "' from local " + std::to_string(local));
                    } else {
                        debug_msg("MIR", "Copying non-function from local " +
                                             std::to_string(local) + " (type: " +
                                             std::to_string(val->getType()->getTypeID()) + ")");
                    }
                } else {
                    debug_msg("MIR",
                              "Local " + std::to_string(local) + " is null when trying to copy!");
                }
            }
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
            // 関数参照: 関数ポインタとして使える値を返す
            const std::string& funcName = std::get<std::string>(operand.data);
            // まずfunctionsマップから検索
            auto it = functions.find(funcName);
            if (it != functions.end() && it->second) {
                return it->second;
            }
            // 見つからない場合はモジュールから検索
            auto func = module->getFunction(funcName);
            if (func) {
                // 関数を値として使うために、関数のアドレスをポインタとして返す
                // LLVM 14+では、Function*自体が値として使える
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
                        // LoadInst（デリファレンス後）の場合
                        // Deref後は currentType が構造体型になっているはず
                        if (currentType && currentType->kind == hir::TypeKind::Struct) {
                            auto it = structTypes.find(currentType->name);
                            if (it != structTypes.end()) {
                                structType = it->second;
                            }
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

                // LLVM 14: typed pointers require matching pointer types for GEP
                // If addr is i8* but we need StructType*, bitcast first
#if LLVM_VERSION_MAJOR < 15
                auto structPtrType = llvm::PointerType::get(structType, 0);
                if (addr->getType() != structPtrType) {
                    addr = builder->CreateBitCast(addr, structPtrType, "struct_ptr_cast");
                }
#endif

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
                // LLVM 14+では CreateLoad の第1引数はロードする値の型（pointee type）を指定
                // ポインタからポインタをロードするため、ポインタ型そのものを指定
                llvm::Type* ptrType = ctx.getPtrType();  // ptr型（opaque pointer）

                addr = builder->CreateLoad(ptrType, addr);

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
