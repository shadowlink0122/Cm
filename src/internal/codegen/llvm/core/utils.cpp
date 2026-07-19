/// @file utils.cpp
/// @brief ユーティリティ関数（外部関数宣言の入口、パニック生成、型情報取得）
/// ランタイム関数のシグネチャ宣言はruntime/builtins.cpp・runtime/system.cppに分離

#include "mir_to_llvm.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace cm::codegen::llvm_backend {

// 外部関数宣言
llvm::Function* MIRToLLVM::declareExternalFunction(const std::string& name) {
    // functionsテーブルに完全一致で登録されている場合を優先
    if (auto it = functions.find(name); it != functions.end() && it->second) {
        return it->second;
    }
    // Bug#45修正: functionsテーブルからベース名の前方一致で検索
    // impl for内から外部関数を呼ぶ場合、マングリング名の不一致により
    // functionsテーブルに登録済みの正しいシグネチャの関数が見つからない。
    // 前方一致候補が複数ある場合は不定動作防止のためスキップ
    {
        llvm::Function* candidate = nullptr;
        int matchCount = 0;
        for (const auto& [fName, fFunc] : functions) {
            if (fFunc && fName.find(name + "_") == 0) {
                candidate = fFunc;
                matchCount++;
                if (matchCount > 1)
                    break;  // 複数候補は使用しない
            }
        }
        if (matchCount == 1 && candidate) {
            return candidate;
        }
    }

    // print/型変換/文字列/スライス/配列系の組み込みランタイム宣言（runtime/builtins.cpp）
    if (auto* builtinFunc = declareBuiltinRuntimeFunction(name)) {
        return builtinFunc;
    }
    // net/atomic/channel/thread/http系のシステムランタイム宣言（runtime/system.cpp）
    if (auto* systemFunc = declareSystemRuntimeFunction(name)) {
        return systemFunc;
    }

    // currentProgramから関数情報を取得（extern関数だけでなく、全ての関数を検索）
    // これにより、モノモーフィック化されたメソッド（Container__int__get等）も正しいシグネチャで宣言される
    if (currentProgram) {
        for (const auto& func : currentProgram->functions) {
            if (func && func->name == name) {
                // 戻り値型
                llvm::Type* returnType = ctx.getVoidType();
                if (func->return_local < func->locals.size()) {
                    auto& returnLocal = func->locals[func->return_local];
                    if (returnLocal.type && returnLocal.type->kind != hir::TypeKind::Void) {
                        returnType = convertType(returnLocal.type);
                    }
                }

                // パラメータ型
                std::vector<llvm::Type*> paramTypes;
                for (const auto& arg_local : func->arg_locals) {
                    if (arg_local < func->locals.size()) {
                        auto& local = func->locals[arg_local];
                        if (local.type) {
                            paramTypes.push_back(convertType(local.type));
                        }
                    }
                }

                // 関数型（可変長引数を考慮）
                auto funcType = llvm::FunctionType::get(returnType, paramTypes, func->is_variadic);
                auto result = module->getOrInsertFunction(name, funcType);
                return llvm::cast<llvm::Function>(result.getCallee());
            }
        }
    }

    // モジュール分割コンパイル時: allModuleFunctionsから検索
    if (!allModuleFunctions.empty()) {
        for (const auto* func : allModuleFunctions) {
            if (func && func->name == name) {
                // 戻り値型
                llvm::Type* returnType = ctx.getVoidType();
                if (func->return_local < func->locals.size()) {
                    auto& returnLocal = func->locals[func->return_local];
                    if (returnLocal.type && returnLocal.type->kind != hir::TypeKind::Void) {
                        returnType = convertType(returnLocal.type);
                    }
                }

                // パラメータ型
                std::vector<llvm::Type*> paramTypes;
                for (const auto& arg_local : func->arg_locals) {
                    if (arg_local < func->locals.size()) {
                        auto& local = func->locals[arg_local];
                        if (local.type) {
                            paramTypes.push_back(convertType(local.type));
                        }
                    }
                }

                // 関数型（可変長引数を考慮）
                auto funcType = llvm::FunctionType::get(returnType, paramTypes, func->is_variadic);
                auto result = module->getOrInsertFunction(name, funcType);
                return llvm::cast<llvm::Function>(result.getCallee());
            }
        }
    }

    // Bug#45修正: ベース名の前方一致検索（マングリング名の不一致を解決）
    // impl for ブロック内から外部関数を呼ぶ場合、呼び出し側はベース名（例: heap_size_to_class）を使用するが、currentProgram内の関数名はマングリング済み（例: heap_size_to_class_u64）。
    // 完全一致で見つからなかった場合、name + "_" で始まる関数を検索して正しいシグネチャを取得する。
    if (currentProgram) {
        for (const auto& func : currentProgram->functions) {
            if (!func)
                continue;
            // ベース名 + "_" で始まるマングリング済み関数を検索
            if (func->name.find(name + "_") == 0) {
                // 戻り値型
                llvm::Type* returnType = ctx.getVoidType();
                if (func->return_local < func->locals.size()) {
                    auto& returnLocal = func->locals[func->return_local];
                    if (returnLocal.type && returnLocal.type->kind != hir::TypeKind::Void) {
                        returnType = convertType(returnLocal.type);
                    }
                }

                // パラメータ型
                std::vector<llvm::Type*> paramTypes;
                for (const auto& arg_local : func->arg_locals) {
                    if (arg_local < func->locals.size()) {
                        auto& local = func->locals[arg_local];
                        if (local.type) {
                            paramTypes.push_back(convertType(local.type));
                        }
                    }
                }

                // 関数型
                auto funcType = llvm::FunctionType::get(returnType, paramTypes, func->is_variadic);
                auto result = module->getOrInsertFunction(name, funcType);
                return llvm::cast<llvm::Function>(result.getCallee());
            }
        }
    }

    // 最終フォールバック: void() として宣言（本来ここには到達しないはず）
    std::cerr << "[WARN] declareExternalFunction: unknown function '" << name
              << "' - using void() signature" << std::endl;
    auto funcType = llvm::FunctionType::get(ctx.getVoidType(), false);
    auto func = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, name, module);
    return func;
}

// 組み込み関数呼び出し（将来の実装用）
llvm::Value* MIRToLLVM::callIntrinsic([[maybe_unused]] const std::string& name,
                                      [[maybe_unused]] llvm::ArrayRef<llvm::Value*> args) {
    return nullptr;
}

// NOTE: freeHeapAllocatedLocals() はベアメタル対応のため削除すべての配列はスタックに割り当てられるため、明示的な解放は不要

// パニック生成
void MIRToLLVM::generatePanic(const std::string& message) {
    auto msgStr = builder->CreateGlobalStringPtr(message, "panic_msg");
    auto putsFunc = declareExternalFunction("puts");
    builder->CreateCall(putsFunc, {msgStr});

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
