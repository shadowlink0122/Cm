/// @file mir_to_llvm.cpp
/// @brief MIR → LLVM IR 変換器（メインモジュール）

#include "mir_to_llvm.hpp"

#include "../../../common/debug/codegen.hpp"
#include "../monitoring/compilation_guard.hpp"

#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace cm::codegen::llvm_backend {

// TypeAlias（typedef）を基底型に再帰的に解決するヘルパー
// MemAddr → ulong, FsError → ulong のように typedef チェーンを展開する
hir::TypePtr MIRToLLVM::resolveTypeAlias(const hir::TypePtr& type) const {
    if (!type)
        return nullptr;
    auto current = type;
    // TypeAlias チェーンを辿って基底型まで解決
    while (current && current->kind == hir::TypeKind::TypeAlias) {
        if (current->element_type) {
            current = current->element_type;
        } else {
            // element_type が未設定の場合、typedefDefsで名前ベースの解決を試行
            auto it = typedefDefs.find(current->name);
            if (it != typedefDefs.end()) {
                current = it->second;
            } else {
                break;
            }
        }
    }
    // Struct kindだがtypedefの場合も解決（MIRでStruct名としてtypedef名が残る場合）
    if (current && current->kind == hir::TypeKind::Struct) {
        auto it = typedefDefs.find(current->name);
        if (it != typedefDefs.end()) {
            return resolveTypeAlias(it->second);
        }
    }
    return current;
}

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

        // TypeAlias（typedef）を基底型に解決
        auto resolvedFieldType = resolveTypeAlias(field.type);
        switch (resolvedFieldType->kind) {
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
                // TypeAlias（typedef）を基底型に解決してからマングリング
                auto resolvedType = resolveTypeAlias(local.type);
                switch (resolvedType->kind) {
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
            // TypeAlias（typedef）を基底型に解決してからマングリング
            auto resolvedType = resolveTypeAlias(type);
            switch (resolvedType->kind) {
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
    // Bug#45修正: 同名の既存関数がvoid()で先に作成されている場合がある
    // （MIR内にarg_locals空のエントリと非空のエントリが重複して存在するため）
    // シグネチャ不一致の既存関数を削除してから正しいシグネチャで再作成
    if (auto existingFunc = module->getFunction(func.name)) {
        if (existingFunc->getFunctionType() != funcType) {
            existingFunc->eraseFromParent();
        } else {
            return existingFunc;  // 同じシグネチャなら既存関数を返す
        }
    }
    auto llvmFunc =
        llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, func.name, module);

    // アロケータ関数にはnoinline属性を追加
    // LLVMが積極的にインライン化してから削除するのを防ぐ
    if (func.name.find("alloc") != std::string::npos ||
        func.name.find("dealloc") != std::string::npos ||
        func.name.find("reallocate") != std::string::npos) {
        llvmFunc->addFnAttr(llvm::Attribute::NoInline);
    }

    // Bug#11/12修正: ASM文を含む関数にnoinline属性を追加
    // MIRレベルではshould_inlineで抑制済みだが、LLVMの最適化パスによる
    // インライン展開も防止する必要がある（レジスタ割当変更・ret先不在を防ぐ）
    {
        bool hasAsm = false;
        bool hasRetInAsm = false;
        bool hasNonAsmStmt = false;  // ASM以外のステートメントが存在するか
        for (const auto& bb : func.basic_blocks) {
            if (!bb)
                continue;
            for (const auto& stmt : bb->statements) {
                if (!stmt)
                    continue;
                if (stmt->kind == mir::MirStatement::Asm) {
                    hasAsm = true;
                    const auto& asmData = std::get<mir::MirStatement::AsmData>(stmt->data);
                    // Bug#12修正: ASMコード内にret/iret命令があるか検出
                    std::string code = asmData.code;
                    for (size_t p = 0; p < code.size(); ++p) {
                        bool found = false;
                        if (p + 3 <= code.size() && code.substr(p, 3) == "ret") {
                            size_t end = p + 3;
                            if (end < code.size() && code[end] == 'q')
                                end++;
                            bool prevOk = (p == 0 || !std::isalnum(code[p - 1]));
                            bool nextOk = (end >= code.size() || !std::isalnum(code[end]));
                            if (prevOk && nextOk)
                                found = true;
                        }
                        if (p + 4 <= code.size() && code.substr(p, 4) == "iret") {
                            size_t end = p + 4;
                            if (end < code.size() && code[end] == 'q')
                                end++;
                            bool prevOk = (p == 0 || !std::isalnum(code[p - 1]));
                            bool nextOk = (end >= code.size() || !std::isalnum(code[end]));
                            if (prevOk && nextOk)
                                found = true;
                        }
                        if (found)
                            hasRetInAsm = true;
                    }
                } else {
                    // ASM以外のステートメント（関数呼び出し、変数宣言等）
                    hasNonAsmStmt = true;
                }
            }
        }
        if (hasAsm) {
            llvmFunc->addFnAttr(llvm::Attribute::NoInline);
            // Bug#12修正: ret/iretを含む純ASM関数にのみNaked属性を付与
            // Naked属性でprologue/epilogue除去（operandの有無に関わらず）
            // 注意: ASMとCmコードが混在する関数（例: syscall_entry）には
            // Naked属性を付与しない。Naked関数ではASM文のみ出力されるため、
            // 混在関数の通常コード（関数呼び出し等）が省略されてGPFを引き起こす
            if (hasRetInAsm && !hasNonAsmStmt) {
                llvmFunc->addFnAttr(llvm::Attribute::Naked);
            }
        }
    }

    // Bug#1修正: UEFIターゲットでは全関数にWin64呼出規約を設定
    // UEFIはWindows x64 ABIを使用（RCX, RDX, R8, R9）
    // efi_mainだけでなく全関数に適用しないと3引数以上の関数でポインタが破損する
    if (isUefiTarget) {
        llvmFunc->setCallingConv(llvm::CallingConv::Win64);
        // Bug#13修正: LLVMのO2パイプラインのインライン展開を防止
        // インライン展開されるとefi_mainの引数レジスタ(rcx/rdx)が
        // インライン展開されたコードのself/引数として上書きされ、
        // UEFIデータ構造が破壊される
        llvmFunc->addFnAttr(llvm::Attribute::NoInline);
        // Bug#17修正: スタックプローブ無効化
        // LLVMはWindowsターゲットで4KB以上のスタックフレームに___chkstk_msを挿入するが、
        // UEFI/ベアメタル環境ではこの関数が存在しないためリンクエラーになる
        llvmFunc->addFnAttr("no-stack-arg-probe");
        // efi_mainはDLLExportで最適化除去を防ぎ、optnoneで全最適化を無効化
        // optnoneはインライン展開 + DCE(デッドコード削除)を両方防止
        if (func.name == "efi_main") {
            llvmFunc->setDLLStorageClass(llvm::GlobalValue::DLLExportStorageClass);
            llvmFunc->addFnAttr(llvm::Attribute::OptimizeNone);
        }
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

    // typedef定義マップをコピー（convertTypeでTypeAlias/Struct名の解決に使用）
    typedefDefs = program.typedef_defs;

    // ターゲット判定をキャッシュ（境界チェック・ABI設定で使用）
    std::string triple = module->getTargetTriple();
    isWasmTarget = triple.find("wasm") != std::string::npos;
    isUefiTarget = triple.find("windows") != std::string::npos;

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

            if (cm::debug::debug_mode()) {
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
            // 文字列型の場合: IRBuilderなしでグローバル文字列定数を作成
            if (std::holds_alternative<std::string>(gv->init_value->value)) {
                auto& str = std::get<std::string>(gv->init_value->value);
                // 文字列データをグローバル定数として配置
                auto strConstant = llvm::ConstantDataArray::getString(ctx.getContext(), str, true);
                auto strGlobal = new llvm::GlobalVariable(*module, strConstant->getType(), true,
                                                          llvm::GlobalValue::PrivateLinkage,
                                                          strConstant, gv->name + ".str");
                strGlobal->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
                // i8* へのポインタを取得
                initialValue = llvm::ConstantExpr::getBitCast(
                    llvm::ConstantExpr::getInBoundsGetElementPtr(
                        strConstant->getType(), strGlobal,
                        llvm::ArrayRef<llvm::Constant*>{
                            llvm::ConstantInt::get(ctx.getI64Type(), 0),
                            llvm::ConstantInt::get(ctx.getI64Type(), 0)}),
                    ctx.getPtrType());
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

    // === Dead Function Elimination (DFE) ===
    // エントリポイントから到達可能な関数のみをLLVM IRに変換する
    // これにより、未使用のimport関数のIR生成をスキップしてメモリ消費を削減
    std::unordered_set<std::string> reachableFunctions;
    {
        // 関数名→インデックスのマップを構築
        std::unordered_map<std::string, size_t> funcNameToIndex;
        for (size_t i = 0; i < program.functions.size(); ++i) {
            if (program.functions[i]) {
                funcNameToIndex[program.functions[i]->name] = i;
            }
        }

        // 各関数の呼び出し先を収集
        auto collectCallees = [](const mir::MirFunction& func) -> std::vector<std::string> {
            std::vector<std::string> callees;
            for (const auto& block : func.basic_blocks) {
                if (!block || !block->terminator)
                    continue;
                if (block->terminator->kind == mir::MirTerminator::Call) {
                    auto& callData =
                        std::get<mir::MirTerminator::CallData>(block->terminator->data);
                    if (callData.func && callData.func->kind == mir::MirOperand::FunctionRef) {
                        auto& funcName = std::get<std::string>(callData.func->data);
                        callees.push_back(funcName);
                    }
                }
                // Statement内のFunctionRef（関数ポインタ）も収集
                for (const auto& stmt : block->statements) {
                    if (!stmt || stmt->kind != mir::MirStatement::Assign)
                        continue;
                    auto& assignData = std::get<mir::MirStatement::AssignData>(stmt->data);
                    if (!assignData.rvalue)
                        continue;
                    if (assignData.rvalue->kind == mir::MirRvalue::Use) {
                        auto& useData = std::get<mir::MirRvalue::UseData>(assignData.rvalue->data);
                        if (useData.operand &&
                            useData.operand->kind == mir::MirOperand::FunctionRef) {
                            auto& funcName = std::get<std::string>(useData.operand->data);
                            callees.push_back(funcName);
                        }
                    }
                }
            }
            return callees;
        };

        // BFS: エントリポイントから到達可能な関数を収集
        std::queue<std::string> worklist;

        // エントリポイント: export関数、main、extern関数
        for (const auto& func : program.functions) {
            if (!func)
                continue;
            if (func->is_export || func->is_extern || func->name == "main" ||
                func->name == "_start" || func->name == "start_kernel" ||
                func->name.find("__lambda_") == 0) {
                if (reachableFunctions.insert(func->name).second) {
                    worklist.push(func->name);
                }
            }
        }

        // メソッド関数（TypeName__method パターン）のベース型がreachableなら追加
        // vtableエントリからも到達可能関数を追加
        for (const auto& vt : program.vtables) {
            if (!vt)
                continue;
            for (const auto& entry : vt->entries) {
                if (reachableFunctions.insert(entry.impl_function_name).second) {
                    worklist.push(entry.impl_function_name);
                }
            }
        }

        // BFS実行
        while (!worklist.empty()) {
            std::string current = worklist.front();
            worklist.pop();

            auto it = funcNameToIndex.find(current);
            if (it != funcNameToIndex.end()) {
                auto callees = collectCallees(*program.functions[it->second]);
                for (const auto& callee : callees) {
                    if (reachableFunctions.insert(callee).second) {
                        worklist.push(callee);
                    }
                }
            }

            // interface dispatch関数（InterfaceName__method）の場合、
            // 対応するvtableのimpl関数も到達可能に追加
            // 例: Printable__print が呼ばれる場合、Point__print等を追加
            size_t dunder = current.rfind("__");
            if (dunder != std::string::npos && dunder > 0) {
                std::string possibleInterface = current.substr(0, dunder);
                if (interfaceNames.count(possibleInterface) > 0) {
                    // このインターフェースのvtableからimpl関数を追加
                    for (const auto& vt : program.vtables) {
                        if (!vt)
                            continue;
                        if (vt->interface_name == possibleInterface) {
                            for (const auto& entry : vt->entries) {
                                if (reachableFunctions.insert(entry.impl_function_name).second) {
                                    worklist.push(entry.impl_function_name);
                                }
                            }
                        }
                    }
                }
            }
        }

        cm::debug::codegen::log(cm::debug::codegen::Id::LLVMConvert,
                                "DFE: " + std::to_string(reachableFunctions.size()) + "/" +
                                    std::to_string(program.functions.size()) +
                                    " functions reachable");
    }

    // 関数実装
    // 重複した関数はスキップ
    declaredFunctions.clear();
    // std::cerr << "[MIR2LLVM] Converting " << program.functions.size()
    //           << " function implementations...\n";
    size_t skippedCount = 0;
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

        // DFE: 到達不能な関数はスキップ（宣言は残すがbodyは生成しない）
        if (!reachableFunctions.empty() && reachableFunctions.count(func->name) == 0) {
            skippedCount++;
            continue;
        }

        convertFunction(*func);
        // std::cerr << "[MIR2LLVM]   -> Done converting " << funcId << "\n";
    }

    if (skippedCount > 0) {
        cm::debug::codegen::log(
            cm::debug::codegen::Id::LLVMConvertEnd,
            "DFE: skipped " + std::to_string(skippedCount) + " unreachable functions");
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

    // typedef定義マップをコピー（convertTypeでTypeAlias/Struct名の解決に使用）
    if (module.typedef_defs) {
        typedefDefs = *module.typedef_defs;
    }

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
    isUefiTarget = triple.find("windows") != std::string::npos;

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
            // 文字列型の場合: IRBuilderなしでグローバル文字列定数を作成
            if (std::holds_alternative<std::string>(gv->init_value->value)) {
                auto& str = std::get<std::string>(gv->init_value->value);
                auto strConstant = llvm::ConstantDataArray::getString(ctx.getContext(), str, true);
                auto strGlobal = new llvm::GlobalVariable(*this->module, strConstant->getType(),
                                                          true, llvm::GlobalValue::PrivateLinkage,
                                                          strConstant, gv->name + ".str");
                strGlobal->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
                initialValue = llvm::ConstantExpr::getBitCast(
                    llvm::ConstantExpr::getInBoundsGetElementPtr(
                        strConstant->getType(), strGlobal,
                        llvm::ArrayRef<llvm::Constant*>{
                            llvm::ConstantInt::get(ctx.getI64Type(), 0),
                            llvm::ConstantInt::get(ctx.getI64Type(), 0)}),
                    ctx.getPtrType());
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
    // Bug#45修正: extern_functionsにbody付きのimport先export関数が含まれる場合、
    // bodyも生成する (declareだけだとリンカエラーになる)
    for (const auto* func : module.extern_functions) {
        if (!func->basic_blocks.empty() && !func->is_extern) {
            auto funcId = generateFunctionId(*func);
            if (declaredFunctions.count(funcId) > 0)
                continue;
            declaredFunctions.insert(funcId);
            convertFunction(*func);
        }
    }

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
        asmReferencedLocals.clear();
        // NOTE: heapAllocatedLocalsはベアメタル対応のため削除（malloc不使用）

        // Bug#12修正: naked関数（ret/iret含むASM）の専用コード生成パス
        // naked関数ではalloca/load/storeを生成せず、関数引数を
        // ASMオペランドに直接マッピングする
        if (currentFunction->hasFnAttribute(llvm::Attribute::Naked)) {
            // エントリーブロック作成（ASM呼び出しのみ）
            auto entryBB = llvm::BasicBlock::Create(ctx.getContext(), "entry", currentFunction);
            builder->SetInsertPoint(entryBB);

            // ASMステートメントを探す
            for (const auto& bb : func.basic_blocks) {
                if (!bb)
                    continue;
                for (const auto& stmt : bb->statements) {
                    if (!stmt || stmt->kind != mir::MirStatement::Asm)
                        continue;
                    auto& asmData = std::get<mir::MirStatement::AsmData>(stmt->data);

                    // Bug#11修正: UEFIターゲットのレジスタリマップ
                    std::string nakedAsmCode = asmData.code;
                    if (isUefiTarget) {
                        struct RegMapping {
                            std::string sysV;
                            std::string placeholder;
                            std::string win64;
                        };
                        static const std::vector<RegMapping> regMappings = {
                            {"%rdi", "@@SYSV_ARG1_64@@", "%rcx"},
                            {"%rsi", "@@SYSV_ARG2_64@@", "%rdx"},
                            {"%edi", "@@SYSV_ARG1_32@@", "%ecx"},
                            {"%esi", "@@SYSV_ARG2_32@@", "%edx"},
                            {"%di", "@@SYSV_ARG1_16@@", "%cx"},
                            {"%si", "@@SYSV_ARG2_16@@", "%dx"},
                        };
                        for (const auto& m : regMappings) {
                            size_t pos = 0;
                            while ((pos = nakedAsmCode.find(m.sysV, pos)) != std::string::npos) {
                                nakedAsmCode.replace(pos, m.sysV.size(), m.placeholder);
                                pos += m.placeholder.size();
                            }
                        }
                        for (const auto& m : regMappings) {
                            size_t pos = 0;
                            while ((pos = nakedAsmCode.find(m.placeholder, pos)) !=
                                   std::string::npos) {
                                nakedAsmCode.replace(pos, m.placeholder.size(), m.win64);
                                pos += m.win64.size();
                            }
                        }
                    }

                    // operandの有無に関わらず、$N→レジスタ名に事前置換して
                    // operandなしASMとして生成（LLVM17 naked+operand crash回避）
                    std::string finalAsmCode = nakedAsmCode;
                    if (!asmData.operands.empty()) {
                        // 呼び出し規約に基づくレジスタ名
                        std::vector<std::string> argRegs;
                        if (isUefiTarget) {
                            // Win64cc: RCX, RDX, R8, R9
                            argRegs = {"%rcx", "%rdx", "%r8", "%r9"};
                        } else {
                            // System V: RDI, RSI, RDX, RCX, R8, R9
                            argRegs = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
                        }

                        // $N → レジスタ名に置換（2段階で交差防止）
                        struct OpMapping {
                            std::string src;
                            std::string placeholder;
                            std::string reg;
                        };
                        std::vector<OpMapping> opMappings;
                        for (size_t i = 0; i < asmData.operands.size(); ++i) {
                            std::string src = "$" + std::to_string(i);
                            std::string ph = "@@NAKED_OP" + std::to_string(i) + "@@";
                            std::string reg =
                                (i < argRegs.size()) ? argRegs[i] : ("%r" + std::to_string(10 + i));
                            opMappings.push_back({src, ph, reg});
                        }
                        // Phase 1: $N → placeholder
                        for (const auto& m : opMappings) {
                            size_t pos = 0;
                            while ((pos = finalAsmCode.find(m.src, pos)) != std::string::npos) {
                                // $$N（エスケープ）はスキップ
                                if (pos > 0 && finalAsmCode[pos - 1] == '$') {
                                    pos += m.src.size();
                                    continue;
                                }
                                finalAsmCode.replace(pos, m.src.size(), m.placeholder);
                                pos += m.placeholder.size();
                            }
                        }
                        // Phase 2: placeholder → レジスタ名
                        for (const auto& m : opMappings) {
                            size_t pos = 0;
                            while ((pos = finalAsmCode.find(m.placeholder, pos)) !=
                                   std::string::npos) {
                                finalAsmCode.replace(pos, m.placeholder.size(), m.reg);
                                pos += m.reg.size();
                            }
                        }
                    }

                    // クロバー制約を生成（ASMコード内で使用されているレジスタを検出）
                    std::string constraints = "~{memory},~{dirflag},~{fpsr},~{flags}";
                    static const std::vector<std::pair<std::string, std::string>> nakedRegPatterns =
                        {
                            {"%r10", "r10"}, {"%r11", "r11"}, {"%r12", "r12"}, {"%r13", "r13"},
                            {"%r14", "r14"}, {"%r15", "r15"}, {"%rax", "rax"}, {"%rbx", "rbx"},
                            {"%rcx", "rcx"}, {"%rdx", "rdx"}, {"%rdi", "rdi"}, {"%rsi", "rsi"},
                            {"%r8", "r8"},   {"%r9", "r9"},
                        };
                    for (const auto& [pattern, clobberName] : nakedRegPatterns) {
                        if (finalAsmCode.find(pattern) != std::string::npos) {
                            constraints += ",~{" + clobberName + "}";
                        }
                    }
                    // rspが使われている場合もクロバーに追加
                    if (finalAsmCode.find("%rsp") != std::string::npos) {
                        constraints += ",~{rsp}";
                    }

                    auto* asmFuncTy = llvm::FunctionType::get(ctx.getVoidType(), false);
                    auto* inlineAsm =
                        llvm::InlineAsm::get(asmFuncTy, finalAsmCode, constraints, true);
                    builder->CreateCall(asmFuncTy, inlineAsm);
                }
            }

            // naked関数はASM内のretで戻るため、unreachableで終了
            builder->CreateUnreachable();
            return;  // 通常のコード生成をスキップ
        }

        // asm入出力オペランドで参照される変数を事前スキャンしてasmReferencedLocalsに登録
        // これによりSSA定数伝播を防ぎ、asm結果が正しく反映される
        // BUG修正(v0.14.2): メンバ変数asmReferencedLocalsをクリアして再スキャン
        asmReferencedLocals.clear();
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
                        // 定数オペランド（is_constant=true）はlocal_id=0が設定されているが
                        // 変数を参照していないためスキップ
                        if (operand.is_constant) {
                            continue;
                        }
                        // 変数を参照するオペランド（入力/出力/tied）を登録
                        asmReferencedLocals.insert(operand.local_id);
                    }
                }
            }
        }

        // Hazard #45修正: パラメータlocalsの再代入を事前スキャン
        // MIRで再代入されるパラメータはallocaが必要（cross-block domination回避）
        std::unordered_set<unsigned int> reassignedArgLocals;
        for (size_t bbIdx = 0; bbIdx < func.basic_blocks.size(); ++bbIdx) {
            const auto& bb = func.basic_blocks[bbIdx];
            if (!bb)
                continue;
            for (const auto& stmt : bb->statements) {
                if (stmt->kind == mir::MirStatement::Assign) {
                    auto& assignData = std::get<mir::MirStatement::AssignData>(stmt->data);
                    auto targetLocal = assignData.place.local;
                    // プロジェクションなし（直接代入）の場合のみ再代入と判定
                    // _1.*.0 = ... はフィールド書込みであり、_1自体の再代入ではない
                    if (assignData.place.projections.empty() &&
                        std::find(func.arg_locals.begin(), func.arg_locals.end(), targetLocal) !=
                            func.arg_locals.end()) {
                        reassignedArgLocals.insert(static_cast<unsigned int>(targetLocal));
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
                    } else if (reassignedArgLocals.count(static_cast<unsigned int>(localIdx)) > 0) {
                        // Hazard #45修正: 再代入されるポインタ引数のみallocaに格納
                        auto alloca = builder->CreateAlloca(arg.getType(), nullptr,
                                                            "arg_" + std::to_string(argIdx));
                        builder->CreateStore(&arg, alloca);
                        locals[localIdx] = alloca;
                        allocatedLocals.insert(localIdx);
                    } else {
                        locals[localIdx] = &arg;
                    }
                } else if (reassignedArgLocals.count(static_cast<unsigned int>(localIdx)) > 0) {
                    // Hazard #45修正: 再代入される引数のみallocaに格納
                    // cross-block参照時のdomination error回避
                    auto alloca = builder->CreateAlloca(arg.getType(), nullptr,
                                                        "arg_" + std::to_string(argIdx));
                    builder->CreateStore(&arg, alloca);
                    locals[localIdx] = alloca;
                    allocatedLocals.insert(localIdx);
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

                    // asm参照変数（入力/出力/tied）はSSA形式ではなくalloca強制
                    [[maybe_unused]] bool isAsmReferenced =
                        asmReferencedLocals.count(static_cast<unsigned int>(i)) > 0;

                    // Hazard #45修正: 関数ポインタ型のallocaスキップを削除
                    // 以前はSSA形式で扱っていたが、cross-block参照でdomination errorが発生
                    // LLVM mem2regパスが自動的にSSA形式に最適化してくれる
                    // 配列へのポインタ型の一時変数もallocaを生成する（Bug#9修正）
                    // 以前はSSA形式で扱っていたが、Ref(array)の結果がSSA代入されると
                    // locals[ref_result] = locals[array] (配列alloca) となり、
                    // Copy時にCreateLoad(allocatedType=[N x T])が配列全体をloadしてしまう。
                    // これにより後続のstore先(ptr alloca=8B)にバッファオーバーフローが発生。
                    // allocaを生成してstore ptr → load ptrの正しいパスを通すことで修正。
                    // Hazard #45修正: 文字列一時変数のallocaスキップを削除
                    // cross-block参照時のdomination error回避のため常にallocaを生成

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

                        // Tagged Union型のallocaをゼロ初期化
                        // ペイロードフィールド(i8[N])の未使用バイトにゴミが残るのを防止
                        if (local.type && local.type->name.find("__TaggedUnion_") == 0) {
                            auto dataLayout = module->getDataLayout();
                            auto allocSize = dataLayout.getTypeAllocSize(llvmType);
                            builder->CreateMemSet(alloca,
                                                  llvm::ConstantInt::get(ctx.getI8Type(), 0),
                                                  allocSize, llvm::MaybeAlign());
                        }

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
                // main関数はC標準でi32を返すため、retval allocaもi32に強制
                auto llvmType =
                    (func.name == "main") ? ctx.getI32Type() : convertType(returnLocal.type);
                auto alloca = builder->CreateAlloca(llvmType, nullptr, "retval");
                // main関数のretvalは0で初期化（C標準: 正常終了=0）
                if (func.name == "main") {
                    builder->CreateStore(llvm::ConstantInt::get(ctx.getI32Type(), 0), alloca);
                }

                // struct型の戻り値をゼロ初期化（Tagged Union対応）
                // Result<ulong, long>等のペイロードi8[N]で部分書き込みによる
                // ゴミデータを防止。LLVM struct型全般に適用（安全で汎用的）
                if (llvmType->isStructTy()) {
                    auto dataLayout = module->getDataLayout();
                    auto allocSize = dataLayout.getTypeAllocSize(llvmType);
                    builder->CreateMemSet(alloca, llvm::ConstantInt::get(ctx.getI8Type(), 0),
                                          allocSize, llvm::MaybeAlign());
                }

                locals[func.return_local] = alloca;
                allocatedLocals.insert(func.return_local);  // allocaされた変数を記録
            }
        }

        // 到達可能性分析: エントリブロックから到達可能なブロックのみを変換
        // 到達不能ブロック（例: デフォルトの return 0）がLLVM O3で
        // unreachable → ud2 (x86_64 SIGILL) に最適化される問題を防止
        std::unordered_set<size_t> reachableBlocks;
        {
            std::queue<size_t> worklist;
            size_t entry = func.entry_block;
            if (entry < func.basic_blocks.size() && func.basic_blocks[entry]) {
                worklist.push(entry);
                reachableBlocks.insert(entry);
            } else if (!func.basic_blocks.empty() && func.basic_blocks[0]) {
                worklist.push(0);
                reachableBlocks.insert(0);
            }
            while (!worklist.empty()) {
                size_t current = worklist.front();
                worklist.pop();
                const auto& bb = func.basic_blocks[current];
                if (!bb)
                    continue;
                // ターミネーターの遷移先を収集
                if (bb->terminator) {
                    auto addSuccessor = [&](size_t target) {
                        if (target < func.basic_blocks.size() && func.basic_blocks[target] &&
                            reachableBlocks.insert(target).second) {
                            worklist.push(target);
                        }
                    };
                    switch (bb->terminator->kind) {
                        case mir::MirTerminator::Goto: {
                            auto& data =
                                std::get<mir::MirTerminator::GotoData>(bb->terminator->data);
                            addSuccessor(data.target);
                            break;
                        }
                        case mir::MirTerminator::SwitchInt: {
                            auto& data =
                                std::get<mir::MirTerminator::SwitchIntData>(bb->terminator->data);
                            for (auto& [_, target] : data.targets) {
                                addSuccessor(target);
                            }
                            addSuccessor(data.otherwise);
                            break;
                        }
                        case mir::MirTerminator::Call: {
                            auto& data =
                                std::get<mir::MirTerminator::CallData>(bb->terminator->data);
                            addSuccessor(data.success);
                            break;
                        }
                        case mir::MirTerminator::Return:
                            // 遷移先なし
                            break;
                        default:
                            break;
                    }
                }
            }
        }

        // 基本ブロック作成（到達可能なブロックのみ）
        for (size_t i = 0; i < func.basic_blocks.size(); ++i) {
            // DCEで削除されたブロックはスキップ
            if (!func.basic_blocks[i])
                continue;
            // 到達不能ブロックはスキップ
            if (reachableBlocks.count(i) == 0)
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
        // std::cerr << "[MIR2LLVM] Function " << func.name << " has " <<
        // func.basic_blocks.size()
        //           << " blocks\n";
        for (size_t i = 0; i < func.basic_blocks.size(); ++i) {
            // DCEで削除されたブロック / 到達不能ブロックはスキップ
            if (!func.basic_blocks[i] || reachableBlocks.count(i) == 0) {
                continue;
            }

            // std::cerr << "[MIR2LLVM]   Converting block " << i << "/" <<
            // func.basic_blocks.size()
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
    // std::cerr << "[MIR2LLVM]       Checking if block " << block.id << " is in blocks
    // map...\n";
    if (blocks.count(block.id) > 0) {
        // std::cerr << "[MIR2LLVM]       Setting insert point for block " << block.id << "\n";
        builder->SetInsertPoint(blocks[block.id]);
    } else {
        // ブロックがblocks mapに存在しない（DCEで削除された可能性）
        // std::cerr << "[MIR2LLVM]       Block " << block.id << " not in blocks map,
        // skipping\n";
        if (cm::debug::debug_mode()) {
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
    if (cm::debug::debug_mode() && currentMIRFunction && currentMIRFunction->name == "main" &&
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
                // std::cerr << "[MIR2LLVM]       Assign to local " << assign.place.local <<
                // "\n";
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

        // std::cerr << "[MIR2LLVM]       Statement " << stmt_idx << " processed
        // successfully\n"; std::cerr << "[MIR2LLVM]       About to increment stmt_idx from " <<
        // stmt_idx << " to "
        // << (stmt_idx + 1) << "\n";
        // ループの最後の反復かチェック
        if (stmt_idx == block.statements.size() - 1) {
            // std::cerr << "[MIR2LLVM]       Exiting for loop iteration " << stmt_idx << "\n";
        }
        // std::cerr << "[MIR2LLVM]       End of for loop body for stmt_idx=" << stmt_idx <<
        // "\n";
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
                        // std::cerr << "[MIR2LLVM]       Call target (const): " << *name <<
                        // "\n";
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
        if (cm::debug::debug_mode()) {
            debug_msg("CODEGEN",
                      "ERROR: BB " + std::to_string(block.id) + " has no terminator in MIR!");
        }
    }
}

// ステートメント変換
}  // namespace cm::codegen::llvm_backend
