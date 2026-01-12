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

// 構造体がABI上「小さい」かどうかをチェック（値渡し可能かどうか）
// System V ABI: 16バイト以下の構造体はレジスタで値渡し
bool MIRToLLVM::isSmallStruct(const hir::TypePtr& type) const {
    if (!type || type->kind != hir::TypeKind::Struct) {
        return false;
    }

    // 構造体定義を取得
    auto it = structDefs.find(type->name);
    if (it == structDefs.end()) {
        return false;  // 定義が見つからない場合は安全のためポインタ渡し
    }

    const mir::MirStruct* structDef = it->second;

    // フィールドのサイズを合計
    size_t totalSize = 0;
    for (const auto& field : structDef->fields) {
        if (!field.type)
            continue;

        switch (field.type->kind) {
            case hir::TypeKind::Bool:
            case hir::TypeKind::Char:
            case hir::TypeKind::Tiny:
            case hir::TypeKind::UTiny:
                totalSize += 1;
                break;
            case hir::TypeKind::Short:
            case hir::TypeKind::UShort:
                totalSize += 2;
                break;
            case hir::TypeKind::Int:
            case hir::TypeKind::UInt:
            case hir::TypeKind::Float:
                totalSize += 4;
                break;
            case hir::TypeKind::Long:
            case hir::TypeKind::ULong:
            case hir::TypeKind::Double:
            case hir::TypeKind::Pointer:
            case hir::TypeKind::String:
                totalSize += 8;
                break;
            case hir::TypeKind::Struct:
                // ネストした構造体は安全のためポインタ渡し
                return false;
            default:
                totalSize += 8;  // デフォルトはポインタサイズ
                break;
        }

        // 16バイトを超えたら即座にfalse
        if (totalSize > 16) {
            return false;
        }
    }

    return totalSize <= 16;
}

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
                    // 構造体の場合、ABIに従って値渡しかポインタ渡しを決定
                    if (local.type->kind == hir::TypeKind::Struct) {
                        if (isSmallStruct(local.type)) {
                            // 16バイト以下: 値渡し（System V ABI準拠）
                            paramTypes.push_back(llvmType);
                        } else {
                            // 16バイト超: ポインタ渡し
                            paramTypes.push_back(ctx.getPtrType());
                        }
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

    // main関数には以前optnone属性を追加していたが、
    // これはパフォーマンス上重大な問題を引き起こす（バブルソート1000要素が終わらない等）
    // control flowの問題は別途対処する必要がある
    // if (func.name == "main") {
    //     llvmFunc->addFnAttr(llvm::Attribute::NoInline);
    //     llvmFunc->addFnAttr(llvm::Attribute::OptimizeNone);
    // }

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
    //           << " functions\n";

    currentProgram = &program;

    // ターゲット判定をキャッシュ（境界チェックで使用）
    std::string triple = module->getTargetTriple();
    isWasmTarget = triple.find("wasm") != std::string::npos;

    // インターフェース名を収集
    // std::cerr << "[MIR2LLVM] Collecting interfaces (" << program.interfaces.size() << ")...\n";
    size_t iface_count = 0;
    const size_t MAX_INTERFACES = 10000;  // 無限ループ防止
    for (const auto& iface : program.interfaces) {
        if (++iface_count > MAX_INTERFACES) {
            throw std::runtime_error("Too many interfaces in MIR program");
        }
        if (iface) {
            // std::cerr << "[MIR2LLVM]   Interface: " << iface->name << "\n";
            interfaceNames.insert(iface->name);
        }
    }
    // std::cerr << "[MIR2LLVM] Interfaces collected\n";

    // 構造体型を先に定義（2パスアプローチ）
    // パス1: 全ての構造体をopaque型として作成
    // std::cerr << "[MIR2LLVM] Pass 1: Creating struct types (" << program.structs.size() <<
    // ")...\n";
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
    // std::cerr << "[MIR2LLVM] Pass 2: Setting struct bodies...\n";
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
    // std::cerr << "[MIR2LLVM] Creating interface fat pointer types...\n";
    for (const auto& iface : program.interfaces) {
        if (iface) {
            getInterfaceFatPtrType(iface->name);
        }
    }

    // 関数宣言（先に全て宣言）- vtable生成前に必要
    // 重複した関数はスキップ
    // std::cerr << "[MIR2LLVM] Declaring function signatures...\n";
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
    // std::cerr << "[MIR2LLVM] Generating vtables...\n";
    generateVTables(program);

    // 関数実装
    // 重複した関数はスキップ
    declaredFunctions.clear();
    // std::cerr << "[MIR2LLVM] Converting " << program.functions.size()
    //           << " function implementations...\n";
    for (size_t i = 0; i < program.functions.size(); ++i) {
        const auto& func = program.functions[i];
        auto funcId = generateFunctionId(*func);
        // std::cerr << "[MIR2LLVM] [" << (i + 1) << "/" << program.functions.size()
        //           << "] Converting function: " << funcId << "\n";
        if (declaredFunctions.count(funcId) > 0) {
            // std::cerr << "[MIR2LLVM]   -> Skipping duplicate\n";
            continue;
        }
        declaredFunctions.insert(funcId);
        convertFunction(*func);
        // std::cerr << "[MIR2LLVM]   -> Done converting " << funcId << "\n";
    }

    // std::cerr << "[MIR2LLVM] Conversion complete!\n";
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

        // ランタイム関数（cm_*）はスキップ
        // これらはランタイムライブラリで実装されている
        if (func.name.find("cm_print") == 0 || func.name.find("cm_println") == 0 ||
            func.name.find("cm_int_to_string") == 0 || func.name.find("cm_uint_to_string") == 0 ||
            func.name.find("cm_double_to_string") == 0 ||
            func.name.find("cm_float_to_string") == 0 || func.name.find("cm_bool_to_string") == 0 ||
            func.name.find("cm_char_to_string") == 0 || func.name.find("cm_string_concat") == 0 ||
            func.name.find("cm_file_") == 0 || func.name.find("cm_read_") == 0) {
            return;
        }

        // 本体がない関数（extern関数）は宣言のみで本体を生成しない
        if (func.basic_blocks.empty()) {
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
        // NOTE: heapAllocatedLocalsはベアメタル対応のため削除（malloc不使用）

        // エントリーブロック作成
        auto entryBB = llvm::BasicBlock::Create(ctx.getContext(), "entry", currentFunction);
        builder->SetInsertPoint(entryBB);

        // パラメータをローカル変数にマップ
        size_t argIdx = 0;
        for (auto& arg : currentFunction->args()) {
            if (argIdx < func.arg_locals.size()) {
                auto localIdx = func.arg_locals[argIdx];
                // 構造体の値渡しパラメータの場合、allocaに格納してポインタとして使用
                // （C ABIで16バイト以下の構造体はレジスタ渡しされる）
                if (arg.getType()->isStructTy()) {
                    auto alloca = builder->CreateAlloca(arg.getType(), nullptr,
                                                        "arg_" + std::to_string(argIdx) + "_stack");
                    builder->CreateStore(&arg, alloca);
                    locals[localIdx] = alloca;
                    allocatedLocals.insert(localIdx);  // allocaを追跡
                } else if (arg.getType()->isPointerTy() && argIdx == 0 &&
                           localIdx < func.locals.size()) {
                    // プリミティブ型implメソッドのself引数: i8*で渡されるがローカルはプリミティブ型
                    // 例: int__abs(i8* %arg0) で selfはint型
                    auto& localType = func.locals[localIdx].type;
                    if (localType && (localType->kind == hir::TypeKind::Int ||
                                      localType->kind == hir::TypeKind::UInt ||
                                      localType->kind == hir::TypeKind::Long ||
                                      localType->kind == hir::TypeKind::ULong ||
                                      localType->kind == hir::TypeKind::Float ||
                                      localType->kind == hir::TypeKind::Double ||
                                      localType->kind == hir::TypeKind::Bool ||
                                      localType->kind == hir::TypeKind::Char)) {
                        // i8*をプリミティブ型のポインタにキャストしてload
                        auto primType = convertType(localType);
                        auto primPtrType = llvm::PointerType::get(primType, 0);
                        auto castedPtr =
                            builder->CreateBitCast(&arg, primPtrType, "prim_self_cast");
                        auto loadedVal = builder->CreateLoad(primType, castedPtr, "prim_self_load");
                        // allocaに格納
                        auto alloca = builder->CreateAlloca(primType, nullptr, "prim_self");
                        builder->CreateStore(loadedVal, alloca);
                        locals[localIdx] = alloca;
                        allocatedLocals.insert(localIdx);
                    } else {
                        locals[localIdx] = &arg;
                    }
                } else {
                    locals[localIdx] = &arg;
                }
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

                    // プリミティブ型へのポインタの場合、一時変数はプリミティブ型として扱う
                    // これは借用selfの値を格納するための一時変数のケース
                    // 注意: impl メソッド（関数名に__を含む）内でのみ適用
                    // また、local_0（self引数）からコピーされる一時変数のみ適用
                    // &result のような通常のアドレス取得には適用しない
                    hir::TypePtr allocType = local.type;
                    bool isPrimitiveImplMethod = (func.name.find("__") != std::string::npos);
                    // 名前が_tで始まる場合は一時変数
                    bool isTempVar =
                        (local.name.size() >= 2 && local.name[0] == '_' && local.name[1] == 't');
                    // さらに、最初の数個のローカル変数（selfのコピー先として使われる）のみに適用
                    // local_0はself引数、local_1/local_2が最初の一時変数として使われることが多い
                    bool isSelfCopyTarget = (i <= 2);
                    if (isPrimitiveImplMethod && isTempVar && isSelfCopyTarget &&
                        local.type->kind == hir::TypeKind::Pointer && local.type->element_type) {
                        auto elemKind = local.type->element_type->kind;
                        if (elemKind == hir::TypeKind::Int || elemKind == hir::TypeKind::UInt ||
                            elemKind == hir::TypeKind::Long || elemKind == hir::TypeKind::ULong ||
                            elemKind == hir::TypeKind::Float || elemKind == hir::TypeKind::Double ||
                            elemKind == hir::TypeKind::Bool || elemKind == hir::TypeKind::Char) {
                            allocType = local.type->element_type;
                        }
                    }

                    auto llvmType = convertType(allocType);

                    // ベアメタル対応: すべての配列はスタックに割り当て（ヒープ不使用）
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
        //           << " blocks\n";
        for (size_t i = 0; i < func.basic_blocks.size(); ++i) {
            // DCEで削除されたブロックはスキップ
            if (!func.basic_blocks[i]) {
                // std::cerr << "[MIR2LLVM]   Block " << i << " is null (DCE removed), skipping\n";
                continue;
            }

            // std::cerr << "[MIR2LLVM]   Converting block " << i << "/" << func.basic_blocks.size()
            //           << "\n";

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
    }

    // blocksはunordered_mapなので、countで存在確認
    // std::cerr << "[MIR2LLVM]       Checking if block " << block.id << " is in blocks map...\n";
    if (blocks.count(block.id) > 0) {
        // std::cerr << "[MIR2LLVM]       Setting insert point for block " << block.id << "\n";
        builder->SetInsertPoint(blocks[block.id]);
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
    //           << block.statements.size() << "\n";

    for (size_t stmt_idx = 0; stmt_idx < block.statements.size(); ++stmt_idx) {
        const auto& stmt = block.statements[stmt_idx];

        // ステートメント処理開始のログ
        // std::cerr << "[MIR2LLVM]       Processing statement " << stmt_idx << "/"
        //           << block.statements.size() << " (kind=" << static_cast<int>(stmt->kind) <<
        //           ")\n";

        // 問題のある12個目のステートメントの詳細ログ
        if (currentMIRFunction && currentMIRFunction->name == "main" && stmt_idx == 11) {
            if (stmt->kind == mir::MirStatement::Assign) {
                auto& assign = std::get<mir::MirStatement::AssignData>(stmt->data);
                // std::cerr << "[MIR2LLVM]       Assign to local " << assign.place.local << "\n";
                if (assign.rvalue) {
                    // std::cerr << "[MIR2LLVM]       Rvalue kind: "
                    //           << static_cast<int>(assign.rvalue->kind) << "\n";
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
            // std::cerr << "[MIR2LLVM]       Exiting for loop iteration " << stmt_idx << "\n";
        }
        // std::cerr << "[MIR2LLVM]       End of for loop body for stmt_idx=" << stmt_idx << "\n";
    }

    // ターミネータ処理
    if (block.terminator) {
        // std::cerr << "[MIR2LLVM]       Terminator exists, processing terminator (kind="
        //           << static_cast<int>(block.terminator->kind) << ")\n";

        // ターミネータの生成を記録（より詳細な情報を含める）
        std::ostringstream term_str;
        term_str << "term_kind_" << static_cast<int>(block.terminator->kind);

        // Call terminatorの場合は関数名も含める
        if (block.terminator->kind == mir::MirTerminator::Call) {
            auto& callData = std::get<mir::MirTerminator::CallData>(block.terminator->data);
            if (callData.func) {
                if (callData.func->kind == mir::MirOperand::FunctionRef) {
                    // std::cerr << "[MIR2LLVM]       Call target: "
                    //           << std::get<std::string>(callData.func->data) << "\n";
                    term_str << "_" << std::get<std::string>(callData.func->data);
                } else if (callData.func->kind == mir::MirOperand::Constant) {
                    auto& constant = std::get<mir::MirConstant>(callData.func->data);
                    if (auto* name = std::get_if<std::string>(&constant.value)) {
                        // std::cerr << "[MIR2LLVM]       Call target (const): " << *name << "\n";
                        term_str << "_" << *name;
                    }
                }
            }
            // std::cerr << "[MIR2LLVM]       Args count: " << callData.args.size() << "\n";
        }

        guard.add_instruction(term_str.str());

        // std::cerr << "[MIR2LLVM]       Calling convertTerminator()...\n";
        convertTerminator(*block.terminator);
        // std::cerr << "[MIR2LLVM]       convertTerminator() done!\n";
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
                // bool isFunctionValue = false;
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
                                        // ジェネリック構造体の場合、型引数を考慮した名前を生成
                                        std::string structLookupName = currentType->name;
                                        if (!currentType->type_args.empty()) {
                                            for (const auto& typeArg : currentType->type_args) {
                                                if (typeArg) {
                                                    structLookupName += "__";
                                                    if (typeArg->kind == hir::TypeKind::Struct) {
                                                        structLookupName += typeArg->name;
                                                    } else {
                                                        switch (typeArg->kind) {
                                                            case hir::TypeKind::Int:
                                                                structLookupName += "int";
                                                                break;
                                                            case hir::TypeKind::UInt:
                                                                structLookupName += "uint";
                                                                break;
                                                            case hir::TypeKind::Long:
                                                                structLookupName += "long";
                                                                break;
                                                            case hir::TypeKind::ULong:
                                                                structLookupName += "ulong";
                                                                break;
                                                            case hir::TypeKind::Float:
                                                                structLookupName += "float";
                                                                break;
                                                            case hir::TypeKind::Double:
                                                                structLookupName += "double";
                                                                break;
                                                            case hir::TypeKind::Bool:
                                                                structLookupName += "bool";
                                                                break;
                                                            case hir::TypeKind::Char:
                                                                structLookupName += "char";
                                                                break;
                                                            case hir::TypeKind::String:
                                                                structLookupName += "string";
                                                                break;
                                                            default:
                                                                if (!typeArg->name.empty()) {
                                                                    structLookupName +=
                                                                        typeArg->name;
                                                                }
                                                                break;
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                        auto structIt = structDefs.find(structLookupName);
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
                            // sourceがポインタで、targetがプリミティブ整数型の場合
                            // (プリミティブ型implメソッドのselfコピー: i8* -> i32)
                            else if (sourceType->isPointerTy() && targetType->isIntegerTy() &&
                                     targetType->getIntegerBitWidth() >= 8) {
                                // i8*をtargetType*にキャストしてload
                                auto targetPtrType = llvm::PointerType::get(targetType, 0);
                                auto castedPtr =
                                    builder->CreateBitCast(rvalue, targetPtrType, "prim_cast");
                                rvalue = builder->CreateLoad(targetType, castedPtr, "prim_load");
                                sourceType = rvalue->getType();
                            }
                            // sourceがポインタで、targetが浮動小数点型の場合
                            else if (sourceType->isPointerTy() && targetType->isFloatingPointTy()) {
                                auto targetPtrType = llvm::PointerType::get(targetType, 0);
                                auto castedPtr =
                                    builder->CreateBitCast(rvalue, targetPtrType, "float_cast");
                                rvalue = builder->CreateLoad(targetType, castedPtr, "float_load");
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
                                                // ジェネリック構造体の場合、型引数を考慮した名前を生成
                                                std::string structLookupName = targetType->name;
                                                if (!targetType->type_args.empty()) {
                                                    for (const auto& typeArg :
                                                         targetType->type_args) {
                                                        if (typeArg) {
                                                            structLookupName += "__";
                                                            if (typeArg->kind ==
                                                                hir::TypeKind::Struct) {
                                                                structLookupName += typeArg->name;
                                                            } else {
                                                                switch (typeArg->kind) {
                                                                    case hir::TypeKind::Int:
                                                                        structLookupName += "int";
                                                                        break;
                                                                    case hir::TypeKind::UInt:
                                                                        structLookupName += "uint";
                                                                        break;
                                                                    case hir::TypeKind::Long:
                                                                        structLookupName += "long";
                                                                        break;
                                                                    case hir::TypeKind::ULong:
                                                                        structLookupName += "ulong";
                                                                        break;
                                                                    case hir::TypeKind::Float:
                                                                        structLookupName += "float";
                                                                        break;
                                                                    case hir::TypeKind::Double:
                                                                        structLookupName +=
                                                                            "double";
                                                                        break;
                                                                    case hir::TypeKind::Bool:
                                                                        structLookupName += "bool";
                                                                        break;
                                                                    case hir::TypeKind::Char:
                                                                        structLookupName += "char";
                                                                        break;
                                                                    case hir::TypeKind::String:
                                                                        structLookupName +=
                                                                            "string";
                                                                        break;
                                                                    default:
                                                                        if (!typeArg->name
                                                                                 .empty()) {
                                                                            structLookupName +=
                                                                                typeArg->name;
                                                                        }
                                                                        break;
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                                auto structDefIt =
                                                    structDefs.find(structLookupName);
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

            auto lhs = convertOperand(*binop.lhs);
            if (!lhs) {
                return nullptr;
            }

            auto rhs = convertOperand(*binop.rhs);
            if (!rhs) {
                return nullptr;
            }

            auto result = convertBinaryOp(binop.op, lhs, rhs, binop.result_type);
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
                            // ジェネリック構造体の場合、型引数を考慮した名前を生成
                            std::string structLookupName = structType->name;
                            if (!structType->type_args.empty()) {
                                for (const auto& typeArg : structType->type_args) {
                                    if (typeArg) {
                                        structLookupName += "__";
                                        if (typeArg->kind == hir::TypeKind::Struct) {
                                            structLookupName += typeArg->name;
                                        } else {
                                            switch (typeArg->kind) {
                                                case hir::TypeKind::Int:
                                                    structLookupName += "int";
                                                    break;
                                                case hir::TypeKind::UInt:
                                                    structLookupName += "uint";
                                                    break;
                                                case hir::TypeKind::Long:
                                                    structLookupName += "long";
                                                    break;
                                                case hir::TypeKind::ULong:
                                                    structLookupName += "ulong";
                                                    break;
                                                case hir::TypeKind::Float:
                                                    structLookupName += "float";
                                                    break;
                                                case hir::TypeKind::Double:
                                                    structLookupName += "double";
                                                    break;
                                                case hir::TypeKind::Bool:
                                                    structLookupName += "bool";
                                                    break;
                                                case hir::TypeKind::Char:
                                                    structLookupName += "char";
                                                    break;
                                                case hir::TypeKind::String:
                                                    structLookupName += "string";
                                                    break;
                                                default:
                                                    if (!typeArg->name.empty()) {
                                                        structLookupName += typeArg->name;
                                                    }
                                                    break;
                                            }
                                        }
                                    }
                                }
                            }
                            auto it = structTypes.find(structLookupName);
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
        case mir::MirRvalue::Aggregate: {
            // 集約型（構造体、配列、タプル）の構築
            auto& aggData = std::get<mir::MirRvalue::AggregateData>(rvalue.data);

            if (aggData.kind.type == mir::AggregateKind::Type::Struct) {
                // 構造体型を取得
                std::string structName = aggData.kind.name;

                // 型情報から型引数を取得してマングリング
                if (aggData.kind.ty && !aggData.kind.ty->type_args.empty()) {
                    for (const auto& typeArg : aggData.kind.ty->type_args) {
                        if (typeArg) {
                            structName += "__";
                            switch (typeArg->kind) {
                                case hir::TypeKind::Int:
                                    structName += "int";
                                    break;
                                case hir::TypeKind::UInt:
                                    structName += "uint";
                                    break;
                                case hir::TypeKind::Long:
                                    structName += "long";
                                    break;
                                case hir::TypeKind::ULong:
                                    structName += "ulong";
                                    break;
                                case hir::TypeKind::Float:
                                    structName += "float";
                                    break;
                                case hir::TypeKind::Double:
                                    structName += "double";
                                    break;
                                case hir::TypeKind::Bool:
                                    structName += "bool";
                                    break;
                                case hir::TypeKind::Char:
                                    structName += "char";
                                    break;
                                case hir::TypeKind::Struct:
                                    structName += typeArg->name;
                                    break;
                                default:
                                    structName += "unknown";
                                    break;
                            }
                        }
                    }
                }

                auto it = structTypes.find(structName);
                if (it == structTypes.end()) {
                    // マングリングなしでも試す
                    it = structTypes.find(aggData.kind.name);
                }

                if (it == structTypes.end() || !it->second) {
                    // 構造体型が見つからない
                    return nullptr;
                }

                auto* structType = it->second;

                // 一時変数を作成してフィールドを初期化
                auto* alloca = builder->CreateAlloca(structType, nullptr, "agg_temp");

                // 各フィールドを初期化
                for (size_t i = 0; i < aggData.operands.size(); ++i) {
                    if (aggData.operands[i]) {
                        auto* fieldValue = convertOperand(*aggData.operands[i]);
                        if (fieldValue) {
                            auto* gep =
                                builder->CreateStructGEP(structType, alloca, i, "agg_field");
                            builder->CreateStore(fieldValue, gep);
                        }
                    }
                }

                // 構造体値をロードして返す
                return builder->CreateLoad(structType, alloca, "agg_load");
            } else if (aggData.kind.type == mir::AggregateKind::Type::Array) {
                // 配列の処理
                if (aggData.operands.empty() || !aggData.operands[0]) {
                    return nullptr;
                }

                // 最初の要素から型を推定
                auto* firstElem = convertOperand(*aggData.operands[0]);
                if (!firstElem) {
                    return nullptr;
                }

                auto* elemType = firstElem->getType();
                auto* arrayType = llvm::ArrayType::get(elemType, aggData.operands.size());
                auto* alloca = builder->CreateAlloca(arrayType, nullptr, "arr_temp");

                // 各要素を初期化
                for (size_t i = 0; i < aggData.operands.size(); ++i) {
                    if (aggData.operands[i]) {
                        auto* elemValue = convertOperand(*aggData.operands[i]);
                        if (elemValue) {
                            auto* gep =
                                builder->CreateGEP(arrayType, alloca,
                                                   {llvm::ConstantInt::get(ctx.getI32Type(), 0),
                                                    llvm::ConstantInt::get(ctx.getI32Type(), i)},
                                                   "arr_elem");
                            builder->CreateStore(elemValue, gep);
                        }
                    }
                }

                return builder->CreateLoad(arrayType, alloca, "arr_load");
            }

            return nullptr;
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
        // std::cerr << "[MIR2LLVM]        Operand kind: " << static_cast<int>(operand.kind) <<
        // "\n";
        if (operand.kind == mir::MirOperand::Copy || operand.kind == mir::MirOperand::Move) {
            // auto& place = std::get<mir::MirPlace>(operand.data);
            // std::cerr << "[MIR2LLVM]        Place local: " << place.local << "\n";
        }
        return llvm::UndefValue::get(ctx.getI64Type());
    }

    if (recursion_depth >= MAX_RECURSION_DEPTH) {
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
                            // Generic型もStructとして扱う（モノモーフィック化後の型）
                            if (currentType->kind == hir::TypeKind::Struct ||
                                currentType->kind == hir::TypeKind::Generic) {
                                std::string lookupName = currentType->name;
                                auto structIt = structDefs.find(lookupName);

                                // フォールバック: 関数名から構造体名を推論
                                // Container__int__get → Container__int
                                if (structIt == structDefs.end() && currentMIRFunction) {
                                    const auto& funcName = currentMIRFunction->name;
                                    size_t lastDunder = funcName.rfind("__");
                                    if (lastDunder != std::string::npos && lastDunder > 0) {
                                        std::string inferredStruct = funcName.substr(0, lastDunder);
                                        structIt = structDefs.find(inferredStruct);
                                        if (structIt != structDefs.end()) {
                                            lookupName = inferredStruct;
                                        }
                                    }
                                }

                                if (structIt != structDefs.end()) {
                                    auto& fields = structIt->second->fields;
                                    if (proj.field_id < fields.size()) {
                                        currentType = fields[proj.field_id].type;

                                        // フィールド型がジェネリックパラメータ(T等)の場合、マングリング名から具体型に置換
                                        // 例: Box__intのvalue: T -> int
                                        if (currentType && currentType->name.length() <= 2 &&
                                            !currentType->name.empty()) {
                                            // ベースローカルの型名からマングリング名を抽出
                                            auto& baseLocal =
                                                currentMIRFunction->locals[place.local].type;
                                            std::string mangledName;
                                            if (baseLocal) {
                                                if (!baseLocal->type_args.empty() &&
                                                    baseLocal->type_args[0]) {
                                                    // type_argsがある場合は直接使用
                                                    currentType = baseLocal->type_args[0];
                                                } else if (baseLocal->name.find("__") !=
                                                           std::string::npos) {
                                                    // マングリング名から型引数を抽出 (Box__int ->
                                                    // int)
                                                    mangledName = baseLocal->name;
                                                    size_t pos = mangledName.find("__");
                                                    std::string typeArg =
                                                        mangledName.substr(pos + 2);
                                                    size_t nextPos = typeArg.find("__");
                                                    if (nextPos != std::string::npos) {
                                                        typeArg = typeArg.substr(0, nextPos);
                                                    }
                                                    if (!typeArg.empty()) {
                                                        if (typeArg == "int")
                                                            currentType = hir::make_int();
                                                        else if (typeArg == "uint")
                                                            currentType = hir::make_uint();
                                                        else if (typeArg == "long")
                                                            currentType = hir::make_long();
                                                        else if (typeArg == "ulong")
                                                            currentType = hir::make_ulong();
                                                        else if (typeArg == "float")
                                                            currentType = hir::make_float();
                                                        else if (typeArg == "double")
                                                            currentType = hir::make_double();
                                                        else if (typeArg == "bool")
                                                            currentType = hir::make_bool();
                                                        else if (typeArg == "string")
                                                            currentType = hir::make_string();
                                                        else {
                                                            auto st = std::make_shared<hir::Type>(
                                                                hir::TypeKind::Struct);
                                                            st->name = typeArg;
                                                            currentType = st;
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        } else if (proj.kind == mir::ProjectionKind::Index && currentType) {
                            // 配列型またはポインタ型経由のインデックスアクセス
                            if (currentType->kind == hir::TypeKind::Array &&
                                currentType->element_type) {
                                currentType = currentType->element_type;
                            } else if (currentType->kind == hir::TypeKind::Pointer &&
                                       currentType->element_type) {
                                // ポインタ型経由のインデックスアクセス (ptr[i])
                                currentType = currentType->element_type;
                            }
                        } else if (proj.kind == mir::ProjectionKind::Deref && currentType) {
                            if (currentType->kind == hir::TypeKind::Pointer &&
                                currentType->element_type) {
                                currentType = currentType->element_type;
                            }
                            // プリミティブ型借用selfの場合: MIRでは元の型で記録されているが
                            // Deref後も同じ型を維持（ポインタからプリミティブ値をロード）
                            // currentType がすでにプリミティブ型なら変更不要
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

                    // プリミティブ型借用selfの特別処理:
                    // 最後のプロジェクションがDerefで、addrがArgumentの場合
                    // これはプリミティブ型の借用selfへのアクセス（*self）
                    // 元のローカル型からロード型を決定
                    if (!place.projections.empty()) {
                        const auto& lastProj = place.projections.back();
                        if (lastProj.kind == mir::ProjectionKind::Deref &&
                            llvm::isa<llvm::Argument>(addr)) {
                            // 借用selfへのDeref: 元のMIRローカル型を確認
                            if (currentMIRFunction &&
                                place.local < currentMIRFunction->locals.size()) {
                                auto& localInfo = currentMIRFunction->locals[place.local];
                                if (localInfo.type) {
                                    // Pointer型の場合はelement_typeを、そうでなければ元の型を使用
                                    hir::TypePtr elemType = localInfo.type;
                                    if (localInfo.type->kind == hir::TypeKind::Pointer &&
                                        localInfo.type->element_type) {
                                        elemType = localInfo.type->element_type;
                                    }
                                    // プリミティブ型ならその型でロード
                                    if (elemType->kind == hir::TypeKind::Int ||
                                        elemType->kind == hir::TypeKind::UInt ||
                                        elemType->kind == hir::TypeKind::Long ||
                                        elemType->kind == hir::TypeKind::ULong ||
                                        elemType->kind == hir::TypeKind::Float ||
                                        elemType->kind == hir::TypeKind::Double ||
                                        elemType->kind == hir::TypeKind::Bool ||
                                        elemType->kind == hir::TypeKind::Char) {
                                        fieldType = convertType(elemType);
                                    }
                                }
                            }
                        }
                    }

                    // LLVM 14 typed pointers:
                    // 構造体型をロードする場合、アドレスを正しい型にキャスト
#if LLVM_VERSION_MAJOR < 15
                    if (fieldType->isStructTy()) {
                        auto expectedPtrType = llvm::PointerType::get(fieldType, 0);
                        if (addr->getType() != expectedPtrType) {
                            addr =
                                builder->CreateBitCast(addr, expectedPtrType, "ptr_to_struct_cast");
                        }
                    }
#endif
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

                // プリミティブ借用selfの場合: allocatedTypeがポインタでもMIR型がprimitiveなら
                // ポインタをロードした後、そのポインタからプリミティブ値をロード
                // 注意: impl メソッド内でのみ適用（通常のポインタ変数を壊さないため）
                bool isPrimitiveImplMethod =
                    currentMIRFunction &&
                    (currentMIRFunction->name.find("__") != std::string::npos);

                if (isPrimitiveImplMethod && allocatedType->isPointerTy() && currentMIRFunction &&
                    local < currentMIRFunction->locals.size()) {
                    auto& localInfo = currentMIRFunction->locals[local];
                    // selfローカル（最初の引数ローカル）かどうかをチェック
                    bool isSelfLocal = !currentMIRFunction->arg_locals.empty() &&
                                       local == currentMIRFunction->arg_locals[0];
                    // localNameが"self"の場合もチェック
                    if (!isSelfLocal && localInfo.name == "self") {
                        isSelfLocal = true;
                    }

                    if (isSelfLocal) {
                        // プリミティブ型のimplメソッドか判定
                        // 関数名が int__xxx, long__xxx 等の形式
                        const std::string& funcName = currentMIRFunction->name;
                        size_t dunderPos = funcName.find("__");
                        if (dunderPos != std::string::npos) {
                            std::string typeName = funcName.substr(0, dunderPos);
                            bool isPrimitiveType = typeName == "int" || typeName == "uint" ||
                                                   typeName == "long" || typeName == "ulong" ||
                                                   typeName == "short" || typeName == "ushort" ||
                                                   typeName == "float" || typeName == "double" ||
                                                   typeName == "bool" || typeName == "char";

                            if (isPrimitiveType) {
                                // まずポインタをロード
                                auto ptrVal =
                                    builder->CreateLoad(allocatedType, val, "self_ptr_load");
                                // 次にプリミティブ型を決定してロード
                                llvm::Type* elemType = nullptr;
                                if (typeName == "int" || typeName == "uint") {
                                    elemType = ctx.getI32Type();
                                } else if (typeName == "long" || typeName == "ulong") {
                                    elemType = ctx.getI64Type();
                                } else if (typeName == "short" || typeName == "ushort") {
                                    elemType = ctx.getI16Type();
                                } else if (typeName == "float") {
                                    elemType = ctx.getF32Type();
                                } else if (typeName == "double") {
                                    elemType = ctx.getF64Type();
                                } else if (typeName == "bool" || typeName == "char") {
                                    elemType = ctx.getI8Type();
                                }
                                if (elemType) {
                                    return builder->CreateLoad(elemType, ptrVal,
                                                               "borrowed_self_prim_load");
                                }
                            }
                        }
                    }
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
            // プリミティブ型借用selfの処理:
            // 引数（Argument）でポインタ型で、MIRローカル型がプリミティブの場合
            // ポインタから値をロードして返す
            if (val && llvm::isa<llvm::Argument>(val) && val->getType()->isPointerTy()) {
                if (currentMIRFunction && local < currentMIRFunction->locals.size()) {
                    auto& localInfo = currentMIRFunction->locals[local];
                    if (localInfo.type) {
                        hir::TypePtr elemType = localInfo.type;
                        // Pointer<Primitive>型の場合はelement_typeを取得
                        if (localInfo.type->kind == hir::TypeKind::Pointer &&
                            localInfo.type->element_type) {
                            elemType = localInfo.type->element_type;
                        }
                        // Pointer型だがelement_typeがない場合（借用selfのMIR表現）
                        // 関数の戻り値型を使用してプリミティブ型を判定
                        else if (localInfo.type->kind == hir::TypeKind::Pointer &&
                                 !localInfo.type->element_type) {
                            // 関数の戻り値型を取得（return_localの型を使用）
                            if (currentMIRFunction && currentMIRFunction->return_local <
                                                          currentMIRFunction->locals.size()) {
                                auto& retLocal =
                                    currentMIRFunction->locals[currentMIRFunction->return_local];
                                if (retLocal.type &&
                                    retLocal.type->kind != hir::TypeKind::Pointer) {
                                    elemType = retLocal.type;
                                }
                            }
                        }
                        auto typeKind = elemType->kind;

                        // ローカル変数自体がポインタ型の場合はロードをスキップ
                        // ポインタを格納するlocal_2のような変数はポインタ値をそのまま格納すべき
                        if (localInfo.type->kind == hir::TypeKind::Pointer) {
                            // ポインタ型のローカル変数へはポインタ値をそのまま返す
                            return val;
                        }

                        // プリミティブ型または構造体型の場合はロード
                        bool isPrimitive =
                            typeKind == hir::TypeKind::Int || typeKind == hir::TypeKind::UInt ||
                            typeKind == hir::TypeKind::Long || typeKind == hir::TypeKind::ULong ||
                            typeKind == hir::TypeKind::Short || typeKind == hir::TypeKind::UShort ||
                            typeKind == hir::TypeKind::Float || typeKind == hir::TypeKind::Double ||
                            typeKind == hir::TypeKind::Bool || typeKind == hir::TypeKind::Char;
                        // 構造体型/ジェネリック型もロードが必要
                        bool isStruct =
                            typeKind == hir::TypeKind::Struct || typeKind == hir::TypeKind::Generic;
                        if (isPrimitive || isStruct) {
                            auto loadType = convertType(elemType);
                            return builder->CreateLoad(
                                loadType, val,
                                isPrimitive ? "borrowed_prim_load" : "borrowed_struct_load");
                        }
                    }
                }
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

                // Generic型の場合も構造体として扱う（モノモーフィック化後の型）
                // ポインタ型の場合はelement_typeを使用
                hir::TypePtr targetStructType = currentType;
                if (currentType && currentType->kind == hir::TypeKind::Pointer &&
                    currentType->element_type) {
                    targetStructType = currentType->element_type;
                }
                if (targetStructType && (targetStructType->kind == hir::TypeKind::Struct ||
                                         targetStructType->kind == hir::TypeKind::Generic)) {
                    structName = targetStructType->name;

                    // ジェネリック構造体の場合、型引数を考慮した名前を生成
                    // 例: Node<int> -> Node__int
                    // 既にマングリング済み(__含む)の場合はスキップ
                    if (!targetStructType->type_args.empty() &&
                        structName.find("__") == std::string::npos) {
                        for (const auto& typeArg : targetStructType->type_args) {
                            if (typeArg) {
                                structName += "__";
                                if (typeArg->kind == hir::TypeKind::Struct) {
                                    structName += typeArg->name;
                                } else {
                                    switch (typeArg->kind) {
                                        case hir::TypeKind::Int:
                                            structName += "int";
                                            break;
                                        case hir::TypeKind::UInt:
                                            structName += "uint";
                                            break;
                                        case hir::TypeKind::Long:
                                            structName += "long";
                                            break;
                                        case hir::TypeKind::ULong:
                                            structName += "ulong";
                                            break;
                                        case hir::TypeKind::Float:
                                            structName += "float";
                                            break;
                                        case hir::TypeKind::Double:
                                            structName += "double";
                                            break;
                                        case hir::TypeKind::Bool:
                                            structName += "bool";
                                            break;
                                        case hir::TypeKind::Char:
                                            structName += "char";
                                            break;
                                        case hir::TypeKind::String:
                                            structName += "string";
                                            break;
                                        default:
                                            if (!typeArg->name.empty()) {
                                                structName += typeArg->name;
                                            }
                                            break;
                                    }
                                }
                            }
                        }
                    }

                    auto it = structTypes.find(structName);
                    if (it != structTypes.end()) {
                        structType = it->second;
                    }
                }
                // フォールバック: 関数名から構造体名を推論
                // Container__int__get → Container__int
                if (!structType && currentMIRFunction) {
                    const auto& funcName = currentMIRFunction->name;
                    // 最後の__を見つけて、それより前の部分を構造体名として使う
                    size_t lastDunder = funcName.rfind("__");
                    if (lastDunder != std::string::npos && lastDunder > 0) {
                        std::string inferredStruct = funcName.substr(0, lastDunder);
                        auto it = structTypes.find(inferredStruct);
                        if (it != structTypes.end()) {
                            structType = it->second;
                            structName = inferredStruct;
                        }
                    }
                }

                if (!structType) {
                    // 型情報が取得できない場合は、addrの型から推測
                    if (auto allocaInst = llvm::dyn_cast<llvm::AllocaInst>(addr)) {
                        structType = allocaInst->getAllocatedType();
                    } else if (auto loadInst = llvm::dyn_cast<llvm::LoadInst>(addr)) {
                        (void)loadInst;  // 未使用警告を抑制
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
                // structNameは関数名から推論された可能性があるので、それを使用
                if (!structName.empty()) {
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

                // 配列またはポインタ型を処理
                llvm::Type* arrayType = nullptr;
                llvm::Type* elemType = nullptr;
                bool isPointerAccess = false;

                // まずcurrentTypeを確認
                if (currentType) {
                    if (currentType->kind == hir::TypeKind::Array) {
                        arrayType = convertType(currentType);
                        elemType = convertType(currentType->element_type);
                    } else if (currentType->kind == hir::TypeKind::Pointer &&
                               currentType->element_type) {
                        // ポインタ型経由のインデックスアクセス (ptr[i])
                        elemType = convertType(currentType->element_type);
                        isPointerAccess = true;
                    }
                }

                if (!arrayType && !isPointerAccess) {
                    // フォールバック: place.localの型を使用
                    if (currentMIRFunction && place.local < currentMIRFunction->locals.size()) {
                        auto& local = currentMIRFunction->locals[place.local];
                        if (local.type) {
                            if (local.type->kind == hir::TypeKind::Array) {
                                arrayType = convertType(local.type);
                                elemType = convertType(local.type->element_type);
                            } else if (local.type->kind == hir::TypeKind::Pointer &&
                                       local.type->element_type) {
                                elemType = convertType(local.type->element_type);
                                isPointerAccess = true;
                            }
                        }
                    }
                }

                if (!arrayType && !isPointerAccess) {
                    // 型情報が取得できない場合は、allocaまたはGEPから推測
                    if (auto allocaInst = llvm::dyn_cast<llvm::AllocaInst>(addr)) {
                        auto allocType = allocaInst->getAllocatedType();
                        if (allocType->isArrayTy()) {
                            arrayType = allocType;
                            elemType = allocType->getArrayElementType();
                        } else if (allocType->isPointerTy()) {
                            // ポインタ型のallocaの場合
                            isPointerAccess = true;
                            elemType = allocType;  // opaque pointerの場合は型情報がない
                        }
                    } else if (auto gepInst = llvm::dyn_cast<llvm::GetElementPtrInst>(addr)) {
                        auto resultType = gepInst->getResultElementType();
                        if (resultType && resultType->isArrayTy()) {
                            arrayType = resultType;
                            elemType = resultType->getArrayElementType();
                        }
                    }
                }

                // ポインタ経由のインデックスアクセス
                if (isPointerAccess && elemType) {
                    // ポインタ値をloadしてからGEPでインデックスアクセス
                    llvm::Value* ptrVal = addr;
                    if (auto allocaInst = llvm::dyn_cast<llvm::AllocaInst>(addr)) {
                        // allocaからポインタ値をload
                        // LLVM 14: typed pointerモードでは正しい型でloadする必要がある
                        llvm::Type* allocatedType = allocaInst->getAllocatedType();
                        ptrVal = builder->CreateLoad(allocatedType, addr, "ptr_load");
                    } else if (llvm::isa<llvm::Argument>(addr)) {
                        // 関数引数の場合、すでにポインタ値なのでそのまま使用
                        // loadは不要（SSA形式で渡されている）
                        ptrVal = addr;
                    } else if (addr->getType()->isPointerTy()) {
                        // その他のポインタの場合
                        ptrVal = builder->CreateLoad(llvm::PointerType::get(elemType, 0), addr,
                                                     "ptr_load");
                    }

                    // LLVM 14: typed pointersモードではGEP前にポインタ型をelemTypeのポインタに変換
                    llvm::Type* elemPtrType = llvm::PointerType::get(elemType, 0);
                    if (ptrVal->getType() != elemPtrType) {
                        ptrVal = builder->CreateBitCast(ptrVal, elemPtrType, "ptr_cast");
                    }

                    // GEPでptr + index*sizeof(elem)を計算
                    addr = builder->CreateGEP(elemType, ptrVal, indexVal, "elem_ptr");

                    // 次のプロジェクションのために型を更新
                    if (currentType && currentType->kind == hir::TypeKind::Pointer) {
                        currentType = currentType->element_type;
                    }
                    break;
                }

                if (!arrayType || !arrayType->isArrayTy()) {
                    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMError,
                                            "Cannot determine array type for index access",
                                            cm::debug::Level::Error);
                    return nullptr;
                }

                // 配列アクセスの場合
                if (!elemType) {
                    elemType = arrayType->getArrayElementType();
                }

                // 境界チェック: インデックスが配列サイズ未満かチェック
                // 負のインデックスも符号なし比較で検出（負数は巨大な正数として扱われる）
                uint64_t arraySize = arrayType->getArrayNumElements();
                if (arraySize > 0) {
                    // 配列サイズをi64で取得
                    llvm::Value* sizeVal = llvm::ConstantInt::get(ctx.getI64Type(), arraySize);

                    // インデックスをi64に変換（符号なし）
                    llvm::Value* idxForCheck = indexVal;
                    if (!indexVal->getType()->isIntegerTy(64)) {
                        idxForCheck = builder->CreateZExt(indexVal, ctx.getI64Type(), "idx_zext");
                    }

                    // index < size のチェック（符号なし比較）
                    llvm::Value* inBounds =
                        builder->CreateICmpULT(idxForCheck, sizeVal, "bounds_check");

                    // 条件分岐: 範囲内なら続行、範囲外ならパニック
                    llvm::Function* func = builder->GetInsertBlock()->getParent();
                    llvm::BasicBlock* continueBlock =
                        llvm::BasicBlock::Create(ctx.getModule().getContext(), "bounds_ok", func);
                    llvm::BasicBlock* panicBlock =
                        llvm::BasicBlock::Create(ctx.getModule().getContext(), "bounds_fail", func);

                    builder->CreateCondBr(inBounds, continueBlock, panicBlock);

                    // パニックブロック: 境界外アクセスでパニック
                    builder->SetInsertPoint(panicBlock);

                    // WASMターゲットの場合はabortを呼び出さずunreachableのみ
                    // （abortはWASMランタイムで未定義のため）
                    // 注: isWasmTargetはconvert()で一度だけ計算されたキャッシュ値

                    if (!isWasmTarget) {
                        // ネイティブターゲット: cm_panic関数を呼び出し（ない場合はabortを使用）
                        llvm::Function* panicFn = module->getFunction("cm_panic");
                        if (!panicFn) {
                            panicFn = module->getFunction("abort");
                            if (!panicFn) {
                                llvm::FunctionType* abortType = llvm::FunctionType::get(
                                    llvm::Type::getVoidTy(ctx.getModule().getContext()), false);
                                panicFn = llvm::Function::Create(
                                    abortType, llvm::Function::ExternalLinkage, "abort", module);
                            }
                        }
                        if (panicFn->getFunctionType()->getNumParams() > 0) {
                            // cm_panicはメッセージを取る
                            llvm::Value* msgPtr = builder->CreateGlobalStringPtr(
                                "Array index out of bounds", "bounds_error_msg");
                            builder->CreateCall(panicFn, {msgPtr});
                        } else {
                            builder->CreateCall(panicFn);
                        }
                    }
                    // WASM/ネイティブ共通: unreachableで終了
                    builder->CreateUnreachable();

                    // 続行ブロックに移動
                    builder->SetInsertPoint(continueBlock);
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

                // 重要: 借用selfの場合、addrは既にポインタ値（関数引数として渡された）
                // allocaに格納されていない場合は、追加のロードは不要
                bool needsLoad = true;

                // addrがllvm::Argumentの場合は直接ポインタ値として使用
                // （引数はlocals[arg_local] = &argで直接格納されている）
                if (llvm::isa<llvm::Argument>(addr)) {
                    needsLoad = false;
                    // addrはすでにポインタ値なのでそのまま使用
                }

                if (needsLoad) {
                    llvm::Type* ptrType = ctx.getPtrType();  // ptr型（opaque pointer）
                    addr = builder->CreateLoad(ptrType, addr);
                }

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
