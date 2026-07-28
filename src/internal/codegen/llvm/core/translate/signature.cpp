/// @file signature.cpp
/// @brief 関数ID生成と関数シグネチャ変換（オーバーロード対応のマングリングを含む）

#include "internal/base/debug/codegen.hpp"
#include "internal/codegen/llvm/core/mir_to_llvm.hpp"
#include "internal/codegen/llvm/monitoring/compilation_guard.hpp"

#include <iostream>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cm::codegen::llvm_backend {

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
    // 16バイト超構造体の戻り値はsret（先頭の隠し出力ポインタ）へ変換する（C14 Phase 4。
    // 第一級集約returnのSROA全展開によるO2二次爆発を防ぐ）
    bool useSret = needsSretReturn(func);
    llvm::Type* returnType;
    if (func.name == "main") {
        returnType = ctx.getI32Type();
    } else if (useSret) {
        returnType = ctx.getVoidType();
        paramTypes.insert(paramTypes.begin(), ctx.getPtrType());
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
    // Bug#45修正: 同名の既存関数がvoid()で先に作成されている場合がある（MIR内にarg_locals空のエントリと非空のエントリが重複して存在するため）
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

    // sretパラメータへ属性を付与（最適化に呼び出し元バッファへの直接書き込みであることを伝える）
    if (useSret && func.return_local < func.locals.size()) {
        auto retLlvmType = convertType(func.locals[func.return_local].type);
        llvmFunc->addParamAttr(
            0, llvm::Attribute::get(ctx.getContext(), llvm::Attribute::StructRet, retLlvmType));
        llvmFunc->addParamAttr(0, llvm::Attribute::get(ctx.getContext(), llvm::Attribute::NoAlias));
    }

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
            // Naked属性を付与しない。Naked関数ではASM文のみ出力されるため、混在関数の通常コード（関数呼び出し等）が省略されてGPFを引き起こす
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
        // インライン展開されるとefi_mainの引数レジスタ(rcx/rdx)がインライン展開されたコードのself/引数として上書きされ、UEFIデータ構造が破壊される
        llvmFunc->addFnAttr(llvm::Attribute::NoInline);
        // Bug#17修正: スタックプローブ無効化
        // LLVMはWindowsターゲットで4KB以上のスタックフレームに___chkstk_msを挿入するが、UEFI/ベアメタル環境ではこの関数が存在しないためリンクエラーになる
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

}  // namespace cm::codegen::llvm_backend
