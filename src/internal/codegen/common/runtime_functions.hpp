/// @file runtime_functions.hpp
/// @brief ランタイム関数宣言の共通定義
///
/// native/wasmで共通のランタイム関数宣言

#pragma once

#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <string>
#include <unordered_map>

namespace cm::codegen::common {

/// ランタイム関数シグネチャ
struct RuntimeFunctionSig {
    std::string name;
    llvm::Type* returnType;
    std::vector<llvm::Type*> argTypes;
    bool isVarArg;
};

/// ランタイム関数宣言ヘルパー
class RuntimeFunctions {
   private:
    llvm::Module& module;
    llvm::LLVMContext& ctx;

    // 型キャッシュ
    llvm::Type* voidTy;
    llvm::Type* i8Ty;
    llvm::Type* i32Ty;
    llvm::Type* i64Ty;
    llvm::Type* f64Ty;
    llvm::Type* ptrTy;

    // 関数キャッシュ
    std::unordered_map<std::string, llvm::Function*> cache;

   public:
    RuntimeFunctions(llvm::Module& mod, llvm::LLVMContext& context);

    /// ランタイム関数を取得（必要に応じて宣言）
    llvm::Function* get(const std::string& name);

    /// print系関数
    llvm::Function* getPrintString();
    llvm::Function* getPrintlnString();
    llvm::Function* getPrintInt();
    llvm::Function* getPrintlnInt();
    llvm::Function* getPrintDouble();
    llvm::Function* getPrintlnDouble();
    llvm::Function* getPrintBool();
    llvm::Function* getPrintlnBool();
    llvm::Function* getPrintChar();
    llvm::Function* getPrintlnChar();

    /// 型変換関数
    llvm::Function* getIntToString();
    llvm::Function* getUIntToString();
    llvm::Function* getCharToString();
    llvm::Function* getBoolToString();
    llvm::Function* getDoubleToString();

    /// 文字列操作関数
    llvm::Function* getStringConcat();
    llvm::Function* getFormatString();
    llvm::Function* getFormatReplace();
    llvm::Function* getFormatReplaceInt();
    llvm::Function* getFormatReplaceDouble();
    llvm::Function* getFormatReplaceString();

    /// パニック関数
    llvm::Function* getPanic();

   private:
    llvm::Function* declare(const std::string& name, llvm::Type* retTy,
                            std::vector<llvm::Type*> argTys, bool isVarArg = false);
};

}  // namespace cm::codegen::common
