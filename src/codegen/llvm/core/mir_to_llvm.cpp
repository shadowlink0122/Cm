/// @file mir_to_llvm.cpp
/// @brief MIR → LLVM IR 変換器（メインモジュール）

#include "mir_to_llvm.hpp"

#include "../../../common/debug/codegen.hpp"
#include "../monitoring/compilation_guard.hpp"

#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <map>
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
    // main/efi_main関数は特別扱い（エントリポイント）
    if (func.name == "main" || func.name == "efi_main") {
        return func.name;
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
    // main/efi_main関数は特別扱い（エントリポイント）
    if (baseName == "main" || baseName == "efi_main") {
        return baseName;
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

    // UEFIエントリポイント: efi_mainにWin64呼出規約とDLLExportを設定
    // DLLExportにより最適化で関数が除去されるのを防ぐ
    if (func.name == "efi_main") {
        llvmFunc->setCallingConv(llvm::CallingConv::Win64);
        llvmFunc->setDLLStorageClass(llvm::GlobalValue::DLLExportStorageClass);
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

    // パス3: インポートモジュールのstruct型を動的に推論・登録
    // program.structsに含まれないが、関数のメソッドとして参照されるstruct型を
    // 関数bodyのフィールドプロジェクションから推論して登録する
    {
        // 全関数のローカル型から参照されるstruct名を収集
        std::unordered_map<std::string, const mir::MirFunction*> missingStructFunctions;
        for (const auto& func : program.functions) {
            if (!func)
                continue;
            // メソッド名パターン: StructName__method
            const auto& funcName = func->name;
            size_t lastDunder = funcName.rfind("__");
            if (lastDunder == std::string::npos || lastDunder == 0)
                continue;

            std::string structName = funcName.substr(0, lastDunder);
            // 既に登録済みならスキップ
            if (structTypes.count(structName) > 0)
                continue;
            // ジェネリック型パラメータパターン（__T__, __K__等）はスキップ
            if (structName.find("__T__") != std::string::npos ||
                structName.find("__K__") != std::string::npos ||
                structName.find("__V__") != std::string::npos)
                continue;

            // 最初のパラメータが*StructNameであることを確認
            if (func->arg_locals.size() >= 1) {
                auto firstArgLocal = func->arg_locals[0];
                if (firstArgLocal < func->locals.size()) {
                    const auto& localType = func->locals[firstArgLocal].type;
                    if (localType && localType->kind == hir::TypeKind::Pointer &&
                        localType->element_type &&
                        localType->element_type->kind == hir::TypeKind::Struct &&
                        localType->element_type->name == structName) {
                        missingStructFunctions[structName] = func.get();
                    }
                }
            }
        }

        // 欠落struct型を関数bodyのフィールドプロジェクションから推論
        for (const auto& [structName, func] : missingStructFunctions) {
            // フィールドプロジェクションを解析してフィールド数と型を推論
            // _1.*.N = copy(rhs) パターンを検出
            std::map<uint32_t, hir::TypePtr> fieldTypeMap;

            for (const auto& bb : func->basic_blocks) {
                if (!bb)
                    continue;
                for (const auto& stmt : bb->statements) {
                    if (!stmt || stmt->kind != mir::MirStatement::Assign)
                        continue;
                    const auto& assign = std::get<mir::MirStatement::AssignData>(stmt->data);
                    const auto& place = assign.place;

                    // _1.*.N パターン（selfへのフィールド書き込み）を検出
                    if (place.projections.size() >= 2 &&
                        place.projections[0].kind == mir::ProjectionKind::Deref &&
                        place.projections[1].kind == mir::ProjectionKind::Field) {
                        uint32_t fieldId = place.projections[1].field_id;

                        // rvalueの型からフィールド型を推論
                        if (assign.rvalue && assign.rvalue->kind == mir::MirRvalue::Use) {
                            const auto& useData =
                                std::get<mir::MirRvalue::UseData>(assign.rvalue->data);
                            if (useData.operand) {
                                hir::TypePtr rhsType = nullptr;
                                if (useData.operand->type) {
                                    rhsType = useData.operand->type;
                                } else if (useData.operand->kind == mir::MirOperand::Copy ||
                                           useData.operand->kind == mir::MirOperand::Move) {
                                    const auto* rhsPlace =
                                        std::get_if<mir::MirPlace>(&useData.operand->data);
                                    if (rhsPlace && rhsPlace->local < func->locals.size()) {
                                        rhsType = func->locals[rhsPlace->local].type;
                                    }
                                } else if (useData.operand->kind == mir::MirOperand::Constant) {
                                    const auto* c =
                                        std::get_if<mir::MirConstant>(&useData.operand->data);
                                    if (c && c->type) {
                                        rhsType = c->type;
                                    }
                                }
                                if (rhsType && fieldTypeMap.find(fieldId) == fieldTypeMap.end()) {
                                    fieldTypeMap[fieldId] = rhsType;
                                }
                            }
                        }
                    }
                }
            }

            if (fieldTypeMap.empty())
                continue;

            // フィールド数を決定（最大fieldId + 1）
            uint32_t numFields = 0;
            for (const auto& [fid, _] : fieldTypeMap) {
                if (fid + 1 > numFields)
                    numFields = fid + 1;
            }

            // LLVM構造体型を作成
            std::vector<llvm::Type*> fieldTypes;
            for (uint32_t i = 0; i < numFields; ++i) {
                auto it = fieldTypeMap.find(i);
                if (it != fieldTypeMap.end() && it->second) {
                    fieldTypes.push_back(convertType(it->second));
                } else {
                    // 型が不明なフィールドはi32として扱う
                    fieldTypes.push_back(ctx.getI32Type());
                }
            }

            auto newStructType = llvm::StructType::create(ctx.getContext(), fieldTypes, structName);
            structTypes[structName] = newStructType;

            if (cm::debug::g_debug_mode) {
                std::cerr << "[MIR2LLVM] 動的推論: struct " << structName
                          << " (フィールド数: " << numFields << ")" << std::endl;
            }
        }
    }

    // enum型を処理（Tagged Unionは構造体として生成）
    for (const auto& enumPtr : program.enums) {
        if (!enumPtr)
            continue;
        const auto& enumDef = *enumPtr;
        enumDefs[enumDef.name] = &enumDef;

        // Tagged Unionの場合は構造体型を生成
        if (enumDef.is_tagged_union()) {
            // Tagged Union: { i32 tag, i8[N] payload }
            // Nは最大ペイロードサイズ
            uint32_t maxPayloadSize = enumDef.max_payload_size();
            if (maxPayloadSize == 0)
                maxPayloadSize = 8;  // 最低8バイト

            std::vector<llvm::Type*> enumFieldTypes;
            enumFieldTypes.push_back(ctx.getI32Type());  // tag
            enumFieldTypes.push_back(
                llvm::ArrayType::get(ctx.getI8Type(), maxPayloadSize));  // payload

            auto enumStructType = llvm::StructType::create(ctx.getContext(), enumFieldTypes,
                                                           enumDef.name + "_tagged");
            enumTypes[enumDef.name] = enumStructType;
        }
        // シンプルなenumはi32として扱う（enumTypes追加なし）
    }

    // インターフェース型（fat pointer）を定義
    // std::cerr << "[MIR2LLVM] Creating interface fat pointer types...\n";
    for (const auto& iface : program.interfaces) {
        if (iface) {
            getInterfaceFatPtrType(iface->name);
        }
    }

    // グローバル変数を生成
    for (const auto& gv : program.global_vars) {
        if (!gv)
            continue;

        auto llvmType = convertType(gv->type);
        if (!llvmType)
            continue;

        // リンケージの決定
        auto linkage =
            gv->is_export ? llvm::GlobalValue::ExternalLinkage : llvm::GlobalValue::InternalLinkage;

        // 初期値の決定
        llvm::Constant* initialValue = nullptr;
        if (gv->init_value) {
            // 文字列型の場合
            if (std::holds_alternative<std::string>(gv->init_value->value)) {
                auto& str = std::get<std::string>(gv->init_value->value);
                auto strConst = builder->CreateGlobalStringPtr(str, gv->name + ".str");
                initialValue = llvm::cast<llvm::Constant>(strConst);
            }
            // 整数型の場合
            else if (std::holds_alternative<int64_t>(gv->init_value->value)) {
                auto val = std::get<int64_t>(gv->init_value->value);
                initialValue = llvm::ConstantInt::get(llvmType, val);
            }
            // 浮動小数点型の場合
            else if (std::holds_alternative<double>(gv->init_value->value)) {
                auto val = std::get<double>(gv->init_value->value);
                initialValue = llvm::ConstantFP::get(llvmType, val);
            }
            // bool型の場合
            else if (std::holds_alternative<bool>(gv->init_value->value)) {
                auto val = std::get<bool>(gv->init_value->value);
                initialValue = llvm::ConstantInt::get(llvmType, val ? 1 : 0);
            }
        }

        // 初期値が設定されなかった場合はゼロ初期化
        if (!initialValue) {
            initialValue = llvm::Constant::getNullValue(llvmType);
        }

        // LLVM GlobalVariableを作成
        auto globalVar = new llvm::GlobalVariable(*module, llvmType, gv->is_const, linkage,
                                                  initialValue, gv->name);

        // グローバル変数マップに登録
        globalVariables[gv->name] = globalVar;
    }
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

// モジュール単位でのMIR→LLVM IR変換
// extern関数はdeclareのみ、自モジュール関数は完全変換
void MIRToLLVM::convert(const mir::ModuleProgram& module) {
    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMConvert,
                            "Starting module conversion: " + module.module_name);

    // allModuleFunctionsを構築（自モジュール + extern の全関数）
    // declareExternalFunctionでcurrentProgramがNULLの場合のフォールバックに使用
    allModuleFunctions.clear();
    for (const auto* func : module.functions) {
        allModuleFunctions.push_back(func);
    }
    for (const auto* func : module.extern_functions) {
        allModuleFunctions.push_back(func);
    }

    // ターゲット判定をキャッシュ
    std::string triple = this->module->getTargetTriple();
    isWasmTarget = triple.find("wasm") != std::string::npos;

    // === インターフェース名を収集 ===
    for (const auto* iface : module.interfaces) {
        if (iface) {
            interfaceNames.insert(iface->name);
        }
    }

    // === 構造体型を定義（自モジュール + extern） ===
    // パス1: opaque型として作成
    auto register_structs_pass1 = [&](const std::vector<const mir::MirStruct*>& structs) {
        for (const auto* structPtr : structs) {
            const auto& name = structPtr->name;
            if (structTypes.count(name) > 0)
                continue;  // 重複スキップ
            structDefs[name] = structPtr;
            auto structType = llvm::StructType::create(ctx.getContext(), name);
            structTypes[name] = structType;
        }
    };
    register_structs_pass1(module.structs);
    register_structs_pass1(module.extern_structs);

    // パス2: フィールド型を設定
    auto register_structs_pass2 = [&](const std::vector<const mir::MirStruct*>& structs) {
        for (const auto* structPtr : structs) {
            const auto& name = structPtr->name;
            auto it = structTypes.find(name);
            if (it == structTypes.end())
                continue;
            if (it->second->isOpaque()) {
                std::vector<llvm::Type*> fieldTypes;
                for (const auto& field : structPtr->fields) {
                    fieldTypes.push_back(convertType(field.type));
                }
                it->second->setBody(fieldTypes);
            }
        }
    };
    register_structs_pass2(module.structs);
    register_structs_pass2(module.extern_structs);

    // === enum型を定義（自モジュール + extern） ===
    auto register_enums = [&](const std::vector<const mir::MirEnum*>& enums) {
        for (const auto* enumPtr : enums) {
            const auto& enumDef = *enumPtr;
            if (enumDefs.count(enumDef.name) > 0)
                continue;  // 重複スキップ
            enumDefs[enumDef.name] = &enumDef;

            // Tagged Unionの場合は構造体型を生成
            if (enumDef.is_tagged_union()) {
                uint32_t maxPayloadSize = enumDef.max_payload_size();
                if (maxPayloadSize == 0)
                    maxPayloadSize = 8;

                std::vector<llvm::Type*> enumFieldTypes;
                enumFieldTypes.push_back(ctx.getI32Type());
                enumFieldTypes.push_back(llvm::ArrayType::get(ctx.getI8Type(), maxPayloadSize));

                auto enumStructType = llvm::StructType::create(ctx.getContext(), enumFieldTypes,
                                                               enumDef.name + "_tagged");
                enumTypes[enumDef.name] = enumStructType;
            }
        }
    };
    register_enums(module.enums);
    register_enums(module.extern_enums);

    // === インターフェースfat pointer型 ===
    for (const auto* iface : module.interfaces) {
        if (iface) {
            getInterfaceFatPtrType(iface->name);
        }
    }

    // === グローバル変数 ===
    for (const auto* gv : module.global_vars) {
        if (!gv)
            continue;
        if (globalVariables.count(gv->name) > 0)
            continue;  // 重複スキップ

        auto llvmType = convertType(gv->type);
        if (!llvmType)
            continue;

        auto linkage =
            gv->is_export ? llvm::GlobalValue::ExternalLinkage : llvm::GlobalValue::InternalLinkage;

        llvm::Constant* initialValue = nullptr;
        if (gv->init_value) {
            if (std::holds_alternative<std::string>(gv->init_value->value)) {
                auto& str = std::get<std::string>(gv->init_value->value);
                auto strConst = builder->CreateGlobalStringPtr(str, gv->name + ".str");
                initialValue = llvm::cast<llvm::Constant>(strConst);
            } else if (std::holds_alternative<int64_t>(gv->init_value->value)) {
                initialValue =
                    llvm::ConstantInt::get(llvmType, std::get<int64_t>(gv->init_value->value));
            } else if (std::holds_alternative<double>(gv->init_value->value)) {
                initialValue =
                    llvm::ConstantFP::get(llvmType, std::get<double>(gv->init_value->value));
            } else if (std::holds_alternative<bool>(gv->init_value->value)) {
                initialValue =
                    llvm::ConstantInt::get(llvmType, std::get<bool>(gv->init_value->value) ? 1 : 0);
            }
        }
        if (!initialValue) {
            initialValue = llvm::Constant::getNullValue(llvmType);
        }

        auto globalVar = new llvm::GlobalVariable(*this->module, llvmType, gv->is_const, linkage,
                                                  initialValue, gv->name);
        globalVariables[gv->name] = globalVar;
    }

    // === 関数シグネチャを宣言 ===
    std::set<std::string> declaredFunctions;

    // 自モジュールの関数（完全な定義）
    for (const auto* func : module.functions) {
        auto funcId = generateFunctionId(*func);
        if (declaredFunctions.count(funcId) > 0)
            continue;
        declaredFunctions.insert(funcId);
        auto llvmFunc = convertFunctionSignature(*func);
        functions[funcId] = llvmFunc;
    }

    // extern関数（宣言のみ、ExternalLinkage）
    for (const auto* func : module.extern_functions) {
        auto funcId = generateFunctionId(*func);
        if (declaredFunctions.count(funcId) > 0)
            continue;
        declaredFunctions.insert(funcId);
        auto llvmFunc = convertFunctionSignature(*func);
        // extern関数はExternalLinkageを確保
        llvmFunc->setLinkage(llvm::GlobalValue::ExternalLinkage);
        functions[funcId] = llvmFunc;
    }

    // === vtable生成 ===
    // currentProgramが必要なのでダミーで対応は難しい
    // vtable情報はModuleProgramのvtablesから直接生成
    // 注意: generateVTables()はMirProgramを必要とするため、
    //        モジュール単位ではvtableを個別に処理する必要がある
    // 現時点ではvtableを使うプログラムは全体コンパイルにフォールバック

    // === 自モジュール関数の実装を変換 ===
    declaredFunctions.clear();
    for (const auto* func : module.functions) {
        auto funcId = generateFunctionId(*func);
        if (declaredFunctions.count(funcId) > 0)
            continue;
        declaredFunctions.insert(funcId);
        convertFunction(*func);
    }
    // extern関数はボディなし（declare）なので変換しない

    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMConvertEnd,
                            "Module conversion complete: " + module.module_name);
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
            func.name.find("cm_file_") == 0 || func.name.find("cm_read_") == 0 ||
            func.name.find("cm_io_") == 0) {
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

        // asm出力オペランドを持つ変数を事前スキャンしてallocatedLocalsに登録
        // これによりSSA定数伝播を防ぎ、asm結果が正しく反映される
        std::set<unsigned int> asmOutputLocals;
        for (size_t bbIdx = 0; bbIdx < func.basic_blocks.size(); ++bbIdx) {
            const auto& bb = func.basic_blocks[bbIdx];
            // DCEで削除されたブロックはスキップ
            if (!bb) {
                continue;
            }
            for (size_t stmtIdx = 0; stmtIdx < bb->statements.size(); ++stmtIdx) {
                const auto& stmt = bb->statements[stmtIdx];
                if (stmt->kind == mir::MirStatement::Asm) {
                    auto& asmData = std::get<mir::MirStatement::AsmData>(stmt->data);
                    for (const auto& operand : asmData.operands) {
                        // 出力オペランド（=r, +r）を検出
                        if (!operand.constraint.empty() &&
                            (operand.constraint[0] == '=' || operand.constraint[0] == '+')) {
                            asmOutputLocals.insert(operand.local_id);
                        }
                    }
                }
            }
        }

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
                if (arg.getType()->isStructTy() || arg.getType()->isArrayTy()) {
                    // 構造体・配列の値渡しパラメータの場合、allocaに格納してポインタとして使用
                    // （構造体: C ABIで16バイト以下はレジスタ渡し、配列: GEP操作にポインタが必要）
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

                    // asm出力オペランド変数はSSA形式ではなくalloca強制
                    bool isAsmOutput = asmOutputLocals.count(static_cast<unsigned int>(i)) > 0;

                    // 関数ポインタ型はアロケーションしない（SSA形式で扱う）
                    // ただしasm出力変数は例外
                    if (!isAsmOutput &&
                        (local.type->kind == hir::TypeKind::Function ||
                         (local.type->kind == hir::TypeKind::Pointer && local.type->element_type &&
                          local.type->element_type->kind == hir::TypeKind::Function))) {
                        continue;
                    }
                    // 配列へのポインタ型の一時変数はアロケーションしない（SSA形式で扱う）
                    // ただしasm出力変数は例外
                    if (!isAsmOutput && local.type->kind == hir::TypeKind::Pointer &&
                        local.type->element_type &&
                        local.type->element_type->kind == hir::TypeKind::Array &&
                        !local.is_user_variable) {
                        continue;
                    }
                    // 文字列型の一時変数はアロケーションしない（直接値を使用）
                    // ただしasm出力変数は例外
                    if (!isAsmOutput && local.type->kind == hir::TypeKind::String &&
                        !local.is_user_variable) {
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
                        // std::cerr << "[MIR2LLVM]     Local " << i
                        //           << " is slice, calling cm_slice_new\n";
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
                    // グローバル変数はconvert()で既に作成済み
                    if (local.is_global) {
                        // グローバル変数への参照
                        auto it = globalVariables.find(local.name);
                        if (it != globalVariables.end()) {
                            locals[i] = it->second;
                            allocatedLocals.insert(i);
                        }
                    } else if (local.is_static) {
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

        // 最初のブロック（エントリブロック）へジャンプ
        // func.entry_blockを使用して正しいエントリポイントにジャンプ
        mir::BlockId entry = func.entry_block;
        if (entry < func.basic_blocks.size() && func.basic_blocks[entry]) {
            builder->CreateBr(blocks[entry]);
        } else if (!func.basic_blocks.empty() && func.basic_blocks[0]) {
            // フォールバック: entry_blockが無効な場合はブロック0を使用
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

            // Tagged Unionペイロード読み込みの特別処理
            // rvalueがUse/Copyで、ソースがTagged Unionのfield[1]かつターゲットが構造体の場合
            // memcpyを使用して直接コピー
            if (assign.rvalue->kind == mir::MirRvalue::Use) {
                auto& useData = std::get<mir::MirRvalue::UseData>(assign.rvalue->data);
                if (useData.operand && (useData.operand->kind == mir::MirOperand::Copy ||
                                        useData.operand->kind == mir::MirOperand::Move)) {
                    auto& srcPlace = std::get<mir::MirPlace>(useData.operand->data);

                    // ソースがTagged Unionのfield[1]か確認
                    bool isSrcTaggedUnionPayload = false;
                    if (!srcPlace.projections.empty() &&
                        srcPlace.projections.back().kind == mir::ProjectionKind::Field &&
                        srcPlace.projections.back().field_id == 1) {
                        if (currentMIRFunction &&
                            srcPlace.local < currentMIRFunction->locals.size()) {
                            auto& srcLocal = currentMIRFunction->locals[srcPlace.local];

                            if (srcLocal.type && srcLocal.type->name.find("__TaggedUnion_") == 0) {
                                isSrcTaggedUnionPayload = true;
                            }
                        }
                    }

                    // ターゲットが構造体型か確認
                    bool isTargetStruct = false;
                    hir::TypePtr targetType = nullptr;
                    if (currentMIRFunction &&
                        assign.place.local < currentMIRFunction->locals.size()) {
                        targetType = currentMIRFunction->locals[assign.place.local].type;
                        if (targetType && targetType->kind == hir::TypeKind::Struct) {
                            isTargetStruct = true;
                        }
                    }

                    // Tagged Unionペイロードからの値コピー
                    if (isSrcTaggedUnionPayload) {
                        if (isTargetStruct) {
                            // 構造体ペイロード → memcpyで直接コピー
                            auto srcAddr = convertPlaceToAddress(srcPlace);

                            // ターゲットアドレス（構造体ローカル）
                            auto destAddr = locals[assign.place.local];
                            if (!destAddr && allocatedLocals.count(assign.place.local) > 0) {
                                destAddr = locals[assign.place.local];
                            }

                            if (srcAddr && destAddr) {
                                // 構造体サイズを取得
                                auto llvmTargetType = convertType(targetType);
                                auto dataLayout = module->getDataLayout();
                                auto structSize = dataLayout.getTypeAllocSize(llvmTargetType);

                                // memcpy: dest=構造体ローカル, src=i8配列, size=構造体サイズ
                                builder->CreateMemCpy(destAddr, llvm::MaybeAlign(), srcAddr,
                                                      llvm::MaybeAlign(), structSize);
                                break;
                            }
                        } else if (targetType) {
                            // 非構造体ペイロード（string/ptr/int等）
                            // ペイロードバイト配列からターゲット型でロード
                            auto srcAddr = convertPlaceToAddress(srcPlace);
                            if (srcAddr) {
                                auto llvmTargetType = convertType(targetType);
                                auto loadVal =
                                    builder->CreateLoad(llvmTargetType, srcAddr, "payload_load");

                                auto destAddr = locals[assign.place.local];
                                if (destAddr && allocatedLocals.count(assign.place.local) > 0) {
                                    // allocaモード: load→store
                                    builder->CreateStore(loadVal, destAddr);
                                } else {
                                    // SSAモード（string等allocaなし）: 直接値を設定
                                    locals[assign.place.local] = loadVal;
                                }
                                break;
                            }
                        }
                    }
                }
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
                            // 重要: rvalueがalloca（スタック上のアドレス）の場合のみloadする
                            // rvalueがポインタ値（nullポインタを含む）の場合はloadしてはいけない
                            bool isRvalueAlloca = llvm::isa<llvm::AllocaInst>(rvalue);
                            if (sourceType->isPointerTy() && targetType->isStructTy() &&
                                isRvalueAlloca) {
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

                            // Tagged Unionペイロードへの書き込みを検出
                            // field[1]への書き込みで、ターゲットがi8配列の場合はmemcpyを使用
                            bool isTaggedUnionPayload = false;
                            bool isFieldProj = !assign.place.projections.empty() &&
                                               assign.place.projections.back().kind ==
                                                   mir::ProjectionKind::Field &&
                                               assign.place.projections.back().field_id == 1;

                            if (isFieldProj) {
                                // 親がTagged Union型か確認
                                if (currentMIRFunction &&
                                    assign.place.local < currentMIRFunction->locals.size()) {
                                    auto& local = currentMIRFunction->locals[assign.place.local];
                                    if (local.type &&
                                        local.type->name.find("__TaggedUnion_") == 0) {
                                        isTaggedUnionPayload = true;
                                    }
                                }
                            }

                            // rvalueが構造体値、またはallocaポインタで構造体を指す場合
                            bool isStructPayload = rvalue->getType()->isStructTy();
                            llvm::Type* structType = nullptr;
                            if (!isStructPayload && rvalue->getType()->isPointerTy()) {
                                if (auto* allocaInst = llvm::dyn_cast<llvm::AllocaInst>(rvalue)) {
                                    if (allocaInst->getAllocatedType()->isStructTy()) {
                                        isStructPayload = true;
                                        structType = allocaInst->getAllocatedType();
                                    }
                                }
                            }
                            if (!structType && isStructPayload) {
                                structType = rvalue->getType();
                            }

                            // Index projectionがあるか確認（配列要素への代入）
                            bool hasIndexProjection = false;
                            for (const auto& proj : assign.place.projections) {
                                if (proj.kind == mir::ProjectionKind::Index) {
                                    hasIndexProjection = true;
                                    break;
                                }
                            }

                            // 構造体代入でmemcpyを使用する条件:
                            // 1. Tagged Unionペイロードへの書き込み
                            bool needsStructCopy =
                                (isTaggedUnionPayload && isStructPayload && structType) ||
                                (hasIndexProjection && isStructPayload && structType);

                            if (needsStructCopy) {
                                // 構造体ペイロードの場合: memcpyを使用
                                llvm::Value* srcPtr = rvalue;
                                if (!rvalue->getType()->isPointerTy()) {
                                    // 一時的なallocaを作成してstoreし、そのポインタを使用
                                    auto tempAlloca = builder->CreateAlloca(rvalue->getType(),
                                                                            nullptr, "tmp_payload");
                                    builder->CreateStore(rvalue, tempAlloca);
                                    srcPtr = tempAlloca;
                                }

                                // ペイロードサイズを取得
                                auto dataLayout = module->getDataLayout();
                                auto payloadSize = dataLayout.getTypeAllocSize(structType);

                                // memcpy: dest=addr, src=srcPtr, size=payloadSize
                                builder->CreateMemCpy(addr, llvm::MaybeAlign(), srcPtr,
                                                      llvm::MaybeAlign(), payloadSize);
                            } else {
                                // 通常のStore操作を実行
                                // asm出力で変更される変数へのstoreはvolatileにして最適化を防止
                                auto* storeInst = builder->CreateStore(rvalue, addr);
                                if (isAllocated) {
                                    storeInst->setVolatile(true);
                                }
                            }
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
        case mir::MirStatement::Asm: {
            // インラインアセンブリ
            auto& asmData = std::get<mir::MirStatement::AsmData>(stmt.data);
            debug_msg("llvm_asm", "Emitting inline asm: " + asmData.code +
                                      " operands=" + std::to_string(asmData.operands.size()));

            if (asmData.operands.empty()) {
                // オペランドなし: シンプルなasm
                auto* asmFuncTy = llvm::FunctionType::get(ctx.getVoidType(), false);
                std::string constraints = asmData.is_must ? "~{memory}" : "";
                auto* inlineAsm = llvm::InlineAsm::get(asmFuncTy, asmData.code, constraints,
                                                       asmData.is_must  // hasSideEffects
                );
                builder->CreateCall(asmFuncTy, inlineAsm);
            } else {
                // オペランド付き: 制約文字列とオペランドを生成
                // 出力/入出力オペランドを分類
                // LLVMの制約形式: 出力制約,tied入力,純粋入力の順
                std::vector<llvm::Type*> inputTypes;
                std::vector<llvm::Value*> inputValues;
                std::vector<llvm::Value*> outputPtrs;      // 出力先ポインタ（=r用）
                std::vector<llvm::Type*> outputTypes;      // 出力オペランドの型（=r用）
                std::vector<mir::LocalId> outputLocalIds;  // 出力ローカルIDも記録
                std::string constraints;
                int outputCount = 0;  // =r の数
                int inputCount = 0;  // 入力オペランドの数（将来の拡張/デバッグ用）
                (void)inputCount;  // 現時点では読み取り不要だが、インクリメントは維持

                // AArch64ターゲット判定とオペランド型記録
                // LLVMのAArch64バックエンドがi32に対してxレジスタを割り当てる場合があるため、
                // :w修飾子を付与して32bitレジスタ(w)を強制する
                std::string asmTriple = module->getTargetTriple();
                bool isAArch64Target = (asmTriple.find("aarch64") != std::string::npos ||
                                        asmTriple.find("arm64") != std::string::npos);
                std::map<size_t, llvm::Type*> operandElemTypes;  // オペランドindex→LLVM型
                std::map<size_t, bool> operandIsMemory;          // メモリ制約かどうか

                // =m制約用: メモリ出力はポインタを入力として渡す
                std::vector<llvm::Value*> memOutputPtrs;  // =m用のポインタ（入力として渡す）
                std::vector<llvm::Type*> memOutputTypes;  // =m用の要素型（elementtype属性用）
                std::vector<mir::LocalId> memOutputLocalIds;
                std::vector<std::string> memOutputConstraints;

                // m入力制約用: ポインタを渡し、elementtype属性が必要
                std::vector<size_t> memInputIndices;  // pureInputValues内でのm制約インデックス
                std::vector<llvm::Type*> memInputTypes;  // m入力の要素型（elementtype属性用）
                // +m tied入力用: 同様にelementtype属性が必要
                std::vector<size_t> memTiedInputIndices;  // tiedInputValues内での+m制約インデックス
                std::vector<llvm::Type*> memTiedInputTypes;  // +m入力の要素型

                // 2パス方式で入力値を収集
                // 第1パス: +rのtied入力を収集（出力順）
                // 第2パス: 純粋な入力(r,m)を収集
                std::vector<llvm::Value*> tiedInputValues;
                std::vector<llvm::Type*> tiedInputTypes;
                std::vector<llvm::Value*> pureInputValues;
                std::vector<llvm::Type*> pureInputTypes;

                for (size_t i = 0; i < asmData.operands.size(); ++i) {
                    auto& operand = asmData.operands[i];

                    // 定数オペランドの場合（macro/const）
                    if (operand.is_constant) {
                        // i,n制約: 定数値をConstantIntとして生成
                        llvm::Value* constVal =
                            llvm::ConstantInt::get(ctx.getI64Type(), operand.const_value);
                        pureInputValues.push_back(constVal);
                        pureInputTypes.push_back(constVal->getType());
                        debug_msg("llvm_asm", "[ASM] const operand: " + operand.constraint + " = " +
                                                  std::to_string(operand.const_value));
                        continue;
                    }

                    // ローカル変数を取得（localsはstd::map）
                    llvm::Value* localPtr = nullptr;
                    if (locals.count(operand.local_id) > 0 && locals[operand.local_id]) {
                        localPtr = locals[operand.local_id];
                    }

                    if (!localPtr)
                        continue;

                    // ローカル変数の実際の型を取得
                    llvm::Type* elemType = ctx.getI32Type();  // デフォルトint型
                    if (auto* allocaInst = llvm::dyn_cast<llvm::AllocaInst>(localPtr)) {
                        // allocaから実際の型を取得
                        elemType = allocaInst->getAllocatedType();
                    } else if (currentMIRFunction &&
                               operand.local_id < currentMIRFunction->locals.size()) {
                        // SSA値（ConstantInt等）の場合、MIR型情報から型を推定
                        auto& localInfo = currentMIRFunction->locals[operand.local_id];
                        if (localInfo.type) {
                            auto llvmType = convertType(localInfo.type);
                            if (llvmType && !llvmType->isVoidTy()) {
                                elemType = llvmType;
                            }
                        }
                    }
                    // AArch64用: オペランドの型とメモリ制約を記録
                    operandElemTypes[i] = elemType;
                    operandIsMemory[i] = (operand.constraint.find('m') != std::string::npos);
                    bool hasOutput = operand.constraint[0] == '=' || operand.constraint[0] == '+';
                    bool isPureInput = operand.constraint[0] != '=' && operand.constraint[0] != '+';
                    bool isTiedInput = operand.constraint[0] == '+';

                    // ローカル変数から値をロード
                    auto loadValue = [&]() -> llvm::Value* {
                        if (llvm::isa<llvm::PointerType>(localPtr->getType()) ||
                            llvm::isa<llvm::AllocaInst>(localPtr)) {
                            auto* loadInst = builder->CreateLoad(elemType, localPtr);
                            if (auto* li = llvm::dyn_cast<llvm::LoadInst>(loadInst)) {
                                li->setVolatile(true);
                            }
                            return loadInst;
                        }
                        return localPtr;
                    };

                    if (isTiedInput) {
                        // +r, +m: tied入力 - 先に入力値を収集
                        bool isMemoryTied = (operand.constraint.find('m') != std::string::npos);
                        if (isMemoryTied) {
                            // +m: ポインタを渡す（メモリ出力と同様）
                            // tiedInputValues内でのインデックスを記録（後でelementtype追加用）
                            memTiedInputIndices.push_back(tiedInputValues.size());
                            memTiedInputTypes.push_back(elemType);  // 要素型を記録
                            tiedInputValues.push_back(localPtr);
                            tiedInputTypes.push_back(localPtr->getType());
                        } else {
                            // +r: 値をロードして渡す
                            auto* val = loadValue();
                            tiedInputValues.push_back(val);
                            tiedInputTypes.push_back(val->getType());
                        }
                    } else if (isPureInput) {
                        // 制約の種類によって値の渡し方を変える
                        std::string constraintType = operand.constraint;
                        bool isMemoryConstraint = (constraintType.find('m') != std::string::npos);
                        bool isImmediateConstraint =
                            (constraintType.find('i') != std::string::npos ||
                             constraintType.find('n') != std::string::npos);
                        bool isGeneralConstraint = (constraintType.find('g') != std::string::npos);

                        if (isMemoryConstraint) {
                            // m制約: ポインタ（アドレス）を渡す
                            // pureInputValues内でのインデックスを記録（後でelementtype追加用）
                            memInputIndices.push_back(pureInputValues.size());
                            memInputTypes.push_back(elemType);  // 要素型を記録
                            pureInputValues.push_back(localPtr);
                            pureInputTypes.push_back(localPtr->getType());
                        } else if (isImmediateConstraint) {
                            // i,n制約: 即値（値をロードして渡す、LLVMが定数最適化）
                            auto* val = loadValue();
                            pureInputValues.push_back(val);
                            pureInputTypes.push_back(val->getType());
                        } else if (isGeneralConstraint) {
                            // g制約: 汎用オペランド（レジスタ、メモリ、即値のいずれか）
                            // 値をロードして渡す（LLVMが最適な方法を選択）
                            auto* val = loadValue();
                            pureInputValues.push_back(val);
                            pureInputTypes.push_back(val->getType());
                        } else {
                            // r制約: 値をロードして渡す
                            auto* val = loadValue();
                            pureInputValues.push_back(val);
                            pureInputTypes.push_back(val->getType());
                        }
                    }

                    if (hasOutput) {
                        // 制約の種類をチェック: =m (メモリ出力) か =r (レジスタ出力) か
                        std::string constraintType = operand.constraint;
                        bool isMemOutput = (constraintType.find('m') != std::string::npos);

                        if (isMemOutput) {
                            // =m 制約: メモリ出力
                            // LLVMではメモリ出力はポインタを入力として渡す（void型）
                            // 出力オペランドの場合、ストア可能なポインタが必要
                            if (!llvm::isa<llvm::AllocaInst>(localPtr) &&
                                !llvm::isa<llvm::GlobalVariable>(localPtr) &&
                                !llvm::isa<llvm::GetElementPtrInst>(localPtr)) {
                                auto* alloca =
                                    builder->CreateAlloca(elemType, nullptr, "asm_mem_out");
                                localPtr = alloca;
                                allocatedLocals.insert(operand.local_id);
                            }
                            // メモリ出力として記録（後で入力に追加）
                            memOutputPtrs.push_back(localPtr);
                            memOutputTypes.push_back(elemType);  // 要素型を記録
                            memOutputLocalIds.push_back(operand.local_id);
                            // 制約文字列を記録（=m → *m として変換）
                            if (operand.constraint[0] == '=') {
                                memOutputConstraints.push_back("=*m");
                            } else {  // +m
                                memOutputConstraints.push_back("+*m");
                            }
                        } else {
                            // =r 制約: レジスタ出力（従来通り）
                            if (!llvm::isa<llvm::AllocaInst>(localPtr) &&
                                !llvm::isa<llvm::GlobalVariable>(localPtr) &&
                                !llvm::isa<llvm::GetElementPtrInst>(localPtr)) {
                                auto* alloca =
                                    builder->CreateAlloca(elemType, nullptr, "asm_out_tmp");
                                if (isTiedInput && !tiedInputValues.empty()) {
                                    auto* initStore =
                                        builder->CreateStore(tiedInputValues.back(), alloca);
                                    initStore->setVolatile(true);
                                }
                                localPtr = alloca;
                                // localsマップを更新して、後続のcopy操作が
                                // allocaからvolatile loadで値を読み取れるようにする
                                locals[operand.local_id] = alloca;
                                allocatedLocals.insert(operand.local_id);
                            }
                            outputPtrs.push_back(localPtr);
                            outputTypes.push_back(elemType);  // 出力の型を記録
                            outputLocalIds.push_back(operand.local_id);
                            outputCount++;
                        }
                    }

                    if (!constraints.empty())
                        constraints += ",";
                    constraints += operand.constraint;
                }

                // 入力値をLLVM順序で結合: 純粋入力 → tied入力 → メモリ出力
                // 制約文字列の順序と一致させる（r,... → 0,1,... → *m,...）
                for (size_t i = 0; i < pureInputValues.size(); ++i) {
                    inputValues.push_back(pureInputValues[i]);
                    inputTypes.push_back(pureInputTypes[i]);
                    inputCount++;
                }
                for (size_t i = 0; i < tiedInputValues.size(); ++i) {
                    inputValues.push_back(tiedInputValues[i]);
                    inputTypes.push_back(tiedInputTypes[i]);
                    inputCount++;
                }
                // メモリ出力ポインタを入力として追加（=m, +m制約用）
                for (size_t i = 0; i < memOutputPtrs.size(); ++i) {
                    inputValues.push_back(memOutputPtrs[i]);
                    inputTypes.push_back(memOutputPtrs[i]->getType());
                    inputCount++;
                }

                // 制約文字列をLLVM形式に変換
                // LLVMの制約形式: 出力制約,入力制約の順
                // オペランド番号はこの順序に対応するため、再マッピングが必要
                // +rは「=r」（出力）と「0」（入力をtie）に分解する
                std::string outputConstraints;
                std::string inputConstraints;

                // オペランド番号の再マッピング表（元の番号→LLVM番号）
                std::map<size_t, size_t> operandRemap;
                size_t llvmOutputIdx = 0;  // 出力オペランドのLLVMインデックス
                size_t llvmInputIdx = 0;  // 入力オペランドを数える（出力の後に来る）

                // まず出力オペランドを処理（=r のみ、=m は除外）
                for (size_t i = 0; i < asmData.operands.size(); ++i) {
                    const auto& operand = asmData.operands[i];

                    if (operand.constraint[0] == '+' || operand.constraint[0] == '=') {
                        // メモリ出力(=m, +m)は出力制約に含めない（入力として処理）
                        bool isMemOutput = (operand.constraint.find('m') != std::string::npos);
                        if (isMemOutput) {
                            // メモリ出力はスキップ（後で入力として追加）
                            continue;
                        }

                        // =r または +r: 出力オペランド
                        operandRemap[i] = llvmOutputIdx;
                        llvmOutputIdx++;

                        if (operand.constraint[0] == '+') {
                            if (!outputConstraints.empty())
                                outputConstraints += ",";
                            outputConstraints += "=" + operand.constraint.substr(1);
                        } else {
                            if (!outputConstraints.empty())
                                outputConstraints += ",";
                            outputConstraints += operand.constraint;
                        }
                    }
                }

                // 次に入力オペランドを処理
                // inputValuesの構築順序と一致させるため:
                // 1. 純粋入力（r, m等）
                // 2. tied入力（+r）
                // 3. メモリ出力（=m, +m）

                // 1. 純粋入力を先に処理
                for (size_t i = 0; i < asmData.operands.size(); ++i) {
                    const auto& operand = asmData.operands[i];

                    // 純粋な入力（r, m等）のみ処理
                    if (operand.constraint[0] != '+' && operand.constraint[0] != '=') {
                        operandRemap[i] = llvmOutputIdx + llvmInputIdx;
                        llvmInputIdx++;
                        if (!inputConstraints.empty())
                            inputConstraints += ",";
                        // m制約は*m（間接メモリ）に変換
                        if (operand.constraint.find('m') != std::string::npos) {
                            inputConstraints += "*m";
                        } else {
                            inputConstraints += operand.constraint;
                        }
                    }
                }

                // 2. tied入力（+r）を処理
                for (size_t i = 0; i < asmData.operands.size(); ++i) {
                    const auto& operand = asmData.operands[i];

                    if (operand.constraint[0] == '+') {
                        // +r: 入力としてtied（出力番号を参照）
                        // ただし+mはスキップ（メモリ出力として別処理）
                        if (operand.constraint.find('m') == std::string::npos) {
                            if (!inputConstraints.empty())
                                inputConstraints += ",";
                            inputConstraints += std::to_string(operandRemap[i]);
                        }
                    }
                }

                // 3. メモリ出力の制約を入力制約に追加（=*m形式）
                // メモリ出力のオペランド番号も再マッピング
                size_t memOutputStartIdx = llvmOutputIdx + llvmInputIdx;
                for (size_t i = 0; i < asmData.operands.size(); ++i) {
                    const auto& operand = asmData.operands[i];
                    if ((operand.constraint[0] == '=' || operand.constraint[0] == '+') &&
                        operand.constraint.find('m') != std::string::npos) {
                        // メモリ出力のオペランド番号を設定
                        operandRemap[i] = memOutputStartIdx++;
                        if (!inputConstraints.empty())
                            inputConstraints += ",";
                        // =m → "*m" (indirect memory), +m も同様
                        inputConstraints += "*m";
                    }
                }

                // 最終制約: 出力,入力の順
                std::string llvmConstraints = outputConstraints;
                if (!inputConstraints.empty()) {
                    if (!llvmConstraints.empty()) {
                        llvmConstraints += ",";
                    }
                    llvmConstraints += inputConstraints;
                }

                // clobberリストを追加（~{memory}, ~{eax}等）
                // is_must=trueの場合はデフォルトで~{memory}を追加
                if (asmData.is_must && !llvmConstraints.empty()) {
                    // clobbers配列に~{memory}がなければ追加
                    bool hasMemoryClobber = false;
                    for (const auto& clob : asmData.clobbers) {
                        if (clob == "memory" || clob == "~{memory}") {
                            hasMemoryClobber = true;
                            break;
                        }
                    }
                    if (!hasMemoryClobber) {
                        llvmConstraints += ",~{memory}";
                    }
                }
                // 明示的なclobbersを追加
                for (const auto& clob : asmData.clobbers) {
                    if (!llvmConstraints.empty()) {
                        llvmConstraints += ",";
                    }
                    // ~{...}形式でなければ追加
                    if (clob.substr(0, 2) == "~{") {
                        llvmConstraints += clob;
                    } else {
                        llvmConstraints += "~{" + clob + "}";
                    }
                }

                // ハードコードレジスタの自動クロバー検出
                // ASMコード内の %reg パターンを検出し、入出力オペランドでないものを
                // 自動的にクロバーとして追加する
                // （LLVMのインライン展開時にレジスタの値が不正に再利用されることを防止）
                {
                    // x86-64のレジスタ名一覧（LLVM形式）
                    // 64bit → LLVM名のマッピング
                    static const std::vector<std::pair<std::string, std::string>> regPatterns = {
                        // 64ビットレジスタ
                        {"%rax", "rax"},
                        {"%rbx", "rbx"},
                        {"%rcx", "rcx"},
                        {"%rdx", "rdx"},
                        {"%rsi", "rsi"},
                        {"%rdi", "rdi"},
                        {"%rbp", "rbp"},
                        {"%r8", "r8"},
                        {"%r9", "r9"},
                        {"%r10", "r10"},
                        {"%r11", "r11"},
                        {"%r12", "r12"},
                        {"%r13", "r13"},
                        {"%r14", "r14"},
                        {"%r15", "r15"},
                        // 32ビットレジスタ（対応する64ビットをクロバー）
                        {"%eax", "rax"},
                        {"%ebx", "rbx"},
                        {"%ecx", "rcx"},
                        {"%edx", "rdx"},
                        {"%esi", "rsi"},
                        {"%edi", "rdi"},
                        // 16ビットレジスタ
                        {"%ax", "rax"},
                        {"%bx", "rbx"},
                        {"%cx", "rcx"},
                        {"%dx", "rdx"},
                        // 8ビットレジスタ
                        {"%al", "rax"},
                        {"%bl", "rbx"},
                        {"%cl", "rcx"},
                        {"%dl", "rdx"},
                        {"%ah", "rax"},
                        {"%bh", "rbx"},
                        {"%ch", "rcx"},
                        {"%dh", "rdx"},
                    };

                    std::set<std::string> detectedClobbers;
                    std::string asmCode = asmData.code;

                    for (const auto& [pattern, llvmName] : regPatterns) {
                        size_t searchPos = 0;
                        while ((searchPos = asmCode.find(pattern, searchPos)) !=
                               std::string::npos) {
                            // レジスタ名の直後が英数字やアンダースコアでないことを確認
                            // （%r12の検出で%r12bを誤検出しないように）
                            size_t afterPos = searchPos + pattern.size();
                            bool isFullMatch = true;
                            if (afterPos < asmCode.size()) {
                                char nextChar = asmCode[afterPos];
                                // %r8, %r9等の短いパターンと%r8d等の区別
                                if (std::isalnum(nextChar) || nextChar == '_') {
                                    // ただし%eax等→%eaxl等は普通ないので、
                                    // 32bit以上のパターンは次の文字がレジスタ拡張子でなければOK
                                    if (pattern.size() >= 4) {
                                        // %rax, %eax等: 後ろの文字は通常ない
                                        isFullMatch = false;
                                    } else {
                                        // %r8, %al等: %r8d のような拡張をチェック
                                        isFullMatch = false;
                                    }
                                }
                            }
                            if (isFullMatch) {
                                detectedClobbers.insert(llvmName);
                            }
                            searchPos += pattern.size();
                        }
                    }

                    // オペランドとして使用されているレジスタは除外しない
                    // （LLVMは入出力オペランドと重複するクロバーを自動的に無視する）
                    // 既に追加済みのクロバーとの重複チェック
                    for (const auto& reg : detectedClobbers) {
                        std::string clobStr = "~{" + reg + "}";
                        if (llvmConstraints.find(clobStr) == std::string::npos) {
                            if (!llvmConstraints.empty()) {
                                llvmConstraints += ",";
                            }
                            llvmConstraints += clobStr;
                        }
                    }
                }

                // asmコード内のオペランド番号を更新
                // 2段階方式: まず一時プレースホルダーに置換、次に最終番号に置換
                // これにより$0→$1と$1→$0のような交差置換が正しく処理される
                std::string remappedCode = asmData.code;

                // 第1段階: $N を一時プレースホルダー __TMP_N__ に置換
                // ただし $$N（即値エスケープ）はスキップする
                for (int i = static_cast<int>(asmData.operands.size()) - 1; i >= 0; --i) {
                    std::string oldPattern = "$" + std::to_string(i);
                    std::string tempPattern = "__TMP_" + std::to_string(i) + "__";
                    size_t pos = 0;
                    while ((pos = remappedCode.find(oldPattern, pos)) != std::string::npos) {
                        // $$N（即値エスケープ）の場合はスキップ
                        if (pos > 0 && remappedCode[pos - 1] == '$') {
                            pos++;
                            continue;
                        }
                        size_t afterNum = pos + oldPattern.length();
                        if (afterNum < remappedCode.length() &&
                            std::isdigit(remappedCode[afterNum])) {
                            pos++;
                            continue;
                        }
                        remappedCode.replace(pos, oldPattern.length(), tempPattern);
                        pos += tempPattern.length();
                    }
                }

                // 第2段階: __TMP_N__ を最終的な$REMAP[N]に置換
                // AArch64ターゲットの場合、i32以下の型のレジスタオペランドに:w修飾子を付与
                // これによりLLVMがxレジスタではなくwレジスタ(32bit)を使用する
                for (size_t i = 0; i < asmData.operands.size(); ++i) {
                    std::string tempPattern = "__TMP_" + std::to_string(i) + "__";
                    std::string newPattern;
                    // AArch64 + i32以下 + 非メモリ制約の場合に :w 修飾子を付与
                    bool needsWModifier = false;
                    if (isAArch64Target && !operandIsMemory[i]) {
                        auto typeIt = operandElemTypes.find(i);
                        if (typeIt != operandElemTypes.end()) {
                            llvm::Type* opType = typeIt->second;
                            if (opType->isIntegerTy() && opType->getIntegerBitWidth() <= 32) {
                                needsWModifier = true;
                            }
                        }
                    }
                    if (needsWModifier) {
                        newPattern = "${" + std::to_string(operandRemap[i]) + ":w}";
                    } else {
                        newPattern = "$" + std::to_string(operandRemap[i]);
                    }
                    size_t pos = 0;
                    while ((pos = remappedCode.find(tempPattern, pos)) != std::string::npos) {
                        remappedCode.replace(pos, tempPattern.length(), newPattern);
                        pos += newPattern.length();
                    }
                }

                constraints = llvmConstraints;  // 変換後の制約を使用

                if (outputCount > 0) {
                    // 出力がある場合: 戻り値型を設定
                    llvm::Type* retType;
                    if (outputCount == 1) {
                        // 単一出力: 直接その型を返す
                        retType = !outputTypes.empty() ? outputTypes[0] : ctx.getI32Type();
                    } else {
                        // 複数出力: 構造体型を返す
                        retType = llvm::StructType::get(ctx.getContext(), outputTypes);
                    }

                    auto* asmFuncTy = llvm::FunctionType::get(retType, inputTypes, false);
                    // 出力オペランドがある場合は常にsideeffect=trueにして最適化を抑制
                    auto* inlineAsm = llvm::InlineAsm::get(asmFuncTy, remappedCode, constraints,
                                                           true /* hasSideEffects */);
                    auto* result = builder->CreateCall(asmFuncTy, inlineAsm, inputValues);

                    // 出力結果を各ローカル変数にstoreする
                    for (size_t i = 0; i < outputLocalIds.size() && i < outputPtrs.size(); ++i) {
                        auto local_id = outputLocalIds[i];
                        auto* outputPtr = outputPtrs[i];

                        if (!outputPtr)
                            continue;

                        llvm::Value* outputValue;
                        if (outputCount == 1) {
                            // 単一出力: 結果をそのまま使用
                            outputValue = result;
                        } else {
                            // 複数出力: 構造体からextractvalueで取り出す
                            outputValue =
                                builder->CreateExtractValue(result, {static_cast<unsigned>(i)});
                        }

                        // 出力ポインタがある場合はvolatile store
                        if (llvm::isa<llvm::PointerType>(outputPtr->getType())) {
                            auto* storeInst = builder->CreateStore(outputValue, outputPtr);
                            storeInst->setVolatile(true);
                            // allocatedLocalsに追加してvolatile loadを強制
                            allocatedLocals.insert(local_id);
                        } else {
                            // 直接SSA値として格納（fallback）
                            locals[local_id] = outputValue;
                        }
                    }

                    // m入力制約にelementtype属性を追加（*m間接メモリ用）
                    if (!memInputIndices.empty()) {
                        if (auto* callInst = llvm::dyn_cast<llvm::CallInst>(result)) {
                            size_t pureInputStartArgIdx = tiedInputValues.size();
                            for (size_t i = 0; i < memInputIndices.size(); ++i) {
                                size_t pureIdx = memInputIndices[i];
                                size_t argIdx = pureInputStartArgIdx + pureIdx;
                                // オペランドの実際の型を使用
                                llvm::Type* memElemType = (i < memInputTypes.size())
                                                              ? memInputTypes[i]
                                                              : ctx.getI32Type();
                                auto elemTypeAttr = llvm::Attribute::get(
                                    ctx.getContext(), llvm::Attribute::ElementType, memElemType);
                                callInst->addParamAttr(argIdx, elemTypeAttr);
                            }
                        }
                    }
                } else if (!inputValues.empty()) {
                    // 入力のみ: 戻り値なし
                    auto* asmFuncTy = llvm::FunctionType::get(ctx.getVoidType(), inputTypes, false);
                    auto* inlineAsm =
                        llvm::InlineAsm::get(asmFuncTy, remappedCode, constraints, asmData.is_must);
                    auto* callInst = builder->CreateCall(asmFuncTy, inlineAsm, inputValues);

                    // メモリ出力ポインタ（*m制約）にelementtype属性を追加
                    // LLVM 17ではindirect memory制約にはelementtype属性が必要
                    if (!memOutputPtrs.empty()) {
                        // メモリ出力のインデックス計算: tied入力 + pure入力 + memOutputIdx
                        size_t memOutputStartArgIdx =
                            tiedInputValues.size() + pureInputValues.size();
                        for (size_t i = 0; i < memOutputPtrs.size(); ++i) {
                            size_t argIdx = memOutputStartArgIdx + i;
                            // オペランドの実際の型を使用
                            llvm::Type* memElemType =
                                (i < memOutputTypes.size()) ? memOutputTypes[i] : ctx.getI32Type();
                            auto elemTypeAttr = llvm::Attribute::get(
                                ctx.getContext(), llvm::Attribute::ElementType, memElemType);
                            callInst->addParamAttr(argIdx, elemTypeAttr);
                        }

                        // メモリ出力のローカル変数をallocatedLocalsに追加
                        for (const auto& local_id : memOutputLocalIds) {
                            allocatedLocals.insert(local_id);
                        }
                    }

                    // m入力制約にもelementtype属性を追加（*m間接メモリ用）
                    if (!memInputIndices.empty()) {
                        // memInputIndicesはpureInputValues内でのインデックス
                        // 実際のCallInst引数インデックス: tied入力 + pureInputIndex
                        size_t pureInputStartArgIdx = tiedInputValues.size();
                        for (size_t i = 0; i < memInputIndices.size(); ++i) {
                            size_t pureIdx = memInputIndices[i];
                            size_t argIdx = pureInputStartArgIdx + pureIdx;
                            // オペランドの実際の型を使用
                            llvm::Type* memElemType =
                                (i < memInputTypes.size()) ? memInputTypes[i] : ctx.getI32Type();
                            auto elemTypeAttr = llvm::Attribute::get(
                                ctx.getContext(), llvm::Attribute::ElementType, memElemType);
                            callInst->addParamAttr(argIdx, elemTypeAttr);
                        }
                    }

                    // +m tied入力にもelementtype属性を追加
                    if (!memTiedInputIndices.empty()) {
                        // memTiedInputIndicesはtiedInputValues内でのインデックス
                        // 実際のCallInst引数インデックス: tiedInputIndex （先頭から）
                        for (size_t i = 0; i < memTiedInputIndices.size(); ++i) {
                            size_t tiedIdx = memTiedInputIndices[i];
                            // オペランドの実際の型を使用
                            llvm::Type* memElemType = (i < memTiedInputTypes.size())
                                                          ? memTiedInputTypes[i]
                                                          : ctx.getI32Type();
                            auto elemTypeAttr = llvm::Attribute::get(
                                ctx.getContext(), llvm::Attribute::ElementType, memElemType);
                            callInst->addParamAttr(tiedIdx, elemTypeAttr);
                        }
                    }
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

            // プロジェクションがある場合
            if (!refData.place.projections.empty()) {
                auto& localInfo = currentMIRFunction->locals[local];
                hir::TypePtr currentType = localInfo.type;
                llvm::Value* addr = basePtr;

                // Projectionシーケンスを順次処理
                for (size_t pi = 0; pi < refData.place.projections.size(); ++pi) {
                    const auto& proj = refData.place.projections[pi];

                    if (proj.kind == mir::ProjectionKind::Deref) {
                        // ポインタをロード
                        if (currentType && currentType->kind == hir::TypeKind::Pointer) {
                            addr = builder->CreateLoad(ctx.getPtrType(), addr, "deref_load");
                            currentType = currentType->element_type;
                        }
                    } else if (proj.kind == mir::ProjectionKind::Index) {
                        // ポインタ型の場合、Indexの前に暗黙のDerefが必要
                        // MIR生成で Deref が省略されるケース（p[0]等）に対応
                        if (currentType && currentType->kind == hir::TypeKind::Pointer) {
                            addr = builder->CreateLoad(ctx.getPtrType(), addr, "implicit_deref");
                            currentType = currentType->element_type;
                        }

                        // インデックスアクセス
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

                        // Array型の場合と、Pointer要素へのアクセス（Deref後）で異なるGEPを生成
                        if (currentType && currentType->kind == hir::TypeKind::Array) {
                            // Array型: {0, idx}の2インデックスGEP
                            auto elemType = convertType(currentType->element_type);
                            auto arraySize = currentType->array_size.value_or(0);
                            auto arrayType = llvm::ArrayType::get(elemType, arraySize);
                            addr = builder->CreateGEP(
                                arrayType, addr,
                                {llvm::ConstantInt::get(ctx.getI64Type(), 0), indexVal},
                                "arr_elem_ptr");
                            // 型を要素型に更新
                            currentType = currentType->element_type;
                        } else {
                            // Pointer要素へのアクセス（Deref後）: {idx}のみのGEP
                            llvm::Type* elemType = ctx.getI32Type();  // デフォルト
                            if (currentType) {
                                elemType = convertType(currentType);
                            }
                            addr = builder->CreateGEP(elemType, addr, indexVal, "idx_elem_ptr");
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
                                            // ネストジェネリックの場合、再帰的にマングリング
                                            std::string nestedName = typeArg->name;
                                            // type_argsがある場合（例: Vector<int>）、再帰的に処理
                                            if (!typeArg->type_args.empty()) {
                                                for (const auto& nestedArg : typeArg->type_args) {
                                                    if (nestedArg) {
                                                        nestedName += "__";
                                                        switch (nestedArg->kind) {
                                                            case hir::TypeKind::Int:
                                                                nestedName += "int";
                                                                break;
                                                            case hir::TypeKind::UInt:
                                                                nestedName += "uint";
                                                                break;
                                                            case hir::TypeKind::Long:
                                                                nestedName += "long";
                                                                break;
                                                            case hir::TypeKind::ULong:
                                                                nestedName += "ulong";
                                                                break;
                                                            case hir::TypeKind::Float:
                                                                nestedName += "float";
                                                                break;
                                                            case hir::TypeKind::Double:
                                                                nestedName += "double";
                                                                break;
                                                            case hir::TypeKind::Bool:
                                                                nestedName += "bool";
                                                                break;
                                                            case hir::TypeKind::Char:
                                                                nestedName += "char";
                                                                break;
                                                            case hir::TypeKind::String:
                                                                nestedName += "string";
                                                                break;
                                                            case hir::TypeKind::Struct:
                                                                nestedName += nestedArg->name;
                                                                break;
                                                            default:
                                                                if (!nestedArg->name.empty()) {
                                                                    nestedName += nestedArg->name;
                                                                }
                                                                break;
                                                        }
                                                    }
                                                }
                                            }
                                            structLookupName += nestedName;
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
                                addr = builder->CreateGEP(it->second, addr, indices, "field_ptr");
                            }
                        }
                    }
                }
                // Projectionシーケンス処理後はaddrを返す
                return addr;
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
            // ただし、ポインタ同士（ptr == ptr）の場合でもunion alloca→string等の
            // 抽出が必要なケースがあるためスキップする
            if (sourceType == targetType) {
                bool isUnionAllocaExtract = false;
                if (sourceType->isPointerTy()) {
                    if (auto* allocaInst = llvm::dyn_cast<llvm::AllocaInst>(value)) {
                        auto* allocatedType = allocaInst->getAllocatedType();
                        if (auto* structTy = llvm::dyn_cast<llvm::StructType>(allocatedType)) {
                            if (structTy->getNumElements() == 2) {
                                auto* elem0 = structTy->getElementType(0);
                                auto* elem1 = structTy->getElementType(1);
                                if (elem0->isIntegerTy(32) && elem1->isArrayTy()) {
                                    isUnionAllocaExtract = true;
                                }
                            }
                        }
                    }
                }
                if (!isUnionAllocaExtract) {
                    return value;
                }
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

            // === タグ付きユニオン型への変換（int/long/bool/string/struct -> Union等） ===
            // targetTypeがStructType {i32, i8[N]}の場合
            // 注意: この検出はポインタ処理より前に配置する必要がある
            //       string型（ptr）がポインタ処理パスに吸収されないようにするため
            if (auto* structTy = llvm::dyn_cast<llvm::StructType>(targetType)) {
                // タグ付きユニオン構造体（{i32, i8[N]}）かどうか確認
                if (structTy->getNumElements() == 2) {
                    auto* elem0 = structTy->getElementType(0);
                    auto* elem1 = structTy->getElementType(1);
                    if (elem0->isIntegerTy(32) && elem1->isArrayTy()) {
                        // タグ付きユニオンへのキャスト
                        // 1. 一時allocaを作成
                        auto* alloca = builder->CreateAlloca(structTy, nullptr, "union_temp");

                        // 2.
                        // タグを設定（castData.target_type->type_argsからバリアントインデックスを計算）
                        int32_t tagValue = 0;
                        if (castData.target_type && !castData.target_type->type_args.empty()) {
                            // ソース型のLLVM型とtype_argsのHIR型を照合してタグ値を決定
                            for (size_t vi = 0; vi < castData.target_type->type_args.size(); ++vi) {
                                auto& varType = castData.target_type->type_args[vi];
                                if (!varType)
                                    continue;
                                auto* varLLVMType = convertType(varType);
                                if (varLLVMType == sourceType) {
                                    tagValue = static_cast<int32_t>(vi);
                                    break;
                                }
                            }
                        } else if (sourceType->isIntegerTy(64)) {
                            tagValue = 1;  // フォールバック: long = 1
                        }
                        auto* tagGEP = builder->CreateStructGEP(structTy, alloca, 0, "tag_ptr");
                        builder->CreateStore(llvm::ConstantInt::get(ctx.getI32Type(), tagValue),
                                             tagGEP);

                        // 3. ペイロードに値をストア（全型対応）
                        auto* payloadGEP =
                            builder->CreateStructGEP(structTy, alloca, 1, "payload_ptr");

                        if (sourceType->isStructTy()) {
                            // 構造体型: memcpyでペイロードにコピー
                            auto& dataLayout = module->getDataLayout();
                            auto payloadSize = dataLayout.getTypeAllocSize(sourceType);
                            auto* srcAlloca =
                                builder->CreateAlloca(sourceType, nullptr, "struct_tmp");
                            builder->CreateStore(value, srcAlloca);
                            builder->CreateMemCpy(payloadGEP, llvm::MaybeAlign(), srcAlloca,
                                                  llvm::MaybeAlign(), payloadSize);
                        } else {
                            // プリミティブ型（int/long/bool/float/double/ptr等）: bitcastしてストア
                            auto* payloadAsType = builder->CreateBitCast(
                                payloadGEP, llvm::PointerType::get(sourceType, 0),
                                "payload_as_type");
                            builder->CreateStore(value, payloadAsType);
                        }

                        // 4. 構造体全体をロードして返す
                        return builder->CreateLoad(structTy, alloca, "union_load");
                    }
                }
            }

            // === タグ付きユニオン型からの変換（Union -> int/long/bool/string/struct等） ===
            // sourceTypeがStructType {i32, i8[N]}の場合
            if (auto* structTy = llvm::dyn_cast<llvm::StructType>(sourceType)) {
                // タグ付きユニオン構造体（{i32, i8[N]}）かどうか確認
                if (structTy->getNumElements() == 2) {
                    auto* elem0 = structTy->getElementType(0);
                    auto* elem1 = structTy->getElementType(1);
                    if (elem0->isIntegerTy(32) && elem1->isArrayTy()) {
                        // タグ付きユニオンからのキャスト
                        // valueは集約型なので、一時allocaにストアしてからアクセス
                        auto* alloca =
                            builder->CreateAlloca(structTy, nullptr, "union_extract_temp");
                        builder->CreateStore(value, alloca);

                        // ペイロードポインタを取得
                        auto* payloadGEP =
                            builder->CreateStructGEP(structTy, alloca, 1, "payload_extract_ptr");

                        // ターゲット型に応じてロード（全型対応）
                        if (targetType->isStructTy()) {
                            // 構造体型: memcpyで読み出し
                            auto* destAlloca =
                                builder->CreateAlloca(targetType, nullptr, "struct_extract_tmp");
                            auto& dataLayout = module->getDataLayout();
                            auto copySize = dataLayout.getTypeAllocSize(targetType);
                            builder->CreateMemCpy(destAlloca, llvm::MaybeAlign(), payloadGEP,
                                                  llvm::MaybeAlign(), copySize);
                            return builder->CreateLoad(targetType, destAlloca, "struct_from_union");
                        } else {
                            // プリミティブ型（int/long/bool/float/double/ptr等）: bitcastしてロード
                            auto* payloadAsType = builder->CreateBitCast(
                                payloadGEP, llvm::PointerType::get(targetType, 0),
                                "payload_as_target");
                            return builder->CreateLoad(targetType, payloadAsType, "val_from_union");
                        }
                    }
                }
            }

            // LLVM 14+: opaque pointersではポインタ間のBitCast不要
            if (sourceType->isPointerTy() && targetType->isPointerTy()) {
                // opaque pointersでは全てのポインタは ptr 型なので通常変換不要
                // ただし、union alloca(ptr)→string(ptr)の抽出が必要なケースは除く
                bool isUnionAllocaExtract = false;
                if (auto* allocaInst = llvm::dyn_cast<llvm::AllocaInst>(value)) {
                    auto* allocatedType = allocaInst->getAllocatedType();
                    if (auto* sTy = llvm::dyn_cast<llvm::StructType>(allocatedType)) {
                        if (sTy->getNumElements() == 2 && sTy->getElementType(0)->isIntegerTy(32) &&
                            sTy->getElementType(1)->isArrayTy()) {
                            isUnionAllocaExtract = true;
                        }
                    }
                }
                if (!isUnionAllocaExtract) {
                    return value;
                }
            }

            // int <-> ポインタ変換
            if (sourceType->isIntegerTy() && targetType->isPointerTy()) {
                return builder->CreateIntToPtr(value, targetType, "inttoptr");
            }
            if (sourceType->isPointerTy()) {
                // ポインタがタグ付きユニオン構造体を指しているか確認（Union as T）
                if (auto* allocaInst = llvm::dyn_cast<llvm::AllocaInst>(value)) {
                    auto* allocatedType = allocaInst->getAllocatedType();
                    if (auto* structTy = llvm::dyn_cast<llvm::StructType>(allocatedType)) {
                        if (structTy->getNumElements() == 2) {
                            auto* elem0 = structTy->getElementType(0);
                            auto* elem1 = structTy->getElementType(1);
                            if (elem0->isIntegerTy(32) && elem1->isArrayTy()) {
                                // タグ付きユニオン構造体からの抽出（全型対応）
                                auto* payloadGEP = builder->CreateStructGEP(structTy, value, 1,
                                                                            "union_payload_ptr");
                                if (targetType->isStructTy()) {
                                    // 構造体型: memcpyで読み出し
                                    auto* destAlloca = builder->CreateAlloca(
                                        targetType, nullptr, "struct_from_union_ptr");
                                    auto& dataLayout = module->getDataLayout();
                                    auto copySize = dataLayout.getTypeAllocSize(targetType);
                                    builder->CreateMemCpy(destAlloca, llvm::MaybeAlign(),
                                                          payloadGEP, llvm::MaybeAlign(), copySize);
                                    return builder->CreateLoad(targetType, destAlloca,
                                                               "struct_from_union_ptr_load");
                                } else {
                                    // プリミティブ型: bitcastしてロード
                                    auto* payloadAsType = builder->CreateBitCast(
                                        payloadGEP, llvm::PointerType::get(targetType, 0),
                                        "payload_as_target_ptr");
                                    return builder->CreateLoad(targetType, payloadAsType,
                                                               "val_from_union_ptr");
                                }
                            }
                        }
                    }
                }
                // タグ付きユニオンではない場合
                if (targetType->isIntegerTy()) {
                    return builder->CreatePtrToInt(value, targetType, "ptrtoint");
                }
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

                                // Tagged Union構造体の特別処理
                                // __TaggedUnion_* 構造体は {i32 tag, i32 payload} 形式
                                if (lookupName.find("__TaggedUnion_") == 0) {
                                    // field[0] = タグ (int), field[1] = ペイロード (int)
                                    if (proj.field_id == 0) {
                                        currentType = hir::make_int();
                                    } else if (proj.field_id == 1) {
                                        currentType = hir::make_int();
                                    }
                                } else {
                                    auto structIt = structDefs.find(lookupName);

                                    // フォールバック: 関数名から構造体名を推論
                                    // Container__int__get → Container__int
                                    if (structIt == structDefs.end() && currentMIRFunction) {
                                        const auto& funcName = currentMIRFunction->name;
                                        size_t lastDunder = funcName.rfind("__");
                                        if (lastDunder != std::string::npos && lastDunder > 0) {
                                            std::string inferredStruct =
                                                funcName.substr(0, lastDunder);
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
                                                        // マングリング名から型引数を抽出 (Box__int
                                                        // -> int)
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
                                                                auto st =
                                                                    std::make_shared<hir::Type>(
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
                        std::cerr << "[DEBUG] fieldType fallback to i32 in "
                                  << (currentMIRFunction ? currentMIRFunction->name : "?")
                                  << " local=" << place.local
                                  << " projections=" << place.projections.size();
                        if (currentType) {
                            std::cerr << " currentType=" << currentType->name
                                      << " kind=" << static_cast<int>(currentType->kind);
                        } else {
                            std::cerr << " currentType=null";
                        }
                        std::cerr << "\n";
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
                // asm出力で変更される可能性がある変数はvolatile loadで最適化を防止
                auto* loadInst = builder->CreateLoad(allocatedType, val, "load");
                if (allocatedLocals.count(local) > 0) {
                    // asm影響変数: volatile loadで最適化を抑制
                    if (auto* li = llvm::dyn_cast<llvm::LoadInst>(loadInst)) {
                        li->setVolatile(true);
                    }
                }
                return loadInst;
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
                                        case hir::TypeKind::Pointer: {
                                            // ポインタ型: ptr_xxx 形式で追加
                                            structName += "ptr_";
                                            // 要素型を再帰的に追加
                                            if (typeArg->element_type) {
                                                switch (typeArg->element_type->kind) {
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
                                                    case hir::TypeKind::Struct:
                                                        structName += typeArg->element_type->name;
                                                        break;
                                                    default:
                                                        structName += "void";
                                                        break;
                                                }
                                            } else {
                                                structName += "void";
                                            }
                                            break;
                                        }
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
                // 配列インデックスアクセス（多次元配列フラット化対応）
                if (!addr) {
                    cm::debug::codegen::log(cm::debug::codegen::Id::LLVMError,
                                            "Index projection on null address",
                                            cm::debug::Level::Error);
                    return nullptr;
                }

                // ポインタ型の場合は単純なポインタ演算（フラット化不要）
                // Deref後のLoadInst結果（ポインタ値）へのインデックスアクセスも含む
                bool isPointerIndexing =
                    (currentType && currentType->kind == hir::TypeKind::Pointer);

                // Deref後（currentTypeはポインタの要素型）でもaddrがLoadInst結果の場合は
                // ポインタへのインデックスアクセスとして扱う
                if (!isPointerIndexing && addr && llvm::isa<llvm::LoadInst>(addr)) {
                    // LoadInstの結果（ポインタ値）へのインデックスアクセス
                    isPointerIndexing = true;
                }

                // Deref後にaddrがArgument（ポインタ引数、needsLoad=falseでDerefスキップ）の場合
                // currentTypeはelement_type（int等）だがaddrはポインタ値 → ポインタIndexing
                if (!isPointerIndexing && addr && llvm::isa<llvm::Argument>(addr) && projIdx > 0 &&
                    place.projections[projIdx - 1].kind == mir::ProjectionKind::Deref) {
                    isPointerIndexing = true;
                }

                if (isPointerIndexing) {
                    // インデックス値を取得
                    llvm::Value* indexVal = nullptr;
                    auto idx_it = locals.find(proj.index_local);
                    if (idx_it != locals.end()) {
                        indexVal = idx_it->second;
                        if (allocatedLocals.count(proj.index_local)) {
                            llvm::Type* idxType = ctx.getI64Type();
                            if (currentMIRFunction &&
                                proj.index_local < currentMIRFunction->locals.size()) {
                                auto& idxLocal = currentMIRFunction->locals[proj.index_local];
                                idxType = convertType(idxLocal.type);
                            }
                            indexVal = builder->CreateLoad(idxType, indexVal, "idx_load");
                            if (idxType->isIntegerTy(32)) {
                                indexVal =
                                    builder->CreateSExt(indexVal, ctx.getI64Type(), "idx_ext");
                            }
                        }
                    }

                    if (!indexVal) {
                        cm::debug::codegen::log(cm::debug::codegen::Id::LLVMError,
                                                "Cannot get index value for pointer access",
                                                cm::debug::Level::Error);
                        return nullptr;
                    }

                    // ポインタが指す要素の型を取得
                    llvm::Type* elemType = ctx.getI32Type();  // デフォルト
                    if (currentType && currentType->kind == hir::TypeKind::Pointer &&
                        currentType->element_type) {
                        // 通常のポインタ型: element_typeを使用
                        elemType = convertType(currentType->element_type);
                    } else if (currentType && llvm::isa<llvm::LoadInst>(addr)) {
                        // Deref後（LoadInst結果へのインデックスアクセス）: currentType自体が要素型
                        elemType = convertType(currentType);
                    }

                    // addrがポインタ変数を格納している場合、まずポインタ値をロード
                    // これはField projection後（構造体のポインタフィールドへのGEP）にも適用される
                    llvm::Value* ptrVal = addr;
                    bool needsLoad = false;

                    if (auto allocaInst = llvm::dyn_cast<llvm::AllocaInst>(addr)) {
                        auto allocType = allocaInst->getAllocatedType();
                        // allocaがポインタ型を格納している場合
                        if (allocType->isPointerTy() || allocType == ctx.getPtrType()) {
                            needsLoad = true;
                        }
                    } else if (auto gepInst = llvm::dyn_cast<llvm::GetElementPtrInst>(addr)) {
                        // GEP結果がポインタフィールドへのアドレスの場合
                        // currentTypeがPointerなら、このアドレスからポインタ値をロード
                        (void)gepInst;  // 未使用警告を抑制
                        // currentType->kind == Pointer はポインタ型フィールドを示す
                        needsLoad = true;
                    } else if (llvm::isa<llvm::LoadInst>(addr)) {
                        // LoadInst結果（Deref後）はすでにロード済みなので再ロード不要
                        needsLoad = false;
                    } else if (llvm::isa<llvm::Argument>(addr)) {
                        // ポインタ引数（Deref後）はすでにポインタ値なので再ロード不要
                        needsLoad = false;
                    } else {
                        // その他の場合（currentTypeがポインタ型なら）
                        needsLoad = true;
                    }

                    if (needsLoad) {
                        ptrVal = builder->CreateLoad(ctx.getPtrType(), addr, "ptr_load");
                    }

                    // ポインタ + オフセット
                    addr = builder->CreateGEP(elemType, ptrVal, indexVal, "ptr_elem");

                    // 型情報を更新
                    currentType = currentType->element_type;
                    break;
                }

                // 連続するIndexプロジェクションを収集（多次元配列のフラット化）
                std::vector<llvm::Value*> indexValues;
                std::vector<uint64_t> dimensions;
                hir::TypePtr arrayTypeInfo = currentType;

                // 現在のインデックスを取得
                llvm::Value* indexVal = nullptr;
                auto idx_it = locals.find(proj.index_local);
                if (idx_it != locals.end()) {
                    indexVal = idx_it->second;
                    if (allocatedLocals.count(proj.index_local)) {
                        llvm::Type* idxType = ctx.getI64Type();
                        if (currentMIRFunction &&
                            proj.index_local < currentMIRFunction->locals.size()) {
                            auto& idxLocal = currentMIRFunction->locals[proj.index_local];
                            idxType = convertType(idxLocal.type);
                        }
                        indexVal = builder->CreateLoad(idxType, indexVal, "idx_load");
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

                indexValues.push_back(indexVal);

                // 配列の次元サイズを収集
                if (arrayTypeInfo && arrayTypeInfo->kind == hir::TypeKind::Array) {
                    if (arrayTypeInfo->array_size.has_value()) {
                        dimensions.push_back(*arrayTypeInfo->array_size);
                    }
                    arrayTypeInfo = arrayTypeInfo->element_type;
                }

                // 連続するIndexプロジェクションを先読み
                size_t consumedProjections = 0;
                for (size_t nextIdx = projIdx + 1; nextIdx < place.projections.size(); ++nextIdx) {
                    const auto& nextProj = place.projections[nextIdx];
                    if (nextProj.kind != mir::ProjectionKind::Index)
                        break;
                    if (!arrayTypeInfo || arrayTypeInfo->kind != hir::TypeKind::Array)
                        break;

                    // 次のインデックス値を取得
                    llvm::Value* nextIndexVal = nullptr;
                    auto next_idx_it = locals.find(nextProj.index_local);
                    if (next_idx_it != locals.end()) {
                        nextIndexVal = next_idx_it->second;
                        if (allocatedLocals.count(nextProj.index_local)) {
                            llvm::Type* idxType = ctx.getI64Type();
                            if (currentMIRFunction &&
                                nextProj.index_local < currentMIRFunction->locals.size()) {
                                auto& idxLocal = currentMIRFunction->locals[nextProj.index_local];
                                idxType = convertType(idxLocal.type);
                            }
                            nextIndexVal = builder->CreateLoad(idxType, nextIndexVal, "idx_load");
                            if (idxType->isIntegerTy(32)) {
                                nextIndexVal =
                                    builder->CreateSExt(nextIndexVal, ctx.getI64Type(), "idx_ext");
                            }
                        }
                    }

                    if (!nextIndexVal)
                        break;

                    indexValues.push_back(nextIndexVal);
                    if (arrayTypeInfo->array_size.has_value()) {
                        dimensions.push_back(*arrayTypeInfo->array_size);
                    }
                    arrayTypeInfo = arrayTypeInfo->element_type;
                    consumedProjections++;
                }

                // 線形インデックスを計算: linear = i0*D1*D2*... + i1*D2*D3*... + ... + iN
                llvm::Value* linearIndex = nullptr;
                if (indexValues.size() > 1 && dimensions.size() == indexValues.size()) {
                    // 多次元配列の場合、線形インデックスを計算
                    linearIndex = llvm::ConstantInt::get(ctx.getI64Type(), 0);

                    for (size_t i = 0; i < indexValues.size(); ++i) {
                        llvm::Value* idx = indexValues[i];

                        // 後続の全次元のサイズを乗算
                        uint64_t stride = 1;
                        for (size_t j = i + 1; j < dimensions.size(); ++j) {
                            stride *= dimensions[j];
                        }

                        if (stride > 1) {
                            llvm::Value* strideVal =
                                llvm::ConstantInt::get(ctx.getI64Type(), stride);
                            idx = builder->CreateMul(idx, strideVal, "stride_mul");
                        }

                        linearIndex = builder->CreateAdd(linearIndex, idx, "linear_add");
                    }
                } else {
                    // 1次元配列の場合
                    linearIndex = indexValues[0];
                }

                // 消費したプロジェクションをスキップ
                projIdx += consumedProjections;

                // 最終的な要素型を取得
                llvm::Type* elemType = nullptr;
                if (arrayTypeInfo) {
                    elemType = convertType(arrayTypeInfo);
                } else if (currentType && currentType->kind == hir::TypeKind::Array) {
                    // フォールバック: フラット化後の要素型を計算
                    hir::TypePtr elemTypeInfo = currentType;
                    for (size_t i = 0; i < indexValues.size() && elemTypeInfo; ++i) {
                        if (elemTypeInfo->kind == hir::TypeKind::Array) {
                            elemTypeInfo = elemTypeInfo->element_type;
                        }
                    }
                    if (elemTypeInfo) {
                        elemType = convertType(elemTypeInfo);
                    }
                }

                if (!elemType) {
                    // 最終フォールバック: allocaから型を推測
                    if (auto allocaInst = llvm::dyn_cast<llvm::AllocaInst>(addr)) {
                        auto allocType = allocaInst->getAllocatedType();
                        if (allocType->isArrayTy()) {
                            elemType = allocType;
                            while (elemType->isArrayTy()) {
                                elemType = elemType->getArrayElementType();
                            }
                        }
                    }
                }

                if (!elemType) {
                    elemType = ctx.getI32Type();  // デフォルト
                }

                // Clang準拠: 多次元GEPを生成
                // フラット化せず、配列の配列として複数インデックスでアクセス
                // gep inbounds [300 x [300 x i32]], ptr %arr, i64 0, i64 %i, i64 %j
                if (auto allocaInst = llvm::dyn_cast<llvm::AllocaInst>(addr)) {
                    auto allocType = allocaInst->getAllocatedType();
                    if (allocType->isArrayTy() && indexValues.size() > 0) {
                        // 多次元GEP用インデックスを構築
                        std::vector<llvm::Value*> gepIndices;
                        // 最初のインデックスは常に0（ポインタから配列への変換）
                        gepIndices.push_back(llvm::ConstantInt::get(ctx.getI64Type(), 0));
                        // 各次元のインデックスを追加
                        for (auto* idx : indexValues) {
                            gepIndices.push_back(idx);
                        }
                        // 多次元GEPを生成
                        addr = builder->CreateInBoundsGEP(allocType, addr, gepIndices, "elem_ptr");
                        currentType = arrayTypeInfo;
                        break;
                    }
                }

                // フォールバック: 多次元GEPが使えない場合はフラット化
                llvm::Value* basePtr = addr;
                if (auto allocaInst = llvm::dyn_cast<llvm::AllocaInst>(addr)) {
                    auto allocType = allocaInst->getAllocatedType();
                    if (allocType->isArrayTy()) {
                        // 多次元配列の場合、要素型へのポインタに変換
                        std::vector<llvm::Value*> zeroIndices;
                        zeroIndices.push_back(llvm::ConstantInt::get(ctx.getI64Type(), 0));
                        llvm::Type* currentArrayType = allocType;
                        while (currentArrayType->isArrayTy()) {
                            zeroIndices.push_back(llvm::ConstantInt::get(ctx.getI64Type(), 0));
                            currentArrayType = currentArrayType->getArrayElementType();
                        }
                        basePtr = builder->CreateGEP(allocType, addr, zeroIndices, "flat_base");
                    }
                }
                addr = builder->CreateGEP(elemType, basePtr, linearIndex, "flat_elem_ptr");

                // 型情報を更新
                currentType = arrayTypeInfo;
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
